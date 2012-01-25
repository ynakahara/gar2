// garalloc.c : allocate/reallocate/free memory blocks.

#include "garaux.h"
#include <stdlib.h>


static void on_out_of_memory(jmp_buf env) {
  _gar_error(env, NULL, "out of memory");
}


void *_gar_malloc(size_t n, jmp_buf env) {
  void *ptr = malloc(n);
  if (ptr == NULL) on_out_of_memory(env);
  return ptr;
}


void *_gar_realloc(void *p, size_t n, jmp_buf env) {
  void *newptr = realloc(p, n);
  if (newptr == NULL) on_out_of_memory(env);
  return newptr;
}


void _gar_free(void *p) {
  free(p);
}
