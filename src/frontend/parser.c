#include "scmd/parser.h"
#include "scmd/common.h"
#include "scmd/lexer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Parser {
    const char *path;
    ScmdLexer lx;
    ScmdToken cur;
    ScmdToken prev;
    int errors;
    int block_depth;
} Parser;

static void next(Parser *p) {
    p->prev = p->cur;
    p->cur = scmd_lexer_next(&p->lx);
    if (p->cur.kind == TOK_ERROR) {
        scmd_error_at(p->path, p->cur.line, p->cur.col, "invalid token");
        p->errors++;
    }
}

static bool accept(Parser *p, ScmdTokenKind k) {
    if (p->cur.kind != k) return false;
    next(p);
    return true;
}

static bool expect(Parser *p, ScmdTokenKind k, const char *what) {
    if (p->cur.kind == k) { next(p); return true; }
    scmd_error_at(p->path, p->cur.line, p->cur.col, "expected %s, got %s", what, scmd_token_name(p->cur.kind));
    p->errors++;
    return false;
}

static char *token_dup(ScmdToken t) { return scmd_strndup(t.start, t.len); }

static char *string_dup_unescape(ScmdToken t) {
    char *out = (char *)malloc(t.len + 1);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < t.len; ++i) {
        char c = t.start[i];
        if (c == '\\' && i + 1 < t.len) {
            char n = t.start[++i];
            switch (n) {
                case 'n': out[w++] = '\n'; break;
                case 'r': out[w++] = '\r'; break;
                case 't': out[w++] = '\t'; break;
                case '\\': out[w++] = '\\'; break;
                case '"': out[w++] = '"'; break;
                default: out[w++] = n; break;
            }
        } else out[w++] = c;
    }
    out[w] = '\0';
    return out;
}

static uint64_t parse_number_token(Parser *p, ScmdToken t) {
    char *raw = token_dup(t);
    if (!raw) return 0;
    size_t n = strlen(raw), w = 0;
    for (size_t i = 0; i < n; ++i) if (raw[i] != '_') raw[w++] = raw[i];
    raw[w] = '\0';
    int base = 10;
    const char *start = raw;
    if (w > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X')) { base = 16; start += 2; }
    else if (w > 2 && raw[0] == '0' && (raw[1] == 'b' || raw[1] == 'B')) {
        uint64_t v = 0;
        for (const char *q = raw + 2; *q; ++q) {
            if (*q != '0' && *q != '1') {
                scmd_error_at(p->path, t.line, t.col, "invalid binary integer literal");
                p->errors++;
                free(raw);
                return 0;
            }
            v = (v << 1u) | (uint64_t)(*q - '0');
        }
        free(raw);
        return v;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(start, &end, base);
    if (errno || !end || *end) {
        scmd_error_at(p->path, t.line, t.col, "invalid integer literal");
        p->errors++;
        v = 0;
    }
    free(raw);
    return (uint64_t)v;
}

static ScmdExpr *new_expr(ScmdExprKind k, int line, int col) {
    ScmdExpr *e = (ScmdExpr *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = k; e->line = line; e->col = col; e->inferred_type = SCMD_TYPE_UNKNOWN;
    return e;
}

static ScmdStmt *new_stmt(ScmdStmtKind k, int line, int col) {
    ScmdStmt *s = (ScmdStmt *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->kind = k; s->line = line; s->col = col;
    return s;
}

static ScmdExpr *parse_expr(Parser *p);
static ScmdStmt *parse_stmt(Parser *p);

static void expr_args_push(ScmdExpr *call, ScmdExpr *arg) {
    size_t n = call->as.call.arg_count;
    call->as.call.args = (ScmdExpr **)realloc(call->as.call.args, (n + 1u) * sizeof(*call->as.call.args));
    call->as.call.args[n] = arg;
    call->as.call.arg_count = n + 1u;
}

static ScmdExpr *parse_primary(Parser *p) {
    ScmdToken t = p->cur;
    if (accept(p, TOK_TRUE)) {
        ScmdExpr *e = new_expr(EXPR_BOOL, t.line, t.col); if (e) e->as.boolean = true; return e;
    }
    if (accept(p, TOK_FALSE)) {
        ScmdExpr *e = new_expr(EXPR_BOOL, t.line, t.col); if (e) e->as.boolean = false; return e;
    }
    if (accept(p, TOK_NUMBER)) {
        ScmdExpr *e = new_expr(EXPR_INT, t.line, t.col); if (e) e->as.integer = parse_number_token(p, t); return e;
    }
    if (accept(p, TOK_IDENT)) {
        char *name = token_dup(t);
        if (accept(p, TOK_LPAREN)) {
            ScmdExpr *e = new_expr(EXPR_CALL, t.line, t.col);
            if (e) e->as.call.name = name;
            if (p->cur.kind != TOK_RPAREN) {
                do { expr_args_push(e, parse_expr(p)); } while (accept(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "')'");
            return e;
        }
        if (accept(p, TOK_LBRACKET)) {
            ScmdExpr *idx = parse_expr(p);
            expect(p, TOK_RBRACKET, "']'");
            ScmdExpr *e = new_expr(EXPR_INDEX, t.line, t.col);
            if (e) { e->as.index.name = name; e->as.index.index = idx; }
            return e;
        }
        ScmdExpr *e = new_expr(EXPR_IDENT, t.line, t.col); if (e) e->as.name = name; return e;
    }
    if (accept(p, TOK_LPAREN)) {
        ScmdExpr *e = parse_expr(p);
        expect(p, TOK_RPAREN, "')'");
        return e;
    }
    scmd_error_at(p->path, t.line, t.col, "expected expression, got %s", scmd_token_name(t.kind));
    p->errors++;
    if (p->cur.kind != TOK_EOF) next(p);
    ScmdExpr *e = new_expr(EXPR_BOOL, t.line, t.col); if (e) e->as.boolean = false; return e;
}

static ScmdExpr *make_unary(ScmdUnaryOp op, ScmdToken t, ScmdExpr *value) {
    ScmdExpr *e = new_expr(EXPR_UNARY, t.line, t.col);
    if (e) { e->as.unary.op = op; e->as.unary.value = value; }
    return e;
}
static ScmdExpr *make_binary(ScmdBinaryOp op, ScmdToken t, ScmdExpr *lhs, ScmdExpr *rhs) {
    ScmdExpr *e = new_expr(EXPR_BINARY, t.line, t.col);
    if (e) { e->as.binary.op = op; e->as.binary.lhs = lhs; e->as.binary.rhs = rhs; }
    return e;
}

static ScmdExpr *parse_unary(Parser *p) {
    ScmdToken t = p->cur;
    if (accept(p, TOK_BANG)) return make_unary(UNARY_NOT, t, parse_unary(p));
    if (accept(p, TOK_TILDE)) return make_unary(UNARY_BIT_NOT, t, parse_unary(p));
    if (accept(p, TOK_MINUS)) return make_unary(UNARY_NEG, t, parse_unary(p));
    return parse_primary(p);
}

#define BIN_PARSE(name, lower, K1, OP1) \
static ScmdExpr *name(Parser *p) { \
    ScmdExpr *lhs = lower(p); \
    while (p->cur.kind == K1) { ScmdToken t=p->cur; next(p); lhs=make_binary(OP1,t,lhs,lower(p)); } \
    return lhs; \
}

static ScmdExpr *parse_mul(Parser *p) {
    ScmdExpr *lhs = parse_unary(p);
    while (p->cur.kind == TOK_STAR || p->cur.kind == TOK_SLASH || p->cur.kind == TOK_PERCENT) {
        ScmdToken t = p->cur; next(p); ScmdBinaryOp op = t.kind==TOK_STAR?BIN_MUL:(t.kind==TOK_SLASH?BIN_DIV:BIN_MOD);
        lhs = make_binary(op, t, lhs, parse_unary(p));
    }
    return lhs;
}
static ScmdExpr *parse_add(Parser *p) {
    ScmdExpr *lhs = parse_mul(p);
    while (p->cur.kind == TOK_PLUS || p->cur.kind == TOK_MINUS) {
        ScmdToken t=p->cur; next(p); lhs=make_binary(t.kind==TOK_PLUS?BIN_ADD:BIN_SUB,t,lhs,parse_mul(p));
    }
    return lhs;
}
static ScmdExpr *parse_shift(Parser *p) {
    ScmdExpr *lhs = parse_add(p);
    while (p->cur.kind == TOK_SHL || p->cur.kind == TOK_SHR) {
        ScmdToken t=p->cur; next(p); lhs=make_binary(t.kind==TOK_SHL?BIN_SHL:BIN_SHR,t,lhs,parse_add(p));
    }
    return lhs;
}
static ScmdExpr *parse_rel(Parser *p) {
    ScmdExpr *lhs = parse_shift(p);
    while (p->cur.kind==TOK_LT || p->cur.kind==TOK_LE || p->cur.kind==TOK_GT || p->cur.kind==TOK_GE) {
        ScmdToken t=p->cur; next(p); ScmdBinaryOp op = BIN_LT;
        if(t.kind==TOK_LE) op=BIN_LE; else if(t.kind==TOK_GT) op=BIN_GT; else if(t.kind==TOK_GE) op=BIN_GE;
        lhs=make_binary(op,t,lhs,parse_shift(p));
    }
    return lhs;
}
static ScmdExpr *parse_eq(Parser *p) {
    ScmdExpr *lhs=parse_rel(p);
    while(p->cur.kind==TOK_EQ_EQ || p->cur.kind==TOK_BANG_EQ) { ScmdToken t=p->cur; next(p); lhs=make_binary(t.kind==TOK_EQ_EQ?BIN_EQ:BIN_NEQ,t,lhs,parse_rel(p)); }
    return lhs;
}
BIN_PARSE(parse_bit_and, parse_eq, TOK_AMP, BIN_BIT_AND)
BIN_PARSE(parse_bit_xor, parse_bit_and, TOK_CARET, BIN_BIT_XOR)
BIN_PARSE(parse_bit_or, parse_bit_xor, TOK_PIPE, BIN_BIT_OR)
BIN_PARSE(parse_logical_and, parse_bit_or, TOK_AND_AND, BIN_LOGICAL_AND)
BIN_PARSE(parse_logical_or, parse_logical_and, TOK_OR_OR, BIN_LOGICAL_OR)
#undef BIN_PARSE

static ScmdExpr *parse_expr(Parser *p) { return parse_logical_or(p); }

static ScmdStmt *parse_brace_block(Parser *p) {
    ScmdToken open = p->cur;
    if (!expect(p, TOK_LBRACE, "'{'") ) return new_stmt(STMT_BLOCK_SCOPE, open.line, open.col);
    ScmdStmt *block = new_stmt(STMT_BLOCK_SCOPE, open.line, open.col);
    ScmdStmt **tail = &block->as.block_scope.first;
    while (p->cur.kind != TOK_RBRACE && p->cur.kind != TOK_EOF) {
        ScmdStmt *s = parse_stmt(p);
        if (!s) break;
        *tail = s;
        while (*tail) tail = &(*tail)->next;
    }
    expect(p, TOK_RBRACE, "'}'");
    return block;
}

static ScmdTypeKind token_type(ScmdTokenKind k) {
    if (k == TOK_BOOL) return SCMD_TYPE_BOOL;
    if (k == TOK_U8) return SCMD_TYPE_U8;
    return SCMD_TYPE_UNKNOWN;
}

static ScmdStmt *parse_var_decl(Parser *p, bool expect_semi, bool is_volatile, bool noopt) {
    ScmdToken kw = p->cur;
    ScmdTypeKind declared = SCMD_TYPE_UNKNOWN;
    if (p->cur.kind == TOK_BOOL || p->cur.kind == TOK_U8) declared = token_type(p->cur.kind);
    next(p);
    ScmdToken name = p->cur;
    expect(p, TOK_IDENT, "identifier");
    expect(p, TOK_ASSIGN, "'='");
    ScmdExpr *init = parse_expr(p);
    if (expect_semi) expect(p, TOK_SEMI, "';'");
    ScmdStmt *s = new_stmt(STMT_VAR_DECL, kw.line, kw.col);
    s->as.var_decl.declared_type = declared;
    s->as.var_decl.resolved_type = SCMD_TYPE_UNKNOWN;
    s->as.var_decl.name = token_dup(name);
    s->as.var_decl.init = init;
    s->as.var_decl.is_volatile = is_volatile;
    s->as.var_decl.noopt = noopt;
    return s;
}

static ScmdAssignOp assign_op(ScmdTokenKind k) {
    switch(k) {
        case TOK_PLUS_EQ: return ASSIGN_ADD; case TOK_MINUS_EQ: return ASSIGN_SUB; case TOK_STAR_EQ: return ASSIGN_MUL;
        case TOK_SLASH_EQ: return ASSIGN_DIV; case TOK_PERCENT_EQ: return ASSIGN_MOD; case TOK_AMP_EQ: return ASSIGN_BIT_AND;
        case TOK_PIPE_EQ: return ASSIGN_BIT_OR; case TOK_CARET_EQ: return ASSIGN_BIT_XOR; case TOK_SHL_EQ: return ASSIGN_SHL;
        case TOK_SHR_EQ: return ASSIGN_SHR; default: return ASSIGN_SET;
    }
}

static bool is_assign_token(ScmdTokenKind k) {
    return k==TOK_ASSIGN || k==TOK_PLUS_EQ || k==TOK_MINUS_EQ || k==TOK_STAR_EQ || k==TOK_SLASH_EQ || k==TOK_PERCENT_EQ ||
           k==TOK_AMP_EQ || k==TOK_PIPE_EQ || k==TOK_CARET_EQ || k==TOK_SHL_EQ || k==TOK_SHR_EQ;
}

static ScmdStmt *parse_assign_after_name(Parser *p, ScmdToken name, bool expect_semi) {
    ScmdToken op = p->cur; next(p);
    ScmdStmt *s = new_stmt(STMT_ASSIGN, name.line, name.col);
    s->as.assign.name = token_dup(name);
    s->as.assign.op = assign_op(op.kind);
    s->as.assign.value = parse_expr(p);
    if (expect_semi) expect(p, TOK_SEMI, "';'");
    return s;
}

static ScmdStmt *parse_array_assign_after_name(Parser *p, ScmdToken name, bool expect_semi) {
    expect(p, TOK_LBRACKET, "'['");
    ScmdExpr *index = parse_expr(p);
    expect(p, TOK_RBRACKET, "']'");
    ScmdToken op = p->cur;
    if (!is_assign_token(op.kind)) {
        scmd_error_at(p->path, p->cur.line, p->cur.col, "expected assignment operator after array index");
        p->errors++;
    } else next(p);
    ScmdStmt *s = new_stmt(STMT_ARRAY_ASSIGN, name.line, name.col);
    s->as.array_assign.name = token_dup(name);
    s->as.array_assign.index = index;
    s->as.array_assign.op = assign_op(op.kind);
    s->as.array_assign.value = parse_expr(p);
    if (expect_semi) expect(p, TOK_SEMI, "';'");
    return s;
}

static ScmdStmt *parse_if_stmt(Parser *p) {
    ScmdToken kw=p->cur; next(p);
    expect(p, TOK_LPAREN, "'(' after if");
    ScmdExpr *cond=parse_expr(p);
    expect(p, TOK_RPAREN, "')' after if condition");
    ScmdStmt *then_block=parse_brace_block(p);
    ScmdStmt *else_block=NULL;
    if (accept(p,TOK_ELSE)) {
        if (p->cur.kind == TOK_IF) {
            ScmdStmt *wrapper=new_stmt(STMT_BLOCK_SCOPE,p->cur.line,p->cur.col);
            wrapper->as.block_scope.first=parse_if_stmt(p);
            else_block=wrapper;
        } else else_block=parse_brace_block(p);
    }
    ScmdStmt *s=new_stmt(STMT_IF,kw.line,kw.col);
    s->as.if_stmt.cond=cond; s->as.if_stmt.then_block=then_block; s->as.if_stmt.else_block=else_block;
    return s;
}

static ScmdStmt *parse_while_stmt(Parser *p) {
    ScmdToken kw=p->cur; next(p);
    expect(p,TOK_LPAREN,"'(' after while");
    ScmdExpr *cond=parse_expr(p);
    expect(p,TOK_RPAREN,"')' after while condition");
    ScmdStmt *body=parse_brace_block(p);
    ScmdStmt *s=new_stmt(STMT_WHILE,kw.line,kw.col); s->as.while_stmt.cond=cond; s->as.while_stmt.body=body; return s;
}

static ScmdStmt *parse_for_part(Parser *p) {
    if (p->cur.kind==TOK_VAR || p->cur.kind==TOK_BOOL || p->cur.kind==TOK_U8) return parse_var_decl(p,false,false,false);
    if (p->cur.kind==TOK_IDENT) {
        ScmdToken name=p->cur; next(p);
        if (is_assign_token(p->cur.kind)) return parse_assign_after_name(p,name,false);
        if (p->cur.kind==TOK_LBRACKET) return parse_array_assign_after_name(p,name,false);
        scmd_error_at(p->path,name.line,name.col,"for initializer/step must be a declaration or assignment"); p->errors++;
    }
    return NULL;
}

static ScmdStmt *parse_for_stmt(Parser *p) {
    ScmdToken kw=p->cur; next(p);
    expect(p,TOK_LPAREN,"'(' after for");
    ScmdStmt *init=NULL,*step=NULL; ScmdExpr *cond=NULL;
    if (p->cur.kind!=TOK_SEMI) init=parse_for_part(p);
    expect(p,TOK_SEMI,"';' after for initializer");
    if (p->cur.kind!=TOK_SEMI) cond=parse_expr(p);
    expect(p,TOK_SEMI,"';' after for condition");
    if (p->cur.kind!=TOK_RPAREN) step=parse_for_part(p);
    expect(p,TOK_RPAREN,"')' after for clause");
    ScmdStmt *body=parse_brace_block(p);
    ScmdStmt *s=new_stmt(STMT_FOR,kw.line,kw.col); s->as.for_stmt.init=init; s->as.for_stmt.cond=cond; s->as.for_stmt.step=step; s->as.for_stmt.body=body; return s;
}

static char *parse_string_argument(Parser *p, const char *api) {
    expect(p,TOK_LPAREN,"'('");
    ScmdToken str=p->cur;
    if (!expect(p,TOK_STRING,"string literal")) return scmd_strdup("");
    expect(p,TOK_RPAREN,"')'");
    expect(p,TOK_SEMI,"';'");
    (void)api;
    return string_dup_unescape(str);
}

static ScmdStmt *parse_qualified_stmt(Parser *p, ScmdToken base) {
    expect(p,TOK_DOT,"'.'");
    ScmdToken member=p->cur;
    if (p->cur.kind != TOK_IDENT && p->cur.kind != TOK_JUMP) {
        scmd_error_at(p->path,p->cur.line,p->cur.col,"expected member name, got %s",scmd_token_name(p->cur.kind));
        p->errors++;
        return new_stmt(STMT_BLOCK_SCOPE,base.line,base.col);
    }
    next(p);
    char *base_name=token_dup(base), *member_name=token_dup(member);

    if (strcmp(base_name,"console")==0 && strcmp(member_name,"clear")==0) {
        expect(p,TOK_LPAREN,"'('"); expect(p,TOK_RPAREN,"')'"); expect(p,TOK_SEMI,"';'");
        ScmdStmt *s=new_stmt(STMT_BUILTIN,base.line,base.col); s->as.builtin.kind=BUILTIN_CONSOLE_CLEAR;
        free(base_name); free(member_name); return s;
    }
    ScmdBuiltinKind bk; bool builtin=false;
    if(strcmp(base_name,"console")==0 && strcmp(member_name,"print")==0){bk=BUILTIN_CONSOLE_PRINT;builtin=true;}
    else if(strcmp(base_name,"chat")==0 && strcmp(member_name,"send")==0){bk=BUILTIN_CHAT_SEND;builtin=true;}
    else if(strcmp(base_name,"teamchat")==0 && strcmp(member_name,"send")==0){bk=BUILTIN_TEAMCHAT_SEND;builtin=true;}
    else if(strcmp(base_name,"command")==0 && strcmp(member_name,"exec")==0){bk=BUILTIN_COMMAND_EXEC;builtin=true;}
    if(builtin){
        ScmdStmt *s=new_stmt(STMT_BUILTIN,base.line,base.col); s->as.builtin.kind=bk; s->as.builtin.text=parse_string_argument(p,member_name);
        free(base_name); free(member_name); return s;
    }

    ScmdStmt *s=new_stmt(STMT_BLOCK_CALL,base.line,base.col);
    s->as.block_call.block_name=base_name;
    if(strcmp(member_name,"run")==0) s->as.block_call.kind=BLOCK_CALL_RUN;
    else if(strcmp(member_name,"jump")==0) s->as.block_call.kind=BLOCK_CALL_JUMP;
    else if(strcmp(member_name,"runUntil")==0) s->as.block_call.kind=BLOCK_CALL_RUN_UNTIL;
    else if(strcmp(member_name,"runRange")==0) s->as.block_call.kind=BLOCK_CALL_RUN_RANGE;
    else {
        scmd_error_at(p->path,member.line,member.col,"unknown member '%s.%s'",base_name,member_name); p->errors++;
        s->as.block_call.kind=BLOCK_CALL_RUN;
    }
    free(member_name);
    expect(p,TOK_LPAREN,"'('");
    if(s->as.block_call.kind!=BLOCK_CALL_RUN){
        ScmdToken q1=p->cur;
        if(expect(p,TOK_IDENT,"record name")){
            char *first=token_dup(q1);
            if(accept(p,TOK_DOT)){
                ScmdToken rec=p->cur; expect(p,TOK_IDENT,"record name"); free(first); first=token_dup(rec);
            }
            s->as.block_call.record_a=first;
        }
        if(s->as.block_call.kind==BLOCK_CALL_RUN_RANGE){
            expect(p,TOK_COMMA,"','");
            ScmdToken q2=p->cur;
            if(expect(p,TOK_IDENT,"record name")){
                char *second=token_dup(q2);
                if(accept(p,TOK_DOT)){
                    ScmdToken rec=p->cur; expect(p,TOK_IDENT,"record name"); free(second); second=token_dup(rec);
                }
                s->as.block_call.record_b=second;
            }
        }
    }
    expect(p,TOK_RPAREN,"')'"); expect(p,TOK_SEMI,"';'");
    return s;
}

static ScmdStmt *parse_call_stmt_after_name(Parser *p, ScmdToken name) {
    ScmdStmt *s=new_stmt(STMT_CALL,name.line,name.col); s->as.call.name=token_dup(name);
    expect(p,TOK_LPAREN,"'('");
    while(p->cur.kind!=TOK_RPAREN && p->cur.kind!=TOK_EOF){
        ScmdExpr *a=parse_expr(p); size_t n=s->as.call.arg_count;
        s->as.call.args=(ScmdExpr**)realloc(s->as.call.args,(n+1u)*sizeof(*s->as.call.args)); s->as.call.args[n]=a; s->as.call.arg_count=n+1u;
        if(!accept(p,TOK_COMMA)) break;
    }
    expect(p,TOK_RPAREN,"')'"); expect(p,TOK_SEMI,"';'"); return s;
}

static ScmdStmt *parse_stmt(Parser *p) {
    if(p->cur.kind==TOK_VOLATILE) {
        ScmdToken v=p->cur; next(p);
        if(p->cur.kind==TOK_VAR || p->cur.kind==TOK_BOOL || p->cur.kind==TOK_U8) return parse_var_decl(p,true,true,false);
        scmd_error_at(p->path,v.line,v.col,"volatile currently applies to variable declarations"); p->errors++;
    }
    if(p->cur.kind==TOK_VAR || p->cur.kind==TOK_BOOL || p->cur.kind==TOK_U8) return parse_var_decl(p,true,false,false);
    if(p->cur.kind==TOK_IF) return parse_if_stmt(p);
    if(p->cur.kind==TOK_WHILE) return parse_while_stmt(p);
    if(p->cur.kind==TOK_FOR) return parse_for_stmt(p);
    if(p->cur.kind==TOK_WAIT){
        ScmdToken kw=p->cur; next(p); ScmdToken num=p->cur; expect(p,TOK_NUMBER,"duration number");
        uint64_t amount=parse_number_token(p,num); ScmdWaitUnit unit=WAIT_MS;
        if(accept(p,TOK_MS)) unit=WAIT_MS; else if(accept(p,TOK_S)) unit=WAIT_SECONDS; else if(accept(p,TOK_TICK)||accept(p,TOK_TICKS)) unit=WAIT_TICKS;
        else { scmd_error_at(p->path,p->cur.line,p->cur.col,"expected time unit ms, s, tick, or ticks"); p->errors++; }
        expect(p,TOK_SEMI,"';'"); ScmdStmt *s=new_stmt(STMT_WAIT,kw.line,kw.col); s->as.wait_stmt.amount=amount; s->as.wait_stmt.unit=unit; return s;
    }
    if(p->cur.kind==TOK_RETURN){
        ScmdToken t=p->cur; next(p); ScmdStmt *s=new_stmt(STMT_RETURN,t.line,t.col);
        if(p->cur.kind!=TOK_SEMI) s->as.return_stmt.value=parse_expr(p);
        expect(p,TOK_SEMI,"';'"); return s;
    }
    if(p->cur.kind==TOK_RECORD){
        ScmdToken t=p->cur; next(p); ScmdToken name=p->cur; expect(p,TOK_IDENT,"record name"); expect(p,TOK_SEMI,"';'");
        ScmdStmt *s=new_stmt(STMT_RECORD,t.line,t.col); s->as.record.name=token_dup(name); return s;
    }
    if(p->cur.kind==TOK_JUMP){
        ScmdToken t=p->cur; next(p); ScmdToken name=p->cur; expect(p,TOK_IDENT,"record name"); expect(p,TOK_SEMI,"';'");
        ScmdStmt *s=new_stmt(STMT_JUMP,t.line,t.col); s->as.jump.record_name=token_dup(name); return s;
    }
    if(p->cur.kind==TOK_LBRACE) return parse_brace_block(p);
    if(p->cur.kind==TOK_IDENT){
        ScmdToken name=p->cur; next(p);
        if(is_assign_token(p->cur.kind)) return parse_assign_after_name(p,name,true);
        if(p->cur.kind==TOK_LBRACKET) return parse_array_assign_after_name(p,name,true);
        if(p->cur.kind==TOK_LPAREN) return parse_call_stmt_after_name(p,name);
        if(p->cur.kind==TOK_DOT) return parse_qualified_stmt(p,name);
        scmd_error_at(p->path,name.line,name.col,"expected assignment, call, or qualified call after identifier"); p->errors++;
        while(p->cur.kind!=TOK_SEMI && p->cur.kind!=TOK_EOF && p->cur.kind!=TOK_RBRACE && p->cur.kind!=TOK_BLOCK_CLOSE) next(p);
        accept(p,TOK_SEMI); return new_stmt(STMT_BLOCK_SCOPE,name.line,name.col);
    }
    scmd_error_at(p->path,p->cur.line,p->cur.col,"unexpected token %s in statement",scmd_token_name(p->cur.kind)); p->errors++;
    if(p->cur.kind!=TOK_EOF) next(p); return new_stmt(STMT_BLOCK_SCOPE,p->cur.line,p->cur.col);
}

static bool parse_get(Parser *p, ScmdProgram *prog) {
    ScmdToken kw=p->cur; next(p); ScmdToken str=p->cur;
    if(!expect(p,TOK_STRING,"source path string")) return false; expect(p,TOK_SEMI,"';'");
    ScmdImport *im=(ScmdImport*)calloc(1,sizeof(*im)); if(!im) return false;
    im->path=string_dup_unescape(str); im->line=kw.line; im->col=kw.col;
    ScmdImport **tail=&prog->imports; while(*tail) tail=&(*tail)->next; *tail=im; return true;
}

static bool parse_const(Parser *p, ScmdProgram *prog) {
    ScmdToken kw=p->cur; next(p);
    ScmdToken name=p->cur; if(!expect(p,TOK_IDENT,"constant name")) return false;
    expect(p,TOK_ASSIGN,"'='");
    ScmdExpr *value=parse_expr(p); expect(p,TOK_SEMI,"';'");
    ScmdConst *c=(ScmdConst*)calloc(1,sizeof(*c)); if(!c) return false;
    c->name=token_dup(name); c->value=value; c->line=kw.line; c->col=kw.col;
    ScmdConst **tail=&prog->constants; while(*tail) tail=&(*tail)->next; *tail=c; return true;
}

static bool parse_global(Parser *p, ScmdProgram *prog, bool is_volatile, bool noopt) {
    ScmdToken kw=p->cur; ScmdTypeKind declared=SCMD_TYPE_UNKNOWN;
    if(p->cur.kind==TOK_BOOL||p->cur.kind==TOK_U8) declared=token_type(p->cur.kind); next(p);
    ScmdToken name=p->cur; if(!expect(p,TOK_IDENT,"identifier")) return false;
    ScmdExpr *array_len_expr=NULL; bool is_array=false;
    if(accept(p,TOK_LBRACKET)) { is_array=true; array_len_expr=parse_expr(p); expect(p,TOK_RBRACKET,"']'"); }
    expect(p,TOK_ASSIGN,"'='");
    ScmdExpr *init=parse_expr(p); expect(p,TOK_SEMI,"';'");
    ScmdGlobal *g=(ScmdGlobal*)calloc(1,sizeof(*g)); if(!g) return false;
    g->declared_type=declared; g->resolved_type=SCMD_TYPE_UNKNOWN; g->name=token_dup(name); g->init=init;
    g->is_volatile=is_volatile; g->noopt=noopt; g->is_array=is_array; g->array_len_expr=array_len_expr;
    g->line=kw.line; g->col=kw.col;
    ScmdGlobal **tail=&prog->globals; while(*tail) tail=&(*tail)->next; *tail=g; return true;
}

static bool parse_function(Parser *p, ScmdProgram *prog, bool exported, bool resident, bool noopt) {
    ScmdToken kw=p->cur; next(p); ScmdToken name=p->cur; if(!expect(p,TOK_IDENT,"function name")) return false;
    expect(p,TOK_LPAREN,"'('");
    if(p->cur.kind!=TOK_RPAREN){ scmd_error_at(p->path,p->cur.line,p->cur.col,"function parameters are reserved but not implemented yet"); p->errors++; while(p->cur.kind!=TOK_RPAREN&&p->cur.kind!=TOK_EOF) next(p); }
    expect(p,TOK_RPAREN,"')'"); ScmdStmt *body=parse_brace_block(p);
    ScmdFunction *f=(ScmdFunction*)calloc(1,sizeof(*f)); if(!f) return false;
    f->name=token_dup(name); f->exported=exported; f->resident=resident; f->noopt=noopt; f->line=kw.line; f->col=kw.col; f->body=body?body->as.block_scope.first:NULL;
    if(body){body->as.block_scope.first=NULL;free(body);}
    ScmdFunction **tail=&prog->functions; while(*tail) tail=&(*tail)->next; *tail=f; return true;
}

static bool parse_compile(Parser *p, ScmdProgram *prog) {
    ScmdToken kw=p->cur; next(p);
    ScmdStmt *body=parse_brace_block(p);
    ScmdCompileBlock *cb=(ScmdCompileBlock*)calloc(1,sizeof(*cb)); if(!cb) return false;
    cb->line=kw.line; cb->col=kw.col; cb->body=body?body->as.block_scope.first:NULL;
    if(body){body->as.block_scope.first=NULL;free(body);}
    ScmdCompileBlock **tail=&prog->compile_blocks; while(*tail) tail=&(*tail)->next; *tail=cb; return true;
}

static bool parse_named_block(Parser *p, ScmdProgram *prog) {
    ScmdToken kw=p->cur; next(p); ScmdToken name=p->cur; if(!expect(p,TOK_IDENT,"block name")) return false;
    expect(p,TOK_BLOCK_OPEN,"'</'"); p->block_depth++;
    ScmdStmt *first=NULL, **tail=&first;
    while(p->cur.kind!=TOK_BLOCK_CLOSE && p->cur.kind!=TOK_EOF){ ScmdStmt *s=parse_stmt(p); if(!s) break; *tail=s; while(*tail) tail=&(*tail)->next; }
    p->block_depth--; expect(p,TOK_BLOCK_CLOSE,"'/>'");
    ScmdBlock *b=(ScmdBlock*)calloc(1,sizeof(*b)); if(!b) return false; b->name=token_dup(name); b->line=kw.line; b->col=kw.col; b->body=first;
    ScmdBlock **bt=&prog->blocks; while(*bt) bt=&(*bt)->next; *bt=b; return true;
}

bool scmd_parse(const char *path,const char *source,ScmdProgram *out_program){
    memset(out_program,0,sizeof(*out_program)); Parser p={0}; p.path=path; scmd_lexer_init(&p.lx,source); p.cur=scmd_lexer_next(&p.lx);
    while(p.cur.kind!=TOK_EOF){
        bool attr_noopt=false,attr_export=false,attr_resident=false;
        while(p.cur.kind==TOK_AT){
            ScmdToken at=p.cur; next(&p); ScmdToken a=p.cur;
            if(a.kind!=TOK_IDENT && a.kind!=TOK_EXPORT && a.kind!=TOK_RESIDENT){scmd_error_at(path,a.line,a.col,"expected attribute name after @");p.errors++;break;}
            char *name=token_dup(a); next(&p);
            if(strcmp(name,"noopt")==0)attr_noopt=true;
            else if(strcmp(name,"export")==0)attr_export=true;
            else if(strcmp(name,"resident")==0)attr_resident=true;
            else {scmd_error_at(path,at.line,at.col,"unknown attribute '@%s'",name);p.errors++;}
            free(name);
        }
        if(p.cur.kind==TOK_GET){ if(attr_noopt||attr_export||attr_resident){scmd_error_at(path,p.cur.line,p.cur.col,"attributes do not apply to get");p.errors++;} if(!parse_get(&p,out_program)){ while(p.cur.kind!=TOK_SEMI&&p.cur.kind!=TOK_EOF) next(&p); accept(&p,TOK_SEMI);} }
        else if(p.cur.kind==TOK_CONST){ if(attr_noopt||attr_export||attr_resident){scmd_error_at(path,p.cur.line,p.cur.col,"attributes do not apply to const");p.errors++;} parse_const(&p,out_program); }
        else if(p.cur.kind==TOK_COMPILE){ if(attr_noopt||attr_export||attr_resident){scmd_error_at(path,p.cur.line,p.cur.col,"attributes do not apply to compile blocks");p.errors++;} parse_compile(&p,out_program); }
        else if(p.cur.kind==TOK_VOLATILE){
            next(&p);
            if(p.cur.kind==TOK_VAR||p.cur.kind==TOK_BOOL||p.cur.kind==TOK_U8) parse_global(&p,out_program,true,attr_noopt);
            else {scmd_error_at(path,p.cur.line,p.cur.col,"volatile currently applies to global var/bool/u8 declarations");p.errors++;next(&p);}
        }
        else if(p.cur.kind==TOK_VAR||p.cur.kind==TOK_BOOL||p.cur.kind==TOK_U8){ if(attr_export||attr_resident){scmd_error_at(path,p.cur.line,p.cur.col,"@export/@resident apply to functions");p.errors++;} parse_global(&p,out_program,false,attr_noopt); }
        else if(p.cur.kind==TOK_FUNCTION) parse_function(&p,out_program,attr_export,attr_resident,attr_noopt);
        else if(p.cur.kind==TOK_EXPORT){
            ScmdToken ex=p.cur; next(&p); bool resident=attr_resident;
            if(p.cur.kind==TOK_RESIDENT){resident=true;next(&p);}
            if(p.cur.kind!=TOK_FUNCTION){scmd_error_at(path,ex.line,ex.col,"export currently requires function or resident function");p.errors++;}
            else parse_function(&p,out_program,true,resident,attr_noopt);
        }
        else if(p.cur.kind==TOK_RESIDENT){
            ScmdToken rs=p.cur; next(&p);
            if(p.cur.kind!=TOK_FUNCTION){scmd_error_at(path,rs.line,rs.col,"resident currently requires function");p.errors++;}
            else parse_function(&p,out_program,attr_export,true,attr_noopt);
        }
        else if(p.cur.kind==TOK_BLOCK_KW){ if(attr_noopt||attr_export||attr_resident){scmd_error_at(path,p.cur.line,p.cur.col,"attributes do not apply to named blocks yet");p.errors++;} parse_named_block(&p,out_program); }
        else { scmd_error_at(path,p.cur.line,p.cur.col,"top level accepts get, const, compile, var/bool/u8 arrays, volatile declarations, function, export/resident function, and block"); p.errors++; next(&p); }
    }
    return p.errors==0;
}
