#ifndef GARAUX_H_INCLUDED
#define GARAUX_H_INCLUDED

#include "gar.h"
#include "garlib.h"

#ifdef __cplusplus
extern "C" {
#endif

void _gar_error(jmp_buf env, const char *pre, const char *msg)
  __attribute__((noreturn));

void *_gar_malloc(size_t n, jmp_buf env) __attribute__((malloc));
void *_gar_realloc(void *p, size_t n, jmp_buf env);
void _gar_free(void *p);

void _gar_setup_gfile(gar_gfile_t *gf, const gar_gfile_t *fn, void *ud);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
