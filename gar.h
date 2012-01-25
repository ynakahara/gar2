#ifndef GAR_H_INCLUDED
#define GAR_H_INCLUDED

#include <stddef.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gar gar_t; ///< Archive file.
typedef struct gar_fstat gar_fstat_t; ///< Zipped file's status.
typedef struct gar_fdata gar_fdata_t; ///< Zipped file's data stream.
typedef struct gar_gfile gar_gfile_t; ///< Generalized file (see garlib.h).

struct gar_fstat {
  const char *fname;
  size_t fsize;
};

typedef int(*gar_enum_t)(const gar_fstat_t *fstat, void *ud, jmp_buf env);

gar_t *gar_archive_open_file(const char *fname, jmp_buf env);
gar_t *gar_archive_gopen(gar_gfile_t *gf, jmp_buf env);
void gar_archive_close(gar_t *G);
int gar_enum(gar_t *G, gar_enum_t fn, void *ud, jmp_buf env);
int gar_stat(gar_t *G, const char *fname, gar_fstat_t *fstat, jmp_buf env);
gar_fdata_t *gar_open(gar_t *G, const char *fname, jmp_buf env);
size_t gar_read(gar_fdata_t *fd, void *ptr, size_t n, jmp_buf env);
void gar_close(gar_fdata_t *fd);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
