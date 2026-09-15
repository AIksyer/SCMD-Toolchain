#include "scmd/codegen.h"
#include "scmd/comptime.h"
#include "scmd/bytecode.h"
#include "scmd/common.h"
#include "scmd/loader.h"
#include "scmd/project.h"
#include "scmd/sema.h"
#include "scmd/version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
    printf("scmdc %s\n", SCMD_VERSION);
    printf("\nProject workflow:\n");
    printf("  %s init <directory> [--name NAME]\n", argv0);
    printf("  %s build <project.scmdproj> [--no-opt]\n", argv0);
    printf("\nBytecode workflow:\n");
    printf("  %s pack <cfg-root> -o output.scb [--profile NAME]\n", argv0);
    printf("\nSingle-file workflow:\n");
    printf("  %s <input.scmd> [-o output.cfg] [--console-mode sync|async]\n", argv0);
    printf("     [--console-settle-ms N] [--tick-ms N] [--exec-prefix PATH]\n");
    printf("     [--page-bytes N] [--page-commands N] [--no-opt]\n");
    printf("\nSimulation is provided by the separate scmdsim tool.\n");
    printf("\nOther:\n");
    printf("  %s --help\n", argv0);
    printf("  %s --version\n", argv0);
}

static char *default_output_path(const char *input) {
    const char *slash1 = strrchr(input, '/');
    const char *slash2 = strrchr(input, '\\');
    const char *last = NULL;
    if (slash1 && slash2) last = slash1 > slash2 ? slash1 : slash2;
    else last = slash1 ? slash1 : slash2;
    const char *base = last ? last + 1 : input;
    size_t dir_len = (size_t)(base - input);
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - base) : strlen(base);
    char *out = (char *)malloc(dir_len + stem_len + 5);
    if (!out) return NULL;
    memcpy(out, input, dir_len);
    memcpy(out + dir_len, base, stem_len);
    memcpy(out + dir_len + stem_len, ".cfg", 5);
    return out;
}

static int compile_single(int argc, char **argv) {
    const char *input = argv[1];
    const char *output_arg = NULL;
    ScmdCodegenOptions cg_opts = {SCMD_CONSOLE_ASYNC, 16, 16, NULL, 4096, 40, false, true};
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: -o requires a path\n"); return 2; }
            output_arg = argv[i];
        } else if (strcmp(argv[i], "--console-mode") == 0 || strcmp(argv[i], "--render-mode") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --console-mode requires sync or async\n"); return 2; }
            if (strcmp(argv[i], "sync") == 0) cg_opts.console_mode = SCMD_CONSOLE_SYNC;
            else if (strcmp(argv[i], "async") == 0) cg_opts.console_mode = SCMD_CONSOLE_ASYNC;
            else { fprintf(stderr, "error: invalid console mode '%s'\n", argv[i]); return 2; }
        } else if (strcmp(argv[i], "--console-settle-ms") == 0 || strcmp(argv[i], "--render-delay-ms") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --console-settle-ms requires a number\n"); return 2; }
            char *end = NULL;
            long v = strtol(argv[i], &end, 10);
            if (!end || *end != '\0' || v < 0 || v > 60000) {
                fprintf(stderr, "error: invalid console settle delay '%s' (expected 0..60000 ms)\n", argv[i]); return 2;
            }
            cg_opts.console_settle_ms = (int)v;
        } else if (strcmp(argv[i], "--tick-ms") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --tick-ms requires a number\n"); return 2; }
            char *end = NULL;
            long v = strtol(argv[i], &end, 10);
            if (!end || *end != '\0' || v <= 0 || v > 60000) { fprintf(stderr, "error: invalid tick size '%s'\n", argv[i]); return 2; }
            cg_opts.tick_ms = (int)v;
        } else if (strcmp(argv[i], "--exec-prefix") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --exec-prefix requires a path\n"); return 2; }
            cg_opts.exec_prefix = argv[i];
        } else if (strcmp(argv[i], "--page-bytes") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --page-bytes requires a number\n"); return 2; }
            char *end = NULL;
            unsigned long v = strtoul(argv[i], &end, 10);
            if (!end || *end != '\0' || v < 512 || v > 65536) {
                fprintf(stderr, "error: invalid page byte limit '%s' (expected 512..65536)\n", argv[i]); return 2;
            }
            cg_opts.page_bytes = (size_t)v;
        } else if (strcmp(argv[i], "--page-commands") == 0) {
            if (++i >= argc) { fprintf(stderr, "error: --page-commands requires a number\n"); return 2; }
            char *end = NULL;
            unsigned long v = strtoul(argv[i], &end, 10);
            if (!end || *end != '\0' || v < 4 || v > 512) {
                fprintf(stderr, "error: invalid page command limit '%s' (expected 4..512)\n", argv[i]); return 2;
            }
            cg_opts.page_commands = (size_t)v;
        } else if (strcmp(argv[i], "--no-opt") == 0) {
            cg_opts.optimize = false;
        } else {
            fprintf(stderr, "error: unknown argument '%s'\n", argv[i]);
            return 2;
        }
    }

    ScmdProgram program;
    ScmdSourceList sources = {0};
    if (!scmd_load_program(input, &program, &sources)) return 1;
    int rc = 1;
    if (!scmd_comptime_run(input, &program)) goto cleanup_program;
    if (!scmd_sema_check(input, &program)) goto cleanup_program;

    char *owned_output = NULL;
    const char *output = output_arg;
    if (!output) {
        owned_output = default_output_path(input);
        if (!owned_output) { fprintf(stderr, "error: out of memory\n"); goto cleanup_program; }
        output = owned_output;
    }

    if (!scmd_codegen_cfg_ex(input, &program, output, &cg_opts)) {
        free(owned_output);
        goto cleanup_program;
    }

    printf("scmdc: compiled %zu source file(s): %s -> %s\n", sources.count, input, output);
    free(owned_output);
    rc = 0;

cleanup_program:
    scmd_source_list_dispose(&sources);
    scmd_program_dispose(&program);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) { usage(argv[0]); return 0; }
    if (strcmp(argv[1], "--version") == 0) { puts(SCMD_VERSION); return 0; }

    if (strcmp(argv[1], "init") == 0) {
        if (argc < 3) { fprintf(stderr, "error: init requires a directory\n"); return 2; }
        const char *name = NULL;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) name = argv[++i];
            else { fprintf(stderr, "error: unknown init argument '%s'\n", argv[i]); return 2; }
        }
        return scmd_project_init(argv[2], name) ? 0 : 1;
    }

    if (strcmp(argv[1], "pack") == 0) {
        if (argc < 3) { fprintf(stderr, "error: pack requires a CFG root\n"); return 2; }
        const char *root = argv[2];
        const char *output = NULL;
        const char *profile = SCMD_CS2_PROFILE;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "-o") == 0) {
                if (++i >= argc) { fprintf(stderr, "error: -o requires a .scb path\n"); return 2; }
                output = argv[i];
            } else if (strcmp(argv[i], "--profile") == 0) {
                if (++i >= argc) { fprintf(stderr, "error: --profile requires a name\n"); return 2; }
                profile = argv[i];
            } else {
                fprintf(stderr, "error: unknown pack argument '%s'\n", argv[i]);
                return 2;
            }
        }
        if (!output) { fprintf(stderr, "error: pack requires -o output.scb\n"); return 2; }
        if (strcmp(profile, SCMD_CS2_PROFILE) != 0) {
            fprintf(stderr, "error: unsupported compatibility profile '%s' (supported: %s)\n", profile, SCMD_CS2_PROFILE);
            return 2;
        }
        return scmd_bytecode_pack_cfg_root(root, output, profile) ? 0 : 1;
    }

    if (strcmp(argv[1], "build") == 0) {
        if (argc < 3 || argc > 4 || (argc == 4 && strcmp(argv[3], "--no-opt") != 0)) {
            fprintf(stderr, "error: build expects <project.scmdproj> [--no-opt]\n"); return 2;
        }
        ScmdProject project;
        if (!scmd_project_load(argv[2], &project)) return 1;
        if (argc == 4) project.codegen.optimize = false;
        int ok = scmd_project_build(&project) ? 0 : 1;
        scmd_project_dispose(&project);
        return ok;
    }

    return compile_single(argc, argv);
}
