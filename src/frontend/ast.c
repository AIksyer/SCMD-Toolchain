#include "scmd/ast.h"

#include <stdlib.h>
#include <string.h>

static void expr_dispose(ScmdExpr *expr) {
    if (!expr) return;
    switch (expr->kind) {
        case EXPR_BOOL:
        case EXPR_INT:
            break;
        case EXPR_IDENT:
            free(expr->as.name);
            break;
        case EXPR_INDEX:
            free(expr->as.index.name);
            expr_dispose(expr->as.index.index);
            break;
        case EXPR_CALL:
            free(expr->as.call.name);
            for (size_t i = 0; i < expr->as.call.arg_count; ++i) expr_dispose(expr->as.call.args[i]);
            free(expr->as.call.args);
            break;
        case EXPR_UNARY:
            expr_dispose(expr->as.unary.value);
            break;
        case EXPR_BINARY:
            expr_dispose(expr->as.binary.lhs);
            expr_dispose(expr->as.binary.rhs);
            break;
    }
    free(expr);
}

static void stmt_list_dispose(ScmdStmt *stmt) {
    while (stmt) {
        ScmdStmt *next = stmt->next;
        switch (stmt->kind) {
            case STMT_VAR_DECL:
                free(stmt->as.var_decl.name);
                expr_dispose(stmt->as.var_decl.init);
                break;
            case STMT_ASSIGN:
                free(stmt->as.assign.name);
                expr_dispose(stmt->as.assign.value);
                break;
            case STMT_ARRAY_ASSIGN:
                free(stmt->as.array_assign.name);
                expr_dispose(stmt->as.array_assign.index);
                expr_dispose(stmt->as.array_assign.value);
                break;
            case STMT_IF:
                expr_dispose(stmt->as.if_stmt.cond);
                stmt_list_dispose(stmt->as.if_stmt.then_block);
                stmt_list_dispose(stmt->as.if_stmt.else_block);
                break;
            case STMT_WHILE:
                expr_dispose(stmt->as.while_stmt.cond);
                stmt_list_dispose(stmt->as.while_stmt.body);
                break;
            case STMT_FOR:
                stmt_list_dispose(stmt->as.for_stmt.init);
                expr_dispose(stmt->as.for_stmt.cond);
                stmt_list_dispose(stmt->as.for_stmt.step);
                stmt_list_dispose(stmt->as.for_stmt.body);
                break;
            case STMT_BUILTIN:
                free(stmt->as.builtin.text);
                break;
            case STMT_WAIT:
                break;
            case STMT_RETURN:
                expr_dispose(stmt->as.return_stmt.value);
                break;
            case STMT_CALL:
                free(stmt->as.call.name);
                for (size_t i = 0; i < stmt->as.call.arg_count; ++i) expr_dispose(stmt->as.call.args[i]);
                free(stmt->as.call.args);
                break;
            case STMT_BLOCK_SCOPE:
                stmt_list_dispose(stmt->as.block_scope.first);
                break;
            case STMT_RECORD:
                free(stmt->as.record.name);
                break;
            case STMT_JUMP:
                free(stmt->as.jump.record_name);
                break;
            case STMT_BLOCK_CALL:
                free(stmt->as.block_call.block_name);
                free(stmt->as.block_call.record_a);
                free(stmt->as.block_call.record_b);
                break;
        }
        free(stmt);
        stmt = next;
    }
}

void scmd_program_dispose(ScmdProgram *program) {
    if (!program) return;

    ScmdImport *im = program->imports;
    while (im) {
        ScmdImport *next = im->next;
        free(im->path);
        free(im);
        im = next;
    }

    ScmdConst *c = program->constants;
    while (c) {
        ScmdConst *next = c->next;
        free(c->name);
        expr_dispose(c->value);
        free(c);
        c = next;
    }

    ScmdGlobal *g = program->globals;
    while (g) {
        ScmdGlobal *next = g->next;
        free(g->name);
        expr_dispose(g->init);
        expr_dispose(g->array_len_expr);
        free(g->array_init_values);
        free(g);
        g = next;
    }

    ScmdFunction *fn = program->functions;
    while (fn) {
        ScmdFunction *next = fn->next;
        free(fn->name);
        stmt_list_dispose(fn->body);
        free(fn);
        fn = next;
    }

    ScmdBlock *block = program->blocks;
    while (block) {
        ScmdBlock *next = block->next;
        free(block->name);
        stmt_list_dispose(block->body);
        free(block);
        block = next;
    }

    ScmdCompileBlock *cb = program->compile_blocks;
    while (cb) {
        ScmdCompileBlock *next = cb->next;
        stmt_list_dispose(cb->body);
        free(cb);
        cb = next;
    }

    memset(program, 0, sizeof(*program));
}
