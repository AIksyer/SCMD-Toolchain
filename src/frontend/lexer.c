#include "scmd/lexer.h"
#include "scmd/keywords.h"

#include <ctype.h>
#include <string.h>

static ScmdToken tok(ScmdTokenKind kind, const char *start, size_t len, int line, int col) {
    ScmdToken t = {kind, start, len, line, col};
    return t;
}

void scmd_lexer_init(ScmdLexer *lx, const char *source) {
    lx->source = source;
    lx->cur = source;
    lx->line = 1;
    lx->col = 1;
}

static unsigned char peek(ScmdLexer *lx) { return (unsigned char)*lx->cur; }
static unsigned char peek2(ScmdLexer *lx) { return (unsigned char)lx->cur[1]; }
static unsigned char peek3(ScmdLexer *lx) { return (unsigned char)lx->cur[2]; }

static int utf8_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static unsigned char advance(ScmdLexer *lx) {
    unsigned char c = (unsigned char)*lx->cur;
    if (!c) return 0;
    if (c == '\n') {
        lx->cur++;
        lx->line++;
        lx->col = 1;
        return c;
    }
    int n = utf8_len(c);
    for (int i = 0; i < n && lx->cur[i]; ++i) {
        if (i > 0 && (((unsigned char)lx->cur[i] & 0xC0) != 0x80)) { n = i; break; }
    }
    lx->cur += n;
    lx->col++;
    return c;
}

static int ident_start(unsigned char c) {
    return c == '_' || isalpha(c) || c >= 0x80;
}

static int ident_continue(unsigned char c) {
    return ident_start(c) || isdigit(c);
}

static void skip_ws_and_comments(ScmdLexer *lx) {
    for (;;) {
        unsigned char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
            continue;
        }
        if (c == '/' && peek2(lx) == '/') {
            while (peek(lx) && peek(lx) != '\n') advance(lx);
            continue;
        }
        if (c == '/' && peek2(lx) == '*') {
            advance(lx); advance(lx);
            while (peek(lx) && !(peek(lx) == '*' && peek2(lx) == '/')) advance(lx);
            if (peek(lx)) { advance(lx); advance(lx); }
            continue;
        }
        break;
    }
}

ScmdToken scmd_lexer_next(ScmdLexer *lx) {
    skip_ws_and_comments(lx);
    const char *start = lx->cur;
    int line = lx->line, col = lx->col;
    unsigned char c = peek(lx);
    if (!c) return tok(TOK_EOF, start, 0, line, col);

    if (ident_start(c)) {
        advance(lx);
        while (ident_continue(peek(lx))) advance(lx);
        size_t n = (size_t)(lx->cur - start);
        return tok(scmd_keyword_lookup(start, n), start, n, line, col);
    }

    if (isdigit(c)) {
        advance(lx);
        if (c == '0' && (peek(lx) == 'x' || peek(lx) == 'X')) {
            advance(lx);
            while (isxdigit(peek(lx)) || peek(lx) == '_') advance(lx);
        } else if (c == '0' && (peek(lx) == 'b' || peek(lx) == 'B')) {
            advance(lx);
            while (peek(lx) == '0' || peek(lx) == '1' || peek(lx) == '_') advance(lx);
        } else {
            while (isdigit(peek(lx)) || peek(lx) == '_') advance(lx);
        }
        return tok(TOK_NUMBER, start, (size_t)(lx->cur - start), line, col);
    }

    if (c == '"') {
        advance(lx);
        const char *body = lx->cur;
        while (peek(lx) && peek(lx) != '"') {
            if (peek(lx) == '\\' && peek2(lx)) {
                advance(lx);
                advance(lx);
            } else {
                advance(lx);
            }
        }
        if (!peek(lx)) return tok(TOK_ERROR, start, (size_t)(lx->cur - start), line, col);
        size_t n = (size_t)(lx->cur - body);
        advance(lx);
        return tok(TOK_STRING, body, n, line, col);
    }

    /* Multi-character delimiters/operators first. */
    if (c == '<' && peek2(lx) == '/') { advance(lx); advance(lx); return tok(TOK_BLOCK_OPEN, start, 2, line, col); }
    if (c == '/' && peek2(lx) == '>') { advance(lx); advance(lx); return tok(TOK_BLOCK_CLOSE, start, 2, line, col); }
    if (c == '<' && peek2(lx) == '<' && peek3(lx) == '=') { advance(lx); advance(lx); advance(lx); return tok(TOK_SHL_EQ, start, 3, line, col); }
    if (c == '>' && peek2(lx) == '>' && peek3(lx) == '=') { advance(lx); advance(lx); advance(lx); return tok(TOK_SHR_EQ, start, 3, line, col); }
    if (c == '<' && peek2(lx) == '<') { advance(lx); advance(lx); return tok(TOK_SHL, start, 2, line, col); }
    if (c == '>' && peek2(lx) == '>') { advance(lx); advance(lx); return tok(TOK_SHR, start, 2, line, col); }
    if (c == '&' && peek2(lx) == '&') { advance(lx); advance(lx); return tok(TOK_AND_AND, start, 2, line, col); }
    if (c == '|' && peek2(lx) == '|') { advance(lx); advance(lx); return tok(TOK_OR_OR, start, 2, line, col); }
    if (c == '=' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_EQ_EQ, start, 2, line, col); }
    if (c == '!' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_BANG_EQ, start, 2, line, col); }
    if (c == '<' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_LE, start, 2, line, col); }
    if (c == '>' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_GE, start, 2, line, col); }
    if (c == '+' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_PLUS_EQ, start, 2, line, col); }
    if (c == '-' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_MINUS_EQ, start, 2, line, col); }
    if (c == '*' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_STAR_EQ, start, 2, line, col); }
    if (c == '/' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_SLASH_EQ, start, 2, line, col); }
    if (c == '%' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_PERCENT_EQ, start, 2, line, col); }
    if (c == '&' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_AMP_EQ, start, 2, line, col); }
    if (c == '|' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_PIPE_EQ, start, 2, line, col); }
    if (c == '^' && peek2(lx) == '=') { advance(lx); advance(lx); return tok(TOK_CARET_EQ, start, 2, line, col); }

    advance(lx);
    switch (c) {
        case '(': return tok(TOK_LPAREN, start, 1, line, col);
        case ')': return tok(TOK_RPAREN, start, 1, line, col);
        case '{': return tok(TOK_LBRACE, start, 1, line, col);
        case '}': return tok(TOK_RBRACE, start, 1, line, col);
        case ';': return tok(TOK_SEMI, start, 1, line, col);
        case ',': return tok(TOK_COMMA, start, 1, line, col);
        case '.': return tok(TOK_DOT, start, 1, line, col);
        case ':': return tok(TOK_COLON, start, 1, line, col);
        case '=': return tok(TOK_ASSIGN, start, 1, line, col);
        case '+': return tok(TOK_PLUS, start, 1, line, col);
        case '-': return tok(TOK_MINUS, start, 1, line, col);
        case '*': return tok(TOK_STAR, start, 1, line, col);
        case '/': return tok(TOK_SLASH, start, 1, line, col);
        case '%': return tok(TOK_PERCENT, start, 1, line, col);
        case '!': return tok(TOK_BANG, start, 1, line, col);
        case '~': return tok(TOK_TILDE, start, 1, line, col);
        case '&': return tok(TOK_AMP, start, 1, line, col);
        case '|': return tok(TOK_PIPE, start, 1, line, col);
        case '^': return tok(TOK_CARET, start, 1, line, col);
        case '<': return tok(TOK_LT, start, 1, line, col);
        case '>': return tok(TOK_GT, start, 1, line, col);
        default: break;
    }
    return tok(TOK_ERROR, start, 1, line, col);
}

const char *scmd_token_name(ScmdTokenKind kind) {
    switch (kind) {
        case TOK_EOF: return "end of file"; case TOK_ERROR: return "invalid token";
        case TOK_IDENT: return "identifier"; case TOK_STRING: return "string"; case TOK_NUMBER: return "number";
        case TOK_GET: return "get"; case TOK_VAR: return "var"; case TOK_BOOL: return "bool"; case TOK_U8: return "u8";
        case TOK_FUNCTION: return "function"; case TOK_IF: return "if"; case TOK_ELSE: return "else"; case TOK_WHILE: return "while";
        case TOK_FOR: return "for"; case TOK_RETURN: return "return"; case TOK_TRUE: return "true"; case TOK_FALSE: return "false";
        case TOK_WAIT: return "wait"; case TOK_BLOCK_KW: return "block"; case TOK_RECORD: return "record"; case TOK_JUMP: return "jump";
        case TOK_LPAREN: return "("; case TOK_RPAREN: return ")"; case TOK_LBRACE: return "{"; case TOK_RBRACE: return "}";
        case TOK_BLOCK_OPEN: return "</"; case TOK_BLOCK_CLOSE: return "/>"; case TOK_SEMI: return ";"; case TOK_COMMA: return ",";
        case TOK_DOT: return "."; case TOK_COLON: return ":"; case TOK_ASSIGN: return "="; case TOK_PLUS: return "+"; case TOK_MINUS: return "-";
        case TOK_STAR: return "*"; case TOK_SLASH: return "/"; case TOK_PERCENT: return "%"; case TOK_BANG: return "!"; case TOK_TILDE: return "~";
        case TOK_AMP: return "&"; case TOK_PIPE: return "|"; case TOK_CARET: return "^"; case TOK_LT: return "<"; case TOK_LE: return "<=";
        case TOK_GT: return ">"; case TOK_GE: return ">="; case TOK_SHL: return "<<"; case TOK_SHR: return ">>"; case TOK_AND_AND: return "&&";
        case TOK_OR_OR: return "||"; case TOK_EQ_EQ: return "=="; case TOK_BANG_EQ: return "!="; case TOK_PLUS_EQ: return "+=";
        case TOK_MINUS_EQ: return "-="; case TOK_STAR_EQ: return "*="; case TOK_SLASH_EQ: return "/="; case TOK_PERCENT_EQ: return "%=";
        case TOK_AMP_EQ: return "&="; case TOK_PIPE_EQ: return "|="; case TOK_CARET_EQ: return "^="; case TOK_SHL_EQ: return "<<="; case TOK_SHR_EQ: return ">>=";
        case TOK_MS: return "ms"; case TOK_S: return "s"; case TOK_TICK: return "tick"; case TOK_TICKS: return "ticks";
    }
    return "?";
}
