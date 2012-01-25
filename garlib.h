#ifndef GARLIB_H_INCLUDED
#define GARLIB_H_INCLUDED

#include "gar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long long gar_off_t;
typedef struct gar_gfile volatile gar_gfile_v;

struct gar_gfile {
  void *ud;
  size_t(*read)(void *ud, void *ptr, size_t n, jmp_buf env);
  void(*seek)(void *ud, gar_off_t off, jmp_buf env);
  void(*dup)(void *ud, gar_gfile_t *dst, jmp_buf env);
  void(*close)(void *ud);
};

void gar_gfile_null(gar_gfile_v *gf);
void gar_gfile_open_part(gar_gfile_v *gf, gar_off_t off, gar_off_t len,
                         jmp_buf env);
void gar_gfile_open_file(gar_gfile_v *gf, const char *fname, jmp_buf env);
size_t gar_gfile_read(const gar_gfile_t *gf, void *ptr, size_t n, jmp_buf env);
void gar_gfile_seek(const gar_gfile_t *gf, gar_off_t off, jmp_buf env);
void gar_gfile_dup(const gar_gfile_t *gf, gar_gfile_t *dst, jmp_buf env);
void gar_gfile_close(gar_gfile_v *gf);

void gar_inflate(gar_gfile_v *gf, jmp_buf env);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
