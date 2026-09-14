#ifndef SCMD_LOADER_H
#define SCMD_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include "scmd/ast.h"

typedef struct ScmdSourceList {
    char **items;
    size_t count;
} ScmdSourceList;

bool scmd_load_program(const char *entry_path, ScmdProgram *out_program, ScmdSourceList *out_sources);
void scmd_source_list_dispose(ScmdSourceList *sources);

#endif
