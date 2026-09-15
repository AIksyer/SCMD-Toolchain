#ifndef SCMD_COMPTIME_H
#define SCMD_COMPTIME_H

#include <stdbool.h>
#include "scmd/ast.h"

/* Resolve const declarations, size fixed arrays, execute top-level compile {}
 * blocks, and substitute const references into the runtime AST.  The compile
 * interpreter intentionally reuses ordinary SCMD if/while/for syntax; it has
 * no host shell/process escape hatch. */
bool scmd_comptime_run(const char *path, ScmdProgram *program);

#endif
