#ifndef SCMD_PROJECT_H
#define SCMD_PROJECT_H

#include <stdbool.h>
#include "scmd/codegen.h"

typedef struct ScmdProject {
    char *project_path;
    char *base_dir;
    char *name;
    char *entry;
    char *output_dir;
    char *package;
    bool bootstrap;
    ScmdCodegenOptions codegen;
} ScmdProject;

bool scmd_project_load(const char *path, ScmdProject *out_project);
bool scmd_project_build(const ScmdProject *project);
bool scmd_project_init(const char *directory, const char *name_override);
void scmd_project_dispose(ScmdProject *project);

#endif
