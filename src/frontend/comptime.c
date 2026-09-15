#include "scmd/comptime.h"
#include "scmd/common.h"

#include <stdlib.h>
#include <string.h>

typedef struct CtValue { bool is_bool; uint64_t u; } CtValue;
typedef struct CtVar { const char *name; CtValue value; ScmdTypeKind declared; } CtVar;
typedef struct CtEnv { CtVar *items; size_t len,cap; } CtEnv;

typedef struct CtCtx {
    const char *path;
    ScmdProgram *program;
    int errors;
    size_t steps;
} CtCtx;

static ScmdConst *find_const(ScmdProgram *p,const char *name){for(ScmdConst*c=p->constants;c;c=c->next)if(strcmp(c->name,name)==0)return c;return NULL;}
static ScmdGlobal *find_global(ScmdProgram *p,const char *name){for(ScmdGlobal*g=p->globals;g;g=g->next)if(strcmp(g->name,name)==0)return g;return NULL;}
static CtVar *find_local(CtEnv *e,const char *name){if(!e)return NULL;for(size_t i=e->len;i>0;--i)if(strcmp(e->items[i-1].name,name)==0)return &e->items[i-1];return NULL;}
static void env_push(CtEnv *e,const char *name,CtValue v,ScmdTypeKind declared){if(e->len==e->cap){size_t nc=e->cap?e->cap*2u:16u;e->items=(CtVar*)realloc(e->items,nc*sizeof(*e->items));e->cap=nc;}e->items[e->len++]=(CtVar){name,v,declared};}

static bool eval_expr(CtCtx *ctx,CtEnv *env,ScmdExpr *e,CtValue *out);

static bool eval_const_decl(CtCtx *ctx,ScmdConst *c,CtValue *out){
    if(c->ready){out->is_bool=c->evaluated_type==SCMD_TYPE_BOOL;out->u=c->evaluated;return true;}
    /* 2 marks recursion while evaluating. */
    if(c->evaluated_type==SCMD_TYPE_UNKNOWN && c->ready==false && c->evaluated==UINT64_MAX){scmd_error_at(ctx->path,c->line,c->col,"recursive const '%s'",c->name);ctx->errors++;return false;}
    c->evaluated=UINT64_MAX;
    CtValue v={0}; CtEnv none={0};
    if(!eval_expr(ctx,&none,c->value,&v)){free(none.items);return false;} free(none.items);
    c->evaluated=v.u; c->evaluated_type=v.is_bool?SCMD_TYPE_BOOL:SCMD_TYPE_U8; c->ready=true; *out=v; return true;
}

static bool as_int(CtCtx *ctx,ScmdExpr *e,CtValue v,uint64_t *out){if(v.is_bool){scmd_error_at(ctx->path,e->line,e->col,"compile-time integer expression expected");ctx->errors++;return false;}*out=v.u;return true;}
static bool as_bool(CtCtx *ctx,ScmdExpr *e,CtValue v,bool *out){if(!v.is_bool){scmd_error_at(ctx->path,e->line,e->col,"compile-time bool expression expected");ctx->errors++;return false;}*out=v.u!=0;return true;}

static bool eval_expr(CtCtx *ctx,CtEnv *env,ScmdExpr *e,CtValue *out){
    if(!e)return false;
    switch(e->kind){
        case EXPR_BOOL:*out=(CtValue){true,e->as.boolean?1u:0u};return true;
        case EXPR_INT:*out=(CtValue){false,e->as.integer};return true;
        case EXPR_IDENT:{
            CtVar*v=find_local(env,e->as.name);if(v){*out=v->value;return true;}
            ScmdConst*c=find_const(ctx->program,e->as.name);if(c)return eval_const_decl(ctx,c,out);
            scmd_error_at(ctx->path,e->line,e->col,"unknown compile-time value '%s'",e->as.name);ctx->errors++;return false;}
        case EXPR_INDEX:{
            ScmdGlobal*g=find_global(ctx->program,e->as.index.name);CtValue iv={0};uint64_t i=0;
            if(!g||!g->is_array||!eval_expr(ctx,env,e->as.index.index,&iv)||!as_int(ctx,e,iv,&i))return false;
            if(i>=g->array_len){scmd_error_at(ctx->path,e->line,e->col,"compile-time array index %llu out of range for '%s[%zu]'",(unsigned long long)i,g->name,g->array_len);ctx->errors++;return false;}
            *out=(CtValue){g->resolved_type==SCMD_TYPE_BOOL,g->array_init_values[i]};return true;}
        case EXPR_CALL:
            scmd_error_at(ctx->path,e->line,e->col,"compile-time function-call expressions are not implemented yet");ctx->errors++;return false;
        case EXPR_UNARY:{CtValue a={0};if(!eval_expr(ctx,env,e->as.unary.value,&a))return false;
            if(e->as.unary.op==UNARY_NOT){bool b=false;if(!as_bool(ctx,e,a,&b))return false;*out=(CtValue){true,b?0u:1u};return true;}
            uint64_t x=0;if(!as_int(ctx,e,a,&x))return false;*out=(CtValue){false,e->as.unary.op==UNARY_BIT_NOT?~x:(uint64_t)(0u-x)};return true;}
        case EXPR_BINARY:{CtValue av={0},bv={0};if(!eval_expr(ctx,env,e->as.binary.lhs,&av)||!eval_expr(ctx,env,e->as.binary.rhs,&bv))return false;ScmdBinaryOp op=e->as.binary.op;
            if(op==BIN_LOGICAL_AND||op==BIN_LOGICAL_OR){bool a=false,b=false;if(!as_bool(ctx,e,av,&a)||!as_bool(ctx,e,bv,&b))return false;*out=(CtValue){true,(op==BIN_LOGICAL_AND?(a&&b):(a||b))?1u:0u};return true;}
            if(op==BIN_EQ||op==BIN_NEQ){if(av.is_bool!=bv.is_bool){scmd_error_at(ctx->path,e->line,e->col,"compile-time equality operands have different types");ctx->errors++;return false;}bool q=av.u==bv.u;if(op==BIN_NEQ)q=!q;*out=(CtValue){true,q?1u:0u};return true;}
            uint64_t a=0,b=0;if(!as_int(ctx,e,av,&a)||!as_int(ctx,e,bv,&b))return false;
            switch(op){
                case BIN_ADD:*out=(CtValue){false,a+b};return true;case BIN_SUB:*out=(CtValue){false,a-b};return true;case BIN_MUL:*out=(CtValue){false,a*b};return true;
                case BIN_DIV:*out=(CtValue){false,b?a/b:0u};return true;case BIN_MOD:*out=(CtValue){false,b?a%b:a};return true;
                case BIN_SHL:*out=(CtValue){false,b<64u?a<<b:0u};return true;case BIN_SHR:*out=(CtValue){false,b<64u?a>>b:0u};return true;
                case BIN_LT:*out=(CtValue){true,a<b};return true;case BIN_LE:*out=(CtValue){true,a<=b};return true;case BIN_GT:*out=(CtValue){true,a>b};return true;case BIN_GE:*out=(CtValue){true,a>=b};return true;
                case BIN_BIT_AND:*out=(CtValue){false,a&b};return true;case BIN_BIT_OR:*out=(CtValue){false,a|b};return true;case BIN_BIT_XOR:*out=(CtValue){false,a^b};return true;
                default:break;
            }
            return false;}
    }
    return false;
}

static bool apply_assign(CtCtx*ctx,CtValue *dst,ScmdAssignOp op,CtValue rhs,ScmdTypeKind declared,int line,int col){
    if(declared==SCMD_TYPE_BOOL){if(!rhs.is_bool||op!=ASSIGN_SET){scmd_error_at(ctx->path,line,col,"compile-time bool assignment requires '=' and bool rhs");ctx->errors++;return false;}*dst=rhs;return true;}
    if(rhs.is_bool){scmd_error_at(ctx->path,line,col,"compile-time integer assignment received bool rhs");ctx->errors++;return false;}
    uint64_t a=dst->u,b=rhs.u,r=b;switch(op){case ASSIGN_SET:r=b;break;case ASSIGN_ADD:r=a+b;break;case ASSIGN_SUB:r=a-b;break;case ASSIGN_MUL:r=a*b;break;case ASSIGN_DIV:r=b?a/b:0u;break;case ASSIGN_MOD:r=b?a%b:a;break;case ASSIGN_BIT_AND:r=a&b;break;case ASSIGN_BIT_OR:r=a|b;break;case ASSIGN_BIT_XOR:r=a^b;break;case ASSIGN_SHL:r=b<64u?a<<b:0u;break;case ASSIGN_SHR:r=b<64u?a>>b:0u;break;}
    if(declared==SCMD_TYPE_U8&&r>255u){scmd_error_at(ctx->path,line,col,"compile-time u8 assignment value %llu does not fit in u8",(unsigned long long)r);ctx->errors++;return false;}dst->is_bool=false;dst->u=r;return true;
}

static bool exec_stmt_list(CtCtx *ctx,CtEnv *env,ScmdStmt *s);
static bool exec_one(CtCtx *ctx,CtEnv *env,ScmdStmt *s){
    if(++ctx->steps>1000000u){scmd_error_at(ctx->path,s->line,s->col,"compile block exceeded 1000000 execution steps");ctx->errors++;return false;}
    switch(s->kind){
        case STMT_VAR_DECL:{CtValue v={0};if(!eval_expr(ctx,env,s->as.var_decl.init,&v))return false;ScmdTypeKind d=s->as.var_decl.declared_type;if(d==SCMD_TYPE_BOOL&&!v.is_bool){scmd_error_at(ctx->path,s->line,s->col,"compile bool initializer must be bool");ctx->errors++;return false;}if(d==SCMD_TYPE_U8&&(v.is_bool||v.u>255u)){scmd_error_at(ctx->path,s->line,s->col,"compile u8 initializer must fit in u8");ctx->errors++;return false;}if(find_local(env,s->as.var_decl.name)){scmd_error_at(ctx->path,s->line,s->col,"duplicate compile local '%s'",s->as.var_decl.name);ctx->errors++;return false;}env_push(env,s->as.var_decl.name,v,d);return true;}
        case STMT_ASSIGN:{CtVar*v=find_local(env,s->as.assign.name);CtValue rhs={0};if(!v||!eval_expr(ctx,env,s->as.assign.value,&rhs)){if(!v){scmd_error_at(ctx->path,s->line,s->col,"compile assignment to unknown local '%s'",s->as.assign.name);ctx->errors++;}return false;}return apply_assign(ctx,&v->value,s->as.assign.op,rhs,v->declared,s->line,s->col);}
        case STMT_ARRAY_ASSIGN:{ScmdGlobal*g=find_global(ctx->program,s->as.array_assign.name);CtValue iv={0},rhs={0};uint64_t i=0;if(!g||!g->is_array){scmd_error_at(ctx->path,s->line,s->col,"compile assignment target '%s' is not a global array",s->as.array_assign.name);ctx->errors++;return false;}if(!eval_expr(ctx,env,s->as.array_assign.index,&iv)||!as_int(ctx,s->as.array_assign.index,iv,&i)||!eval_expr(ctx,env,s->as.array_assign.value,&rhs))return false;if(i>=g->array_len){scmd_error_at(ctx->path,s->line,s->col,"compile array index %llu out of range for '%s[%zu]'",(unsigned long long)i,g->name,g->array_len);ctx->errors++;return false;}CtValue cur={g->resolved_type==SCMD_TYPE_BOOL,g->array_init_values[i]};if(!apply_assign(ctx,&cur,s->as.array_assign.op,rhs,g->resolved_type,s->line,s->col))return false;g->array_init_values[i]=cur.u;return true;}
        case STMT_IF:{CtValue cv={0};bool b=false;if(!eval_expr(ctx,env,s->as.if_stmt.cond,&cv)||!as_bool(ctx,s->as.if_stmt.cond,cv,&b))return false;ScmdStmt*w=b?s->as.if_stmt.then_block:s->as.if_stmt.else_block;return !w||exec_stmt_list(ctx,env,w->as.block_scope.first);}
        case STMT_WHILE:{for(;;){CtValue cv={0};bool b=false;if(!eval_expr(ctx,env,s->as.while_stmt.cond,&cv)||!as_bool(ctx,s->as.while_stmt.cond,cv,&b))return false;if(!b)return true;if(!exec_stmt_list(ctx,env,s->as.while_stmt.body->as.block_scope.first))return false;}}
        case STMT_FOR:{size_t mark=env->len;if(s->as.for_stmt.init&&!exec_one(ctx,env,s->as.for_stmt.init)){env->len=mark;return false;}for(;;){if(s->as.for_stmt.cond){CtValue cv={0};bool b=false;if(!eval_expr(ctx,env,s->as.for_stmt.cond,&cv)||!as_bool(ctx,s->as.for_stmt.cond,cv,&b)){env->len=mark;return false;}if(!b)break;}if(s->as.for_stmt.body&&!exec_stmt_list(ctx,env,s->as.for_stmt.body->as.block_scope.first)){env->len=mark;return false;}if(s->as.for_stmt.step&&!exec_one(ctx,env,s->as.for_stmt.step)){env->len=mark;return false;}}env->len=mark;return true;}
        case STMT_BLOCK_SCOPE:{size_t mark=env->len;bool ok=exec_stmt_list(ctx,env,s->as.block_scope.first);env->len=mark;return ok;}
        case STMT_CALL:{if(strcmp(s->as.call.name,"assert")==0&&s->as.call.arg_count==1u){CtValue v={0};bool b=false;if(!eval_expr(ctx,env,s->as.call.args[0],&v)||!as_bool(ctx,s->as.call.args[0],v,&b))return false;if(!b){scmd_error_at(ctx->path,s->line,s->col,"compile-time assert failed");ctx->errors++;return false;}return true;}scmd_error_at(ctx->path,s->line,s->col,"compile block only supports assert(expr) calls in 0.11");ctx->errors++;return false;}
        default:scmd_error_at(ctx->path,s->line,s->col,"statement is not valid inside compile block");ctx->errors++;return false;
    }
}
static bool exec_stmt_list(CtCtx *ctx,CtEnv *env,ScmdStmt *s){for(;s;s=s->next)if(!exec_one(ctx,env,s))return false;return true;}

static bool replace_consts_expr(CtCtx *ctx,ScmdExpr *e){if(!e)return true;switch(e->kind){
    case EXPR_IDENT:{ScmdConst*c=find_const(ctx->program,e->as.name);if(c){CtValue v={0};if(!eval_const_decl(ctx,c,&v))return false;free(e->as.name);e->kind=v.is_bool?EXPR_BOOL:EXPR_INT;if(v.is_bool)e->as.boolean=v.u!=0;else e->as.integer=v.u;}return true;}
    case EXPR_INDEX:return replace_consts_expr(ctx,e->as.index.index);
    case EXPR_CALL:for(size_t i=0;i<e->as.call.arg_count;++i)if(!replace_consts_expr(ctx,e->as.call.args[i]))return false;return true;
    case EXPR_UNARY:return replace_consts_expr(ctx,e->as.unary.value);
    case EXPR_BINARY:return replace_consts_expr(ctx,e->as.binary.lhs)&&replace_consts_expr(ctx,e->as.binary.rhs);
    default:return true;}}
static bool replace_consts_stmt(CtCtx *ctx,ScmdStmt*s){for(;s;s=s->next){switch(s->kind){case STMT_VAR_DECL:if(!replace_consts_expr(ctx,s->as.var_decl.init))return false;break;case STMT_ASSIGN:if(!replace_consts_expr(ctx,s->as.assign.value))return false;break;case STMT_ARRAY_ASSIGN:if(!replace_consts_expr(ctx,s->as.array_assign.index)||!replace_consts_expr(ctx,s->as.array_assign.value))return false;break;case STMT_IF:if(!replace_consts_expr(ctx,s->as.if_stmt.cond)||!replace_consts_stmt(ctx,s->as.if_stmt.then_block)||!replace_consts_stmt(ctx,s->as.if_stmt.else_block))return false;break;case STMT_WHILE:if(!replace_consts_expr(ctx,s->as.while_stmt.cond)||!replace_consts_stmt(ctx,s->as.while_stmt.body))return false;break;case STMT_FOR:if(!replace_consts_stmt(ctx,s->as.for_stmt.init)||!replace_consts_expr(ctx,s->as.for_stmt.cond)||!replace_consts_stmt(ctx,s->as.for_stmt.step)||!replace_consts_stmt(ctx,s->as.for_stmt.body))return false;break;case STMT_RETURN:if(!replace_consts_expr(ctx,s->as.return_stmt.value))return false;break;case STMT_CALL:for(size_t i=0;i<s->as.call.arg_count;++i)if(!replace_consts_expr(ctx,s->as.call.args[i]))return false;break;case STMT_BLOCK_SCOPE:if(!replace_consts_stmt(ctx,s->as.block_scope.first))return false;break;default:break;}}return true;}

bool scmd_comptime_run(const char *path,ScmdProgram *program){
    CtCtx ctx={path,program,0,0};
    /* Resolve every const first so forward references are diagnosed here. */
    for(ScmdConst*c=program->constants;c;c=c->next){CtValue v={0};if(!eval_const_decl(&ctx,c,&v))return false;}
    /* Size/initialize fixed runtime arrays. */
    for(ScmdGlobal*g=program->globals;g;g=g->next){
        if(!g->is_array)continue;CtValue lv={0},iv={0};uint64_t n=0;
        if(!eval_expr(&ctx,NULL,g->array_len_expr,&lv)||!as_int(&ctx,g->array_len_expr,lv,&n))return false;
        if(n==0u||n>256u){scmd_error_at(path,g->line,g->col,"fixed array '%s' length must be 1..256",g->name);ctx.errors++;continue;}
        if(g->declared_type==SCMD_TYPE_UNKNOWN){scmd_error_at(path,g->line,g->col,"fixed arrays require explicit bool or u8 element type");ctx.errors++;continue;}
        if(!eval_expr(&ctx,NULL,g->init,&iv))return false;
        if(g->declared_type==SCMD_TYPE_BOOL&&!iv.is_bool){scmd_error_at(path,g->line,g->col,"bool array initializer must be bool");ctx.errors++;continue;}
        if(g->declared_type==SCMD_TYPE_U8&&(iv.is_bool||iv.u>255u)){scmd_error_at(path,g->line,g->col,"u8 array initializer must fit in u8");ctx.errors++;continue;}
        g->array_len=(size_t)n;g->resolved_type=g->declared_type;g->array_init_values=(uint64_t*)calloc(g->array_len,sizeof(uint64_t));if(!g->array_init_values)return false;for(size_t i=0;i<g->array_len;++i)g->array_init_values[i]=iv.u;
    }
    if(ctx.errors)return false;
    for(ScmdCompileBlock*cb=program->compile_blocks;cb;cb=cb->next){CtEnv env={0};if(!exec_stmt_list(&ctx,&env,cb->body)){free(env.items);return false;}free(env.items);}
    /* Runtime AST sees constants as literals; compile blocks themselves never reach codegen. */
    for(ScmdGlobal*g=program->globals;g;g=g->next){if(!replace_consts_expr(&ctx,g->init)||!replace_consts_expr(&ctx,g->array_len_expr))return false;}
    for(ScmdFunction*f=program->functions;f;f=f->next)if(!replace_consts_stmt(&ctx,f->body))return false;
    for(ScmdBlock*b=program->blocks;b;b=b->next)if(!replace_consts_stmt(&ctx,b->body))return false;
    return ctx.errors==0;
}
