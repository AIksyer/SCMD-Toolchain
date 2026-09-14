#ifndef SCMD_SEMA_H
#define SCMD_SEMA_H
#include <stdbool.h>
#include "scmd/ast.h"
bool scmd_sema_check(const char *path, ScmdProgram *program);
#endif
