#ifndef SCMD_AST_H
#define SCMD_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum ScmdTypeKind {
    SCMD_TYPE_UNKNOWN = 0,
    SCMD_TYPE_BOOL,
    SCMD_TYPE_U8
} ScmdTypeKind;

typedef enum ScmdExprKind {
    EXPR_BOOL,
    EXPR_INT,
    EXPR_IDENT,
    EXPR_CALL,
    EXPR_UNARY,
    EXPR_BINARY
} ScmdExprKind;

typedef enum ScmdUnaryOp {
    UNARY_NOT,
    UNARY_BIT_NOT,
    UNARY_NEG
} ScmdUnaryOp;

typedef enum ScmdBinaryOp {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_SHL, BIN_SHR,
    BIN_LT, BIN_LE, BIN_GT, BIN_GE,
    BIN_EQ, BIN_NEQ,
    BIN_BIT_AND, BIN_BIT_XOR, BIN_BIT_OR,
    BIN_LOGICAL_AND, BIN_LOGICAL_OR
} ScmdBinaryOp;

typedef struct ScmdExpr ScmdExpr;
struct ScmdExpr {
    ScmdExprKind kind;
    ScmdTypeKind inferred_type;
    int line;
    int col;
    union {
        bool boolean;
        uint64_t integer;
        char *name;
        struct {
            char *name;
            ScmdExpr **args;
            size_t arg_count;
        } call;
        struct { ScmdUnaryOp op; ScmdExpr *value; } unary;
        struct { ScmdBinaryOp op; ScmdExpr *lhs; ScmdExpr *rhs; } binary;
    } as;
};

typedef enum ScmdAssignOp {
    ASSIGN_SET,
    ASSIGN_ADD, ASSIGN_SUB, ASSIGN_MUL, ASSIGN_DIV, ASSIGN_MOD,
    ASSIGN_BIT_AND, ASSIGN_BIT_OR, ASSIGN_BIT_XOR,
    ASSIGN_SHL, ASSIGN_SHR
} ScmdAssignOp;

typedef enum ScmdBuiltinKind {
    BUILTIN_CONSOLE_PRINT,
    BUILTIN_CONSOLE_CLEAR,
    BUILTIN_CHAT_SEND,
    BUILTIN_TEAMCHAT_SEND,
    BUILTIN_COMMAND_EXEC
} ScmdBuiltinKind;

typedef enum ScmdBlockCallKind {
    BLOCK_CALL_RUN,
    BLOCK_CALL_JUMP,
    BLOCK_CALL_RUN_UNTIL,
    BLOCK_CALL_RUN_RANGE
} ScmdBlockCallKind;

typedef enum ScmdWaitUnit {
    WAIT_MS,
    WAIT_SECONDS,
    WAIT_TICKS
} ScmdWaitUnit;

typedef enum ScmdStmtKind {
    STMT_VAR_DECL,
    STMT_ASSIGN,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_BUILTIN,
    STMT_WAIT,
    STMT_RETURN,
    STMT_CALL,
    STMT_BLOCK_SCOPE,
    STMT_RECORD,
    STMT_JUMP,
    STMT_BLOCK_CALL
} ScmdStmtKind;

typedef struct ScmdStmt ScmdStmt;
struct ScmdStmt {
    ScmdStmtKind kind;
    int line;
    int col;
    ScmdStmt *next;
    union {
        struct {
            ScmdTypeKind declared_type; /* UNKNOWN means var */
            ScmdTypeKind resolved_type;
            char *name;
            ScmdExpr *init;
        } var_decl;
        struct { char *name; ScmdAssignOp op; ScmdExpr *value; } assign;
        struct { ScmdExpr *cond; ScmdStmt *then_block; ScmdStmt *else_block; } if_stmt;
        struct { ScmdExpr *cond; ScmdStmt *body; } while_stmt;
        struct { ScmdStmt *init; ScmdExpr *cond; ScmdStmt *step; ScmdStmt *body; } for_stmt;
        struct { ScmdBuiltinKind kind; char *text; } builtin;
        struct { uint64_t amount; ScmdWaitUnit unit; } wait_stmt;
        struct { ScmdExpr *value; } return_stmt;
        struct { char *name; ScmdExpr **args; size_t arg_count; } call;
        struct { ScmdStmt *first; } block_scope;
        struct { char *name; } record;
        struct { char *record_name; } jump;
        struct {
            char *block_name;
            ScmdBlockCallKind kind;
            char *record_a;
            char *record_b;
        } block_call;
    } as;
};

typedef struct ScmdImport {
    char *path;
    int line;
    int col;
    struct ScmdImport *next;
} ScmdImport;

typedef struct ScmdGlobal {
    ScmdTypeKind declared_type;
    ScmdTypeKind resolved_type;
    char *name;
    ScmdExpr *init;
    int line;
    int col;
    struct ScmdGlobal *next;
} ScmdGlobal;

typedef struct ScmdFunction {
    char *name;
    ScmdStmt *body;
    int line;
    int col;
    struct ScmdFunction *next;
} ScmdFunction;

typedef struct ScmdBlock {
    char *name;
    ScmdStmt *body;
    int line;
    int col;
    struct ScmdBlock *next;
} ScmdBlock;

typedef struct ScmdProgram {
    ScmdImport *imports;
    ScmdGlobal *globals;
    ScmdFunction *functions;
    ScmdBlock *blocks;
} ScmdProgram;

void scmd_program_dispose(ScmdProgram *program);

#endif
