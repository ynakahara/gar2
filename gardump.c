// gardump : list/dump zipped files

#include "gar.h"
#include <stdio.h>


static int on_list(const gar_fstat_t *fstat, void *ud, jmp_buf env) {
  ((void)ud);
  ((void)env);
  printf("%s\n", fstat->fname);
  return 0; // continue enumeration.
}


static int dump_file(gar_t *G, const char *fname) {
  jmp_buf env;
  gar_fdata_t *volatile fd = NULL;
  unsigned char s[1024];
  size_t n;

  // Make sure to close the zipped file stream.
  if (setjmp(env)) {
    gar_close(fd);
    return 1;
  }

  // Open the specified zipped file.
  fd = gar_open(G, fname, env);
  if (fd == NULL) {
    fprintf(stderr, "%s: no such file\n", fname);
    longjmp(env, 1);
  }

  // Print all the zipped file data to stdout.
  while ((n = gar_read(fd, s, sizeof(s), env)) > 0) {
    fwrite(s, 1, n, stdout);
  }

  // Finally close the zipped file stream.
  gar_close(fd);

  return 0;
}


int main(int argc, char *argv[]) {
  gar_t *volatile G = NULL;
  jmp_buf env;
  int i;

  // If no argument is given, display the usage and exit in success.
  if (argc == 1) {
    fprintf(stderr, "synopsis: %s zip-file [zipped-files ...]\n", argv[0]);
    return 0;
  }

  // Make sure to close the zip archive.
  if (setjmp(env)) {
    gar_archive_close(G);
    return 1;
  }

  // Open the specified zip archive.
  G = gar_archive_open_file(argv[1], env);

  if (argc == 2) {
    // If only a zip file name is given, list all the zipped files.
    gar_enum(G, &on_list, NULL, env);
  } else {
    // Otherwise, print the data of the specified zipped file(s) to stdout.
    for (i = 2; i < argc; i++) {
      if (dump_file(G, argv[i])) { // returns nonzero at error.
        longjmp(env, 1);
      }
    }
  }

  // Close the archive.
  gar_archive_close(G);

  return 0;
}
