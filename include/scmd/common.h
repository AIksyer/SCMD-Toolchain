#ifndef SCMD_COMMON_H
#define SCMD_COMMON_H

#include <stdbool.h>
#include <stddef.h>

char *scmd_read_file(const char *path, size_t *size_out);
char *scmd_strdup(const char *s);
char *scmd_strndup(const char *s, size_t n);
char *scmd_format(const char *fmt, ...);
void scmd_error_at(const char *path, int line, int col, const char *fmt, ...);

#endif
