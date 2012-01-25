// garerror.c : display errors.

#include "garaux.h"
#include <stdio.h>


void _gar_error(jmp_buf env, const char *pre, const char *msg) {
  if (pre != NULL) {
    fprintf(stderr, "%s: %s\n", pre, msg);
  } else {
    fprintf(stderr, "%s\n", msg);
  }
  longjmp(env, 1);
}
