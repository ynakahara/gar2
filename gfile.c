// gfile.c : manipulation of generalized files.

#include "garlib.h"
#include "garaux.h"

//-----------------------------------------------------------------------------
// Auxiliary

/// Raise error if the specified @a off is out of the range [0, @a len].
void check_off(gar_off_t off, gar_off_t len, jmp_buf env) {
  if (off > len) {
    _gar_error(env, NULL, "out-of-range seek offset");
  }
}


//-----------------------------------------------------------------------------
// Null Stream

static size_t gfile_null_on_read(void *ud, void *ptr, size_t n, jmp_buf env) {
  ((void)ud);
  ((void)ptr);
  ((void)n);
  ((void)env);
  return 0; // emulating empty file.
}


static void gfile_null_on_seek(void *ud, gar_off_t off, jmp_buf env) {
  ((void)ud);
  ((void)off);
  check_off(off, 0, env);
}


static void gfile_null_on_dup(void *ud, gar_gfile_t *dst, jmp_buf env) {
  ((void)ud);
  ((void)env);
  gar_gfile_null(dst);
}


static void gfile_null_on_close(void *ud) {
  ((void)ud);
}


static const gar_gfile_t c_gfile_null = {
  NULL,
  &gfile_null_on_read,
  &gfile_null_on_seek,
  &gfile_null_on_dup,
  &gfile_null_on_close,
};


/// Open a null stream.
/// Null stream emulates an empty file, and does not leak any resource even if
/// it is not closed.
void gar_gfile_null(gar_gfile_v *gf) {
  gf->ud = NULL;
  gf->read = c_gfile_null.read;
  gf->seek = c_gfile_null.seek;
  gf->dup = c_gfile_null.dup;
  gf->close = c_gfile_null.close;
}


//-----------------------------------------------------------------------------
// Partial Stream

typedef struct gfile_part_ud {
  gar_gfile_t gf;
  gar_off_t pos;
  gar_off_t off;
  gar_off_t len;
} gfile_part_ud_t;


static gar_off_t offmin(gar_off_t x, gar_off_t y) {
  return (x < y) ? x : y;
}


static size_t gfile_part_on_read(void *ud, void *ptr, size_t n, jmp_buf env) {
  gfile_part_ud_t *pud = (gfile_part_ud_t *)ud;
  size_t m = (size_t)offmin(n, pud->len - pud->pos);
  size_t nread = gar_gfile_read(&pud->gf, ptr, m, env);
  pud->pos += nread;
  return nread;
}


static void gfile_part_on_seek(void *ud, gar_off_t off, jmp_buf env) {
  gfile_part_ud_t *pud = (gfile_part_ud_t *)ud;
  check_off(off, pud->len, env);
  gar_gfile_seek(&pud->gf, pud->off + off, env);
  pud->pos = off;
}


static void gfile_part_on_dup(void *ud, gar_gfile_t *dst, jmp_buf _env) {
  gfile_part_ud_t *pud = (gfile_part_ud_t *)ud;
  jmp_buf env;
  gar_gfile_t gf_dup;
  gar_gfile_null(&gf_dup);

  if (setjmp(env)) {
    gar_gfile_close(&gf_dup);
    longjmp(_env, 1);
  }

  // Duplicate the source stream.
  gar_gfile_dup(&pud->gf, &gf_dup, env);

  // Open a new partial stream from the duplicated source stream.
  gar_gfile_open_part(&gf_dup, pud->off, pud->len, env);

  // Store the new stream.
  *dst = gf_dup;
  gar_gfile_null(&gf_dup);
}


static void gfile_part_on_close(void *ud) {
  gfile_part_ud_t *pud = (gfile_part_ud_t *)ud;
  gar_gfile_close(&pud->gf);
  _gar_free(pud);
}


static gfile_part_ud_t *gfile_part_on_open(gar_gfile_v *src, gar_off_t off,
                                           gar_off_t len, jmp_buf _env) {
  jmp_buf env;
  gfile_part_ud_t *volatile pud = NULL;

  if (setjmp(env)) {
    gfile_part_on_close(pud);
    longjmp(_env, 1);
  }

  // Initialize the seek offset and make sure that the given offset is in the
  // range.
  src->seek(src->ud, off, env);

  pud = _gar_malloc(sizeof(gfile_part_ud_t), env);
  pud->gf = *src;
  pud->pos = 0;
  pud->off = off;
  pud->len = len;
  gar_gfile_null(src); // get the ownership.

  return pud;
}


static const gar_gfile_t c_gfile_part = {
  NULL,
  &gfile_part_on_read,
  &gfile_part_on_seek,
  &gfile_part_on_dup,
  &gfile_part_on_close,
};


/// Open a part of the given stream as a new stream.
void gar_gfile_open_part(gar_gfile_v *gf, gar_off_t off, gar_off_t len,
                         jmp_buf env) {
  gf->ud = gfile_part_on_open(gf, off, len, env);
  gf->read = c_gfile_part.read;
  gf->seek = c_gfile_part.seek;
  gf->dup = c_gfile_part.dup;
  gf->close = c_gfile_part.close;
}


//-----------------------------------------------------------------------------
// Methods

size_t gar_gfile_read(const gar_gfile_t *gf, void *ptr, size_t n, jmp_buf env) {
  return gf->read(gf->ud, ptr, n, env);
}


void gar_gfile_seek(const gar_gfile_t *gf, gar_off_t off, jmp_buf env) {
  gf->seek(gf->ud, off, env);
}


void gar_gfile_dup(const gar_gfile_t *gf, gar_gfile_t *dst, jmp_buf env) {
  gf->dup(gf->ud, dst, env);
}


void gar_gfile_close(gar_gfile_v *gf) {
  gf->close(gf->ud);
  gar_gfile_null(gf); // set the closed stream to null.
}
