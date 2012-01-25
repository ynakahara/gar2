// gfilecrt.c : manipulate files via the C runtime.

#include "gar.h"
#include "garlib.h"
#include "garaux.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>


gar_t *gar_archive_open_file(const char *fname, jmp_buf _env) {
  jmp_buf env;
  gar_gfile_t gf;
  gar_gfile_null(&gf);

  if (setjmp(env)) {
    gar_gfile_close(&gf);
    longjmp(_env, 1);
  }

  // Open the specified file.
  gar_gfile_open_file(&gf, fname, env);

  // Open the file as an archive.
  return gar_archive_gopen(&gf, env);
}


typedef struct gfile_file_ud {
  FILE *fp;
  long fsize;
  char fname[1];
} gfile_file_ud_t;


static void _gar_perror(jmp_buf env, const char *pre) {
  _gar_error(env, pre, strerror(errno));
}


static size_t gfile_file_on_read(void *ud, void *ptr, size_t n, jmp_buf env) {
  gfile_file_ud_t *fud = (gfile_file_ud_t *)ud;
  size_t nread = fread(ptr, 1, n, fud->fp);
  if (nread < n && ferror(fud->fp)) { _gar_perror(env, fud->fname); }
  return nread;
}


static void gfile_file_on_seek(void *ud, gar_off_t off, jmp_buf env) {
  gfile_file_ud_t *fud = (gfile_file_ud_t *)ud;

  // Make sure that the specified seek offset is inside of the source file
  // data and that the given offset can be safely casted into (long).
  if (off > (unsigned long)fud->fsize) {
    _gar_error(env, fud->fname, "out-of-range seek offset");
  }

  if (fseek(fud->fp, (long)off, SEEK_SET)) { _gar_perror(env, fud->fname); }
}


static void gfile_file_on_dup(void *ud, gar_gfile_t *dst, jmp_buf env) {
  gfile_file_ud_t *fud = (gfile_file_ud_t *)ud;
  gar_gfile_open_file(dst, fud->fname, env);
}


static void gfile_file_on_close(void *ud) {
  gfile_file_ud_t *fud = (gfile_file_ud_t *)ud;
  if (fud->fp != NULL) fclose(fud->fp);
  _gar_free(fud);
}


static const gar_gfile_t c_gfile_file = {
  NULL,
  &gfile_file_on_read,
  &gfile_file_on_seek,
  &gfile_file_on_dup,
  &gfile_file_on_close,
};


static gfile_file_ud_t *gfile_file_on_open(const char *fname, jmp_buf _env) {
  jmp_buf env;
  gfile_file_ud_t *fud = NULL;

  if (setjmp(env)) {
    gfile_file_on_close(fud);
    longjmp(_env, 1);
  }

  fud = _gar_malloc(offsetof(gfile_file_ud_t, fname) + strlen(fname) + 1, env);
  fud->fp = NULL;
  fud->fsize = -1;
  strcpy(fud->fname, fname);

  if ((fud->fp = fopen(fname, "rb")) == NULL) { _gar_perror(env, fname); }

  if (fseek(fud->fp, 0, SEEK_END) ||
      (fud->fsize = ftell(fud->fp)) == -1 ||
      fseek(fud->fp, 0, SEEK_SET)) {
    _gar_perror(env, fname);
  }

  return fud;
}


/// Open the specified file.
void gar_gfile_open_file(gar_gfile_v *gf, const char *fname, jmp_buf env) {
  gf->ud = gfile_file_on_open(fname, env);
  gf->read = c_gfile_file.read;
  gf->seek = c_gfile_file.seek;
  gf->dup = c_gfile_file.dup;
  gf->close = c_gfile_file.close;
}
