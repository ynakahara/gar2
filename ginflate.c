// ginflate.c : decompress DEFLATE (RFC 1951).

#include "garlib.h"
#include "garaux.h"
#include <string.h>


//-----------------------------------------------------------------------------
// Types

#define BYTE_BIT 8

typedef unsigned char ginflate_byte_t; // 8bit
typedef unsigned int ginflate_uint_t; // fast uint of 32bit or more
typedef unsigned short ginflate_word_t; // small uint of 16bit or more


typedef struct ginflate_hdic {
  ginflate_byte_t max_codelen;
  ginflate_word_t lookup[32768];
} ginflate_hdic_t;


typedef struct ginflate_tag {
  ginflate_uint_t ringbuf_pos; // next write position in ringbuf.
  ginflate_uint_t bits_acc; // accumulator of input bits.
  ginflate_uint_t bits_len; // number of accumulated bits in bits_acc.
  const ginflate_byte_t *input_p; // pointer of input buffer.
  const ginflate_byte_t *input_pend; // end of input buffer.
  ginflate_uint_t match_len; // match length or non-compressed block length.
  ginflate_uint_t match_dist; // match distance.
  ginflate_byte_t bfinal; // the BFINAL flag value of the current block.
  ginflate_byte_t err; // last error.
  ginflate_byte_t ringbuf[64*1024];
  ginflate_byte_t inputbuf[1024];
  ginflate_hdic_t hdic_lit[1];
  ginflate_hdic_t hdic_dist[1];
  ginflate_byte_t *
    (*infl)(struct ginflate_tag *, ginflate_byte_t *, ginflate_byte_t *);
  jmp_buf env;
  gar_gfile_t gf;
} ginflate_t;


//-----------------------------------------------------------------------------
// Error Messages

static const char c_prefix[] = "(inflate)";
static const char c_err_eof[] = "unexpected EOF";
static const char c_err_corrupt[] = "corrupted input data";
static const char c_err_unknown[] = "corrupted inflating buffer";
static const char c_err_seek[] = "the stream is not seekable";
static const char c_err_dup[] = "the stream cannot be duplicated";


//-----------------------------------------------------------------------------
// Constants

#define codelen_bits 4
#define codelen_limit 16


static const ginflate_byte_t c_clen_order[] = {
  16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
};


typedef struct extra_def {
  ginflate_word_t bits;
  ginflate_word_t base;
} extra_def_t;


static const extra_def_t c_lenext[] = {
#include "lenext.inc"
};


static const extra_def_t c_distext[] = {
#include "distext.inc"
};


static const extra_def_t c_clenext[] = {
  { 2, 3 },
  { 3, 3 },
  { 7, 11 },
};


//-----------------------------------------------------------------------------
// Forward Declarations

#define declare_inflate_fn(nm) \
  ginflate_byte_t *nm(ginflate_t *I, ginflate_byte_t *p, ginflate_byte_t *pend)


static declare_inflate_fn(inflate_block);
static declare_inflate_fn(inflate_stored);
static declare_inflate_fn(inflate_compressed);
static declare_inflate_fn(inflate_error);


//-----------------------------------------------------------------------------
// Attributes

static void error(ginflate_t *I, const char *msg)
  __attribute__((noreturn));

static ginflate_uint_t fetch_bits(ginflate_t *I, ginflate_uint_t n)
  __attribute__((always_inline));

static void drop_bits(ginflate_t *I, ginflate_uint_t n)
  __attribute__((always_inline));

static ginflate_uint_t decode_huff(ginflate_t *I, const ginflate_hdic_t *hdic)
  __attribute__((always_inline));

static ginflate_uint_t decode_ext(ginflate_t *I, const extra_def_t ext[],
                                  ginflate_uint_t c)
  __attribute__((always_inline));


//-----------------------------------------------------------------------------
// Auxiliary

/// Get the minimum value of the given unsigned integers.
static ginflate_uint_t umin(ginflate_uint_t x, ginflate_uint_t y) {
  return (x < y) ? x : y;
}


/// Raise error (this function doesn't return).
static void error(ginflate_t *I, const char *msg) {
  I->infl = &inflate_error; // don't decompress any more.
  _gar_error(I->env, c_prefix, msg);
}


//-----------------------------------------------------------------------------
// Bitwise Operations

/// Fetch a new byte string.
static const ginflate_byte_t *fetch_bytes(ginflate_t *I) {
  size_t n;

  n = gar_gfile_read(&I->gf, I->inputbuf, sizeof(I->inputbuf), I->env);
  if (n == 0) return NULL; // there is no more byte to decompress.

  I->input_p = I->inputbuf;
  I->input_pend = &I->inputbuf[n];
  return I->inputbuf;
}


/// Make bit mask of lower @a n bits.
static ginflate_uint_t bitmask(ginflate_uint_t n) {
  return (1 << n) - 1;
}


/**
 * @brief Fetch the specified number of bits and get its value.
 *
 * The number of the actually fetched bits can be less than the specified
 * number of bits even if this function is called, if there is no more bit to
 * fetch (reached the EOF). In this case, the return value can contain
 * undefined bits.
 */
static ginflate_uint_t fetch_bits(ginflate_t *I, ginflate_uint_t n) {
  while (I->bits_len < n) {
    const ginflate_byte_t *p;

    p = I->input_p;
    if (p == I->input_pend) { // need to fetch the next byte string?
      p = fetch_bytes(I);
      if (p == NULL) break; // there is no more input data.
    }

    I->bits_acc += (ginflate_uint_t)*p++ << I->bits_len;
    I->bits_len += BYTE_BIT;
    I->input_p = p;
  }

  return I->bits_acc & bitmask(n);
}


/// Remove the specified number of fetched bits.
static void drop_bits(ginflate_t *I, ginflate_uint_t n) {
  // The following n bits have to have been fetched onto the accumurator
  // by the leading fetch_bits() function.
  // The (I->bits_len) value is less than (n) only when there was no more
  // input data.
  if (n > I->bits_len) error(I, c_err_eof); // insufficient input data.

  // Drop n bits from the accumulator.
  I->bits_acc >>= n;
  I->bits_len -= n;
}


/// Remove fetched bits until the next byte boundary.
static void drop_to_byte(ginflate_t *I) {
  ginflate_uint_t m = I->bits_len % BYTE_BIT;
  I->bits_acc >>= m;
  I->bits_len -= m;
}


/// Get the specified number of bits.
static ginflate_uint_t get_bits(ginflate_t *I, ginflate_uint_t n) {
  if (n <= I->bits_len) { // all the required bits have been fetched.
    ginflate_uint_t bs;

    // Get n bits from the accumulator.
    bs = I->bits_acc & bitmask(n);
    I->bits_acc >>= n;
    I->bits_len -= n;

    return bs;
  }
  else {
    const ginflate_byte_t *p;
    ginflate_uint_t bs, bl;

    p = I->input_p;
    bs = I->bits_acc;
    bl = I->bits_len;

    do {
      if (p == I->input_pend) { // need to fetch the next byte string?
        p = fetch_bytes(I);
        if (p == NULL) error(I, c_err_eof); // insufficient input data.
      }

      bs += (ginflate_uint_t)*p++ << bl;
      bl += BYTE_BIT;
    }
    while (bl < n);

    I->input_p = p;
    I->bits_acc = (bs >> n);
    I->bits_len = (bl - n);

    return bs & bitmask(n);
  }
}


//-----------------------------------------------------------------------------
// Decoding Huffman/Extra Codes

/// Pack symbol and bit length values into a single word.
static ginflate_word_t pack_symb_and_bl(ginflate_uint_t symb,
                                        ginflate_uint_t bl) {
  return (ginflate_word_t)((symb << codelen_bits) + bl);
}


/// Get the symbol value from a word packed by pack_symb_and_bl().
static ginflate_uint_t unpack_symb(ginflate_uint_t packed) {
  return packed >> codelen_bits;
}


/// Get the bit length value from a word packed by pack_symb_and_bl().
static ginflate_uint_t unpack_bl(ginflate_uint_t packed) {
  return packed & bitmask(codelen_bits);
}


/// Reverse the bit pattern.
static ginflate_uint_t reverse_bits(ginflate_uint_t c, ginflate_uint_t n) {
  ginflate_uint_t i;
  ginflate_uint_t d = 0;
  for (i = 0; i < n; i++) {
    d = (d << 1) + (c & 1);
    c >>= 1;
  }
  return d;
}


/// Initialize a Huffman dictionary from the code lengths.
static void init_huffdic(const ginflate_byte_t codelens[],
                         ginflate_uint_t num_codes,
                         ginflate_hdic_t *hdic) {
  ginflate_uint_t i;
  ginflate_uint_t code;
  ginflate_uint_t bl_count[codelen_limit] = { 0 };
  ginflate_uint_t next_code[codelen_limit];
  ginflate_uint_t max_codelen;
  ginflate_uint_t hdic_size;

  // Count the number of code for each code length.
  for (i = 0; i < num_codes; i++) {
    bl_count[codelens[i]]++;
  }

  // Find the numerical value of the smallest code for each code length.
  code = 0;
  bl_count[0] = 0;
  for (i = 1; i < codelen_limit; i++) {
    code = (code + bl_count[i-1]) << 1;
    next_code[i] = code;
  }

  // Get the maximum code lengths and the optimal dictionary size.
  // Only the slice of hdic[0..hdic_size] is used because filling all the
  // hdic[] array with valid values costed *very* much, in experience.
  max_codelen = 0;
  for (i = 1; i <= 15; i++) {
    if (bl_count[i]) max_codelen = i;
  }
  hdic->max_codelen = max_codelen;
  hdic_size = (1 << max_codelen);
  memset(hdic->lookup, 0, sizeof(ginflate_word_t) * hdic_size);

  // Assign the obtained codes to the lookup table.
  for (i = 0; i < num_codes; i++) {
    ginflate_uint_t bl = codelens[i];

    if (bl > 0) { // the code is used?
      ginflate_uint_t c = next_code[bl]++;
      ginflate_uint_t w = pack_symb_and_bl(i, bl);
      ginflate_uint_t cstep = (1 << bl);
      c = reverse_bits(c, bl);
      for (; c < hdic_size; c += cstep) {
        hdic->lookup[c] = w;
      }
    }
  }
}


/// Decode a Huffman code.
static ginflate_uint_t decode_huff(ginflate_t *I, const ginflate_hdic_t *hdic){
  ginflate_uint_t w;
  w = hdic->lookup[fetch_bits(I, hdic->max_codelen)];
  drop_bits(I, unpack_bl(w));
  return unpack_symb(w);
}


/// Decode a code that is coded with <em>extra field</em>.
static ginflate_uint_t decode_ext(ginflate_t *I, const extra_def_t ext[],
                                  ginflate_uint_t c) {
  return ext[c].base + get_bits(I, ext[c].bits);
}


//-----------------------------------------------------------------------------
// Block Headers

/// Begin decompressing a non-compressed block.
static void setup_stored(ginflate_t *I) {
  ginflate_uint_t len, nlen;

  drop_to_byte(I);
  len = get_bits(I, 16);
  nlen = get_bits(I, 16);
  if (len != (nlen ^ 0xffffU)) {
    error(I, c_err_corrupt);
  }

  // Set the remaining number of bytes in this block.
  I->match_len = len;

  // Start to decode stored (non-compressed) block.
  I->infl = &inflate_stored;
}


/// Begin decompressing a block compressed with fixed Huffman codes.
static void setup_fixed_huffman(ginflate_t *I) {
  ginflate_uint_t i;
  ginflate_byte_t clbuf[288];

  // Get the Huffman dict. for literals/lengths.
  i = 0;
  while (i <= 143) clbuf[i++] = 8;
  while (i <= 255) clbuf[i++] = 9;
  while (i <= 279) clbuf[i++] = 7;
  while (i <= 287) clbuf[i++] = 8;
  init_huffdic(clbuf, i, I->hdic_lit);

  // Get the Huffman dict. for distances.
  i = 0;
  while (i <= 31) clbuf[i++] = 5;
  init_huffdic(clbuf, i, I->hdic_dist);

  // Start to decode compressed block.
  I->infl = &inflate_compressed;
}


/// Decode a code length table of dynamic Huffman code.
static void decode_clen(ginflate_t *I, const ginflate_hdic_t *hdic_clen,
                        ginflate_byte_t clbuf[], ginflate_uint_t num_codes) {
  ginflate_uint_t i = 0;
  while (i < num_codes) {
    ginflate_uint_t l = decode_huff(I, hdic_clen);
    if (l < 16) {
      clbuf[i++] = (ginflate_byte_t)l;
    } else {
      ginflate_uint_t j;
      ginflate_byte_t c = (i > 0 && l == 16) ? clbuf[i-1] : 0;
      ginflate_uint_t n = decode_ext(I, c_clenext, l-16);
      for (j = 0; j < n; j++) {
        clbuf[i+j] = c;
      }
      i += n;
    }
  }
}


/// Begin decompressing a block compressed with dynamic Huffman codes.
static void setup_dynamic_huffman(ginflate_t *I) {
  ginflate_uint_t i;
  ginflate_uint_t hlit;
  ginflate_uint_t hdist;
  ginflate_uint_t hclen;
  ginflate_byte_t clbuf[288] = { 0 };
  ginflate_hdic_t *hdic_clen = I->hdic_dist;

  hlit = get_bits(I, 5);
  hdist = get_bits(I, 5);
  hclen = get_bits(I, 4);

  // Get the Huffman dict. for code lengths.
  for (i = 0; i < hclen+4; i++) {
    clbuf[c_clen_order[i]] = get_bits(I, 3);
  }
  init_huffdic(clbuf, 19, hdic_clen);

  // Get the Huffman dict. for literals/lengths.
  decode_clen(I, hdic_clen, clbuf, hlit+257);
  init_huffdic(clbuf, hlit+257, I->hdic_lit);

  // Get the Huffman dict. for distances.
  decode_clen(I, hdic_clen, clbuf, hdist+1);
  init_huffdic(clbuf, hdist+1, I->hdic_dist);

  // Start to decode compressed block.
  I->infl = &inflate_compressed;
}


/// Raise error on undefined block type (BTYPE = 3).
static void setup_error(ginflate_t *I) {
  error(I, c_err_corrupt); // invalid block type (btype).
}


/// Table of the functions with which to begin decompression a new block.
static void(*const c_setup_fn[])(ginflate_t *) = {
  &setup_stored,
  &setup_fixed_huffman,
  &setup_dynamic_huffman,
  &setup_error,
};


/// Decode block header.
static declare_inflate_fn(inflate_block) {
  ginflate_uint_t bfinal;
  ginflate_uint_t btype;

  if (I->bfinal) return p; // there is no more block to inflate.

  bfinal = get_bits(I, 1);
  btype = get_bits(I, 2);

  I->bfinal = bfinal;
  c_setup_fn[btype](I);

  return I->infl(I, p, pend);
}


//-----------------------------------------------------------------------------
// Decompression

/// Put a new byte to the ring buffer.
static ginflate_byte_t ringbuf_put(ginflate_t *I, ginflate_byte_t c) {
  I->ringbuf[I->ringbuf_pos] = c;
  I->ringbuf_pos = (I->ringbuf_pos + 1) % sizeof(I->ringbuf);
  return c;
}


/// Decompress a non-compressed block.
static declare_inflate_fn(inflate_stored) {
  ginflate_uint_t i;
  ginflate_uint_t n = umin(I->match_len, pend-p);
  for (i = 0; i < n; i++) {
    p[i] = ringbuf_put(I, get_bits(I, 8));
  }
  I->match_len -= n;
  p += n;
  if (I->match_len == 0) { // reached the end of block.
    I->infl = &inflate_block;
    return inflate_block(I, p, pend);
  }
  return p;
}


/// Expand a match (of Lampel-Ziv) in compressed block.
static ginflate_byte_t *expand_match(ginflate_t *I,
                                     ginflate_byte_t *p,
                                     ginflate_byte_t *pend) {
  ginflate_uint_t i;
  ginflate_uint_t n = umin(I->match_len, pend-p);
  ginflate_uint_t dist = I->match_dist;
  ginflate_uint_t pos = I->ringbuf_pos;
  ginflate_byte_t *ringbuf = I->ringbuf;
  for (i = 0; i < n; i++) {
    ginflate_byte_t c = ringbuf[(pos - dist) % sizeof(I->ringbuf)];
    ringbuf[pos] = c;
    *p++ = c;
    pos = (pos + 1) % sizeof(I->ringbuf);
  }
  I->match_len -= n;
  I->ringbuf_pos = pos;
  return p;
}


/// Decompress a compressed block.
static declare_inflate_fn(inflate_compressed) {
  if (I->match_len > 0) {
    p = expand_match(I, p, pend);
  }

  while (p < pend) {
    ginflate_uint_t l = decode_huff(I, I->hdic_lit);
    if (l < 256) {
      *p++ = ringbuf_put(I, (ginflate_byte_t)l);
    }
    else if (l >= 257) {
      I->match_len = decode_ext(I, c_lenext, l-257);
      I->match_dist = decode_ext(I, c_distext, decode_huff(I, I->hdic_dist));
      p = expand_match(I, p, pend);
    }
    else { // end of block.
      I->infl = &inflate_block;
      return inflate_block(I, p, pend);
    }
  }

  return p;
}


/**
 * @brief Raise decompression error.
 *
 * This function is set to ginflate_t::infl by the error() function; once this
 * function is set, no more byte is decompressed.
 */
static declare_inflate_fn(inflate_error) {
  ((void)p);
  ((void)pend);
  error(I, c_err_unknown);
}


//-----------------------------------------------------------------------------
// Meta Operations

void ginflate_init(ginflate_t *I) {
  I->ringbuf_pos = 0;
  I->bits_acc = 0;
  I->bits_len = 0;
  I->input_p = NULL;
  I->input_pend = NULL;
  I->match_len = 0;
  I->match_dist = 0;
  I->bfinal = 0;
  I->err = 0;
  I->infl = &inflate_block;
  gar_gfile_null(&I->gf);
}


static size_t ginflate(ginflate_t *I, void *ptr, size_t n) {
  ginflate_byte_t *p = (ginflate_byte_t *)ptr;
  ginflate_byte_t *q = (*I->infl)(I, p, p+n);
  return q - p;
}


//-----------------------------------------------------------------------------
// Stream

static size_t ginflate_on_read(void *ud, void *ptr, size_t n, jmp_buf env) {
  ginflate_t *I = (ginflate_t *)ud;
  if (setjmp(I->env)) { longjmp(env, 1); }
  return ginflate(I, ptr, n);
}


static void ginflate_on_seek(void *ud, gar_off_t off, jmp_buf env) {
  ((void)ud);
  ((void)off);
  _gar_error(env, c_prefix, c_err_seek);
}


static void ginflate_on_dup(void *ud, gar_gfile_t *dst, jmp_buf env) {
  ((void)ud);
  ((void)dst);
  _gar_error(env, c_prefix, c_err_dup);
}


static void ginflate_on_close(void *ud) {
  ginflate_t *I = (ginflate_t *)ud;
  gar_gfile_close(&I->gf);
  _gar_free(I);
}


static ginflate_t *ginflate_on_open(gar_gfile_v *gf, jmp_buf env) {
  ginflate_t *I;

  // Allocate a new ginflate_t instance and initialize it.
  I = _gar_malloc(sizeof(ginflate_t), env);
  ginflate_init(I);

  // Move the given source stream.
  I->gf = *gf;
  gar_gfile_null(gf); // get the ownership.

  return I;
}


static const gar_gfile_t c_ginflate_fn = {
  NULL,
  &ginflate_on_read,
  &ginflate_on_seek,
  &ginflate_on_dup,
  &ginflate_on_close,
};


void gar_inflate(gar_gfile_v *gf, jmp_buf env) {
  gf->ud = ginflate_on_open(gf, env);
  gf->read = c_ginflate_fn.read;
  gf->seek = c_ginflate_fn.seek;
  gf->dup = c_ginflate_fn.dup;
  gf->close = c_ginflate_fn.close;
}
