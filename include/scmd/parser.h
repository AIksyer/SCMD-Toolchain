#ifndef SCMD_PARSER_H
#define SCMD_PARSER_H

#include <stdbool.h>
#include "scmd/ast.h"

bool scmd_parse(const char *path, const char *source, ScmdProgram *out_program);

#endif
