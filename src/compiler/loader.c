#include "scmd/loader.h"
#include "scmd/common.h"
#include "scmd/parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct LoadCtx {
    ScmdProgram program;
    char **visited;
    size_t visited_len;
    size_t visited_cap;
    int errors;
} LoadCtx;

static int is_sep(char c) { return c == '/' || c == '\\'; }

static char *normalize_path(const char *path) {
    if (!path || !*path) return scmd_strdup(".");
    size_t n = strlen(path);
    char *tmp = scmd_strdup(path);
    if (!tmp) return NULL;
    for (size_t i = 0; i < n; ++i) if (tmp[i] == '\\') tmp[i] = '/';

    char **parts = (char **)calloc(n + 1, sizeof(char *));
    if (!parts) { free(tmp); return NULL; }
    size_t count = 0;
    char *p = tmp;
    char prefix[4] = {0};
    int absolute = 0;
    if (isalpha((unsigned char)p[0]) && p[1] == ':') {
        prefix[0] = p[0]; prefix[1] = ':'; p += 2;
        if (*p == '/') { absolute = 1; p++; }
    } else if (*p == '/') {
        absolute = 1; p++;
    }
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';
        if (strcmp(start, ".") == 0 || !*start) continue;
        if (strcmp(start, "..") == 0) {
            if (count && strcmp(parts[count - 1], "..") != 0) count--;
            else if (!absolute) parts[count++] = start;
        } else {
            parts[count++] = start;
        }
    }

    size_t outn = strlen(prefix) + (absolute ? 1 : 0) + 1;
    for (size_t i = 0; i < count; ++i) outn += strlen(parts[i]) + 1;
    char *out = (char *)malloc(outn);
    if (!out) { free(parts); free(tmp); return NULL; }
    out[0] = '\0';
    if (prefix[0]) strcat(out, prefix);
    if (absolute) strcat(out, "/");
    for (size_t i = 0; i < count; ++i) {
        if (out[0] && out[strlen(out) - 1] != '/') strcat(out, "/");
        strcat(out, parts[i]);
    }
    if (!out[0]) strcpy(out, ".");
    free(parts);
    free(tmp);
    return out;
}

static char *dirname_dup(const char *path) {
    const char *last = NULL;
    for (const char *p = path; *p; ++p) if (is_sep(*p)) last = p;
    if (!last) return scmd_strdup(".");
    if (last == path) return scmd_strdup("/");
    return scmd_strndup(path, (size_t)(last - path));
}

static int is_absolute(const char *path) {
    return path && path[0] && (path[0] == '/' || path[0] == '\\' ||
           (path[1] && isalpha((unsigned char)path[0]) && path[1] == ':'));
}

static char *resolve_import(const char *from, const char *import_path) {
    if (is_absolute(import_path)) return normalize_path(import_path);
    char *dir = dirname_dup(from);
    char *joined = scmd_format("%s/%s", dir, import_path);
    char *norm = normalize_path(joined);
    free(dir); free(joined);
    return norm;
}

static int visited_find(LoadCtx *ctx, const char *path) {
    for (size_t i = 0; i < ctx->visited_len; ++i)
        if (strcmp(ctx->visited[i], path) == 0) return (int)i;
    return -1;
}

static void visited_add(LoadCtx *ctx, char *path) {
    if (ctx->visited_len == ctx->visited_cap) {
        size_t nc = ctx->visited_cap ? ctx->visited_cap * 2 : 8;
        ctx->visited = (char **)realloc(ctx->visited, nc * sizeof(*ctx->visited));
        ctx->visited_cap = nc;
    }
    ctx->visited[ctx->visited_len++] = path;
}


static void append_constants(ScmdProgram *dst, ScmdConst *src) {
    if (!src) return;
    ScmdConst **tail = &dst->constants;
    while (*tail) tail = &(*tail)->next;
    *tail = src;
}

static void append_globals(ScmdProgram *dst, ScmdGlobal *src) {
    if (!src) return;
    ScmdGlobal **tail = &dst->globals;
    while (*tail) tail = &(*tail)->next;
    *tail = src;
}

static void append_functions(ScmdProgram *dst, ScmdFunction *src) {
    if (!src) return;
    ScmdFunction **tail = &dst->functions;
    while (*tail) tail = &(*tail)->next;
    *tail = src;
}

static void append_blocks(ScmdProgram *dst, ScmdBlock *src) {
    if (!src) return;
    ScmdBlock **tail = &dst->blocks;
    while (*tail) tail = &(*tail)->next;
    *tail = src;
}


static void append_compile_blocks(ScmdProgram *dst, ScmdCompileBlock *src) {
    if (!src) return;
    ScmdCompileBlock **tail = &dst->compile_blocks;
    while (*tail) tail = &(*tail)->next;
    *tail = src;
}

static bool load_one(LoadCtx *ctx, const char *path) {
    char *norm = normalize_path(path);
    if (!norm) return false;
    if (visited_find(ctx, norm) >= 0) { free(norm); return true; }
    visited_add(ctx, norm);

    size_t size = 0;
    char *source = scmd_read_file(norm, &size);
    (void)size;
    if (!source) {
        fprintf(stderr, "error: cannot read source '%s'\n", norm);
        ctx->errors++;
        return false;
    }

    ScmdProgram unit;
    if (!scmd_parse(norm, source, &unit)) {
        free(source);
        scmd_program_dispose(&unit);
        ctx->errors++;
        return false;
    }
    free(source);

    for (ScmdImport *im = unit.imports; im; im = im->next) {
        char *child = resolve_import(norm, im->path);
        if (!child) { ctx->errors++; continue; }
        if (!load_one(ctx, child)) { free(child); scmd_program_dispose(&unit); return false; }
        free(child);
    }

    append_constants(&ctx->program, unit.constants);
    append_globals(&ctx->program, unit.globals);
    append_functions(&ctx->program, unit.functions);
    append_blocks(&ctx->program, unit.blocks);
    append_compile_blocks(&ctx->program, unit.compile_blocks);
    unit.constants = NULL;
    unit.globals = NULL;
    unit.functions = NULL;
    unit.blocks = NULL;
    unit.compile_blocks = NULL;
    scmd_program_dispose(&unit); /* imports are loader-only metadata */
    return true;
}

bool scmd_load_program(const char *entry_path, ScmdProgram *out_program, ScmdSourceList *out_sources) {
    LoadCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (out_program) memset(out_program, 0, sizeof(*out_program));
    if (out_sources) memset(out_sources, 0, sizeof(*out_sources));
    if (!out_program || !load_one(&ctx, entry_path) || ctx.errors) {
        scmd_program_dispose(&ctx.program);
        for (size_t i = 0; i < ctx.visited_len; ++i) free(ctx.visited[i]);
        free(ctx.visited);
        return false;
    }
    *out_program = ctx.program;
    if (out_sources) {
        out_sources->items = ctx.visited;
        out_sources->count = ctx.visited_len;
    } else {
        for (size_t i = 0; i < ctx.visited_len; ++i) free(ctx.visited[i]);
        free(ctx.visited);
    }
    return true;
}

void scmd_source_list_dispose(ScmdSourceList *sources) {
    if (!sources) return;
    for (size_t i = 0; i < sources->count; ++i) free(sources->items[i]);
    free(sources->items);
    sources->items = NULL;
    sources->count = 0;
}
