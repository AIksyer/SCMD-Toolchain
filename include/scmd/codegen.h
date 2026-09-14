#ifndef SCMD_CODEGEN_H
#define SCMD_CODEGEN_H

#include <stdbool.h>
#include <stddef.h>
#include "scmd/ast.h"

typedef enum ScmdConsoleMode {
    SCMD_CONSOLE_SYNC = 0,
    SCMD_CONSOLE_ASYNC = 1
} ScmdConsoleMode;

typedef struct ScmdCodegenOptions {
    ScmdConsoleMode console_mode;
    int console_settle_ms;
    int tick_ms;
    const char *exec_prefix;
    size_t page_bytes;
    size_t page_commands;
    bool organized_output;
    bool optimize;
} ScmdCodegenOptions;

bool scmd_codegen_cfg(const char *source_path, const ScmdProgram *program, const char *output_path);
bool scmd_codegen_cfg_ex(const char *source_path, const ScmdProgram *program, const char *output_path,
                         const ScmdCodegenOptions *options);

#endif
