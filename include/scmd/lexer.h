#ifndef SCMD_LEXER_H
#define SCMD_LEXER_H

#include <stddef.h>

typedef enum ScmdTokenKind {
    TOK_EOF = 0,
    TOK_ERROR,
    TOK_IDENT,
    TOK_STRING,
    TOK_NUMBER,

    /* keywords */
    TOK_GET,
    TOK_VAR,
    TOK_BOOL,
    TOK_U8,
    TOK_FUNCTION,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_TRUE,
    TOK_FALSE,
    TOK_WAIT,
    TOK_BLOCK_KW,
    TOK_RECORD,
    TOK_JUMP,

    /* punctuation */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_BLOCK_OPEN,   /* </ */
    TOK_BLOCK_CLOSE,  /* /> */
    TOK_SEMI,
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,

    /* operators */
    TOK_ASSIGN,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_BANG,
    TOK_TILDE,
    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_SHL,
    TOK_SHR,
    TOK_AND_AND,
    TOK_OR_OR,
    TOK_EQ_EQ,
    TOK_BANG_EQ,
    TOK_PLUS_EQ,
    TOK_MINUS_EQ,
    TOK_STAR_EQ,
    TOK_SLASH_EQ,
    TOK_PERCENT_EQ,
    TOK_AMP_EQ,
    TOK_PIPE_EQ,
    TOK_CARET_EQ,
    TOK_SHL_EQ,
    TOK_SHR_EQ,

    /* project/time helpers */
    TOK_MS,
    TOK_S,
    TOK_TICK,
    TOK_TICKS
} ScmdTokenKind;

typedef struct ScmdToken {
    ScmdTokenKind kind;
    const char *start;
    size_t len;
    int line;
    int col;
} ScmdToken;

typedef struct ScmdLexer {
    const char *source;
    const char *cur;
    int line;
    int col;
} ScmdLexer;

void scmd_lexer_init(ScmdLexer *lx, const char *source);
ScmdToken scmd_lexer_next(ScmdLexer *lx);
const char *scmd_token_name(ScmdTokenKind kind);

#endif
