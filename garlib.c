// garlib.c : the core functions of the GAR library.

#include "gar.h"
#include "garlib.h"
#include "garaux.h"
#include <string.h>


struct gar {
  gar_gfile_t gf;
};


/// Open the specified (generalized) file as an archive.
gar_t *gar_archive_gopen(gar_gfile_t *gf, jmp_buf _env) {
  jmp_buf env;
  gar_t *volatile G = NULL;

  if (setjmp(env)) {
    gar_archive_close(G);
    longjmp(_env, 1);
  }

  // Allocate a new gar_t instance and move the specified file onto it.
  G = _gar_malloc(sizeof(gar_t), env);
  G->gf = *gf;
  gar_gfile_null(gf); // get the ownership.

  return G;
}


/// Close an archive.
void gar_archive_close(gar_t *G) {
  if (G != NULL) {
    gar_gfile_close(&G->gf);
    _gar_free(G);
  }
}


typedef unsigned char byte_t;
typedef unsigned long u32_t;
typedef unsigned short u16_t;


typedef struct pk0304_header {
  u32_t sig;
  u16_t need_ver;
  u16_t flags;
  u16_t comp_method;
  u16_t last_mod_time;
  u16_t last_mod_date;
  u32_t crc32;
  u32_t comp_size;
  u32_t uncomp_size;
  u16_t fname_len;
  u16_t extra_len;
} pk0304_header_t;


/// Decode an unsigned integer of 32bits in little endian.
static void decode_u32_le(const byte_t s[4], u32_t *t) {
  u32_t a = s[0];
  u32_t b = s[1];
  u32_t c = s[2];
  u32_t d = s[3];
  *t = a | (b << 8) | (c << 16) | (d << 24);
}


/// Decode an unsigned integer of 16bits in little endian.
static void decode_u16_le(const byte_t s[2], u16_t *t) {
  u32_t a = s[0];
  u32_t b = s[1];
  *t = a | (b << 8);
}


/// Try to read the next PK0304 chunk header (local file header).
/// @retval 1  if the header is successfully read.
/// @retval 0  if there is no more PK0304 chunk.
static int read_pk0304_header(const gar_gfile_t *gf, pk0304_header_t *hdr,
                              jmp_buf env) {
  byte_t s[30];
  size_t n;
 
  // Try to read the next PK0304 chunk header (local file header).
  n = gar_gfile_read(gf, s, sizeof(s), env);
  if (n < sizeof(s) || memcmp(s, "PK\3\4", 4)) {
    return 0; // there is no more PK0304 chunk.
  }

  // Decode the header values.
  decode_u32_le(&s[0], &hdr->sig);
  decode_u16_le(&s[4], &hdr->need_ver);
  decode_u16_le(&s[6], &hdr->flags);
  decode_u16_le(&s[8], &hdr->comp_method);
  decode_u16_le(&s[10], &hdr->last_mod_time);
  decode_u16_le(&s[12], &hdr->last_mod_date);
  decode_u32_le(&s[14], &hdr->crc32);
  decode_u32_le(&s[18], &hdr->comp_size);
  decode_u32_le(&s[22], &hdr->uncomp_size);
  decode_u16_le(&s[26], &hdr->fname_len);
  decode_u16_le(&s[28], &hdr->extra_len);

  return 1;
}


/// Full status of a zipped file.
typedef struct gar_zstat {
  gar_fstat_t fstat;
  u16_t comp_method;
  gar_off_t data_off;
  gar_off_t data_len;
} gar_zstat_t;


/// Enumerate all the zipped files.
int gar_enum(gar_t *G, gar_enum_t fn, void *ud, jmp_buf _env) {
  jmp_buf env;
  char *volatile fname = NULL;
  size_t fname_cap;
  const size_t initial_fname_cap = 128;
  pk0304_header_t hdr;
  gar_zstat_t zstat;
  gar_off_t off;
  int result;

  if (setjmp(env)) {
    _gar_free(fname);
    longjmp(_env, 1);
  }

  // Allocate file name buffer with the initial capacity.
  fname = _gar_malloc(initial_fname_cap, env);
  fname_cap = initial_fname_cap;

  // Initialize the variables.
  off = 0;
  result = 0;

  for (;;) {
    // Read the next pk0304 chunk header.
    gar_gfile_seek(&G->gf, off, env);
    if (!read_pk0304_header(&G->gf, &hdr, env)) break;

    // Extend the file name buffer if it is too short.
    if (fname_cap < hdr.fname_len+1U) {
      fname = _gar_realloc(fname, hdr.fname_len+1U, env);
      fname_cap = hdr.fname_len+1U;
    }

    // Read the file name.
    if (gar_gfile_read(&G->gf, fname, hdr.fname_len, env) < hdr.fname_len) {
      break; // insufficient input data.
    }
    fname[hdr.fname_len] = 0;

    // Invoke the callback function.
    zstat.fstat.fname = fname;
    zstat.fstat.fsize = hdr.uncomp_size;
    zstat.comp_method = hdr.comp_method;
    zstat.data_off = off + 30 + hdr.fname_len + hdr.extra_len;
    zstat.data_len = hdr.comp_size;
    result = (*fn)(&zstat.fstat, ud, env);
    if (result != 0) break;

    // Get the offset of the next chunk.
    off += 30 + hdr.fname_len + hdr.extra_len + hdr.comp_size;
  }

  // Cleanup.
  _gar_free(fname);

  return result;
}


/// User-defined argument for the on_find() callback function.
typedef struct find_ud {
  const char *fname; ///< Name of the zipped file to find out.
  gar_zstat_t *zstat; ///< Location to which to store the found file's status.
} find_ud_t;


/// Callback function to find out a zipped file.
static int on_find(const gar_fstat_t *fstat, void *ud, jmp_buf env) {
  const gar_zstat_t *zstat = (const gar_zstat_t *)fstat;
  find_ud_t *fud = (find_ud_t *)ud;
  ((void)env);

  if (strcmp(fud->fname, zstat->fstat.fname) == 0) {
    *(fud->zstat) = *zstat; // store the found file's status.
    fud->zstat->fstat.fname = NULL; // fname is valid only in this call.
    return 1; // stop enumeration.
  } else {
    return 0; // look for the next zipped file.
  }
}


/// Get the full status of the specified zipped file.
/// @retval 1  if the specified zipped file is found.
/// @retval 0  if the specified zipped file is not found.
static int gar_zstat(gar_t *G, const char *fname, gar_zstat_t *zstat,
                     jmp_buf env) {
  find_ud_t fud;
  fud.fname = fname;
  fud.zstat = zstat;
  return gar_enum(G, &on_find, &fud, env); // return 1 if the file is found.
}


/// Get the status of the specified zipped file.
/// @retval 1  if the specified zipped file is found.
/// @retval 0  if the specified zipped file is not found.
int gar_stat(gar_t *G, const char *fname, gar_fstat_t *fstat, jmp_buf env) {
  gar_zstat_t zstat;
  if (gar_zstat(G, fname, &zstat, env)) {
    *fstat = zstat.fstat;
    fstat->fname = fname;
    return 1; // the file is found.
  } else {
    fstat->fname = NULL;
    fstat->fsize = 0;
    return 0; // the file is not found.
  }
}


struct gar_fdata {
  gar_gfile_t gf;
};


/// Open a zipped file's data stream.
static gar_fdata_t *open_fdata(gar_t *G, const gar_zstat_t *zstat,
                               jmp_buf _env) {
  jmp_buf env;
  gar_fdata_t *volatile fd = NULL;

  if (setjmp(env)) {
    gar_close(fd);
    longjmp(_env, 1);
  }

  // Allocate a new gar_fdata_t structure and initialize it.
  fd = _gar_malloc(sizeof(gar_fdata_t), env);
  gar_gfile_null(&fd->gf);

  // Open the zipped file's data stream.
  gar_gfile_dup(&G->gf, &fd->gf, env);
  gar_gfile_open_part(&fd->gf, zstat->data_off, zstat->data_len, env);

  if (zstat->comp_method == 8) {
    gar_inflate(&fd->gf, env);
  }

  return fd;
}


/// Open a zipped file's data stream.
/// @return a gar_fdata_t pointer, or NULL if the specified file is not found.
gar_fdata_t *gar_open(gar_t *G, const char *fname, jmp_buf env) {
  gar_zstat_t zstat;
  if (gar_zstat(G, fname, &zstat, env)) {
    return open_fdata(G, &zstat, env);
  } else {
    return NULL; // the file is not found.
  }
}


/// Read bytes from a zipped file's data stream.
/// @return number of the read bytes; this value can be less than the specified
/// if and only if there is no more byte to read (reached the EOF).
size_t gar_read(gar_fdata_t *fd, void *ptr, size_t n, jmp_buf env) {
  if (fd != NULL) {
    return gar_gfile_read(&fd->gf, ptr, n, env);
  } else {
    return 0; // emulating empty file.
  }
}


/// Close a zipped file's data stream.
void gar_close(gar_fdata_t *fd) {
  if (fd != NULL) {
    gar_gfile_close(&fd->gf);
    _gar_free(fd);
  }
}
