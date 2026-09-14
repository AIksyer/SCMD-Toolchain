#include "scmd/codegen.h"
#include "scmd/common.h"
#include "scmd/version.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path,0777)
#endif

typedef struct AliasDef { char *name; char *body; bool opaque; } AliasDef;
typedef struct AliasVec { AliasDef *items; size_t len,cap; } AliasVec;
typedef struct AsyncWorker { char *file_path,*exec_ref,*entry_alias; int delay_ms; bool clear_first; } AsyncWorker;
typedef struct AsyncVec { AsyncWorker *items; size_t len,cap; } AsyncVec;

typedef struct CGVar {
    const char *source_name;
    ScmdTypeKind type;
    char *bit[8];
} CGVar;

typedef struct CGFunction {
    const ScmdFunction *ast;
    char *entry_alias,*ret_alias;
    CGVar *locals; size_t local_count;
} CGFunction;

typedef struct CGRecord { const char *source_name; char *alias_name; } CGRecord;
typedef struct CGBlock {
    const ScmdBlock *ast;
    char *entry_alias,*ret_alias;
    CGRecord *records; size_t record_count;
} CGBlock;

typedef enum BKind { B_CONST,B_ALIAS,B_NOT,B_AND,B_OR,B_XOR } BKind;
typedef struct BExpr BExpr;
struct BExpr { BKind kind; bool c; const char *alias; BExpr *a,*b,*alloc_next; };
static _Thread_local BExpr *bexpr_arena = NULL;
typedef struct BVec { BExpr *b[8]; } BVec;

typedef struct Codegen {
    const char *source_path,*output_path;
    AliasVec defs;
    AsyncVec workers;
    CGVar *globals; size_t global_count;
    CGFunction *functions; size_t function_count;
    CGBlock *blocks; size_t block_count;
    size_t next_label,next_tmp;
    int errors;
    ScmdCodegenOptions options;
} Codegen;

static char *path_dirname(const char *path){const char *last=NULL;for(const char *p=path;*p;++p)if(*p=='/'||*p=='\\')last=p;if(!last)return scmd_strdup(".");return scmd_strndup(path,(size_t)(last-path));}
static const char *path_basename(const char *p){const char *b=p;for(;*p;++p)if(*p=='/'||*p=='\\')b=p+1;return b;}
static char *path_stem(const char *path){const char *b=path_basename(path),*d=strrchr(b,'.');return scmd_strndup(b,d?(size_t)(d-b):strlen(b));}
static bool mkdirs(const char *path){char *t=scmd_strdup(path);if(!t)return false;for(char *p=t;*p;++p)if(*p=='\\')*p='/';char *scan=t;
#ifdef _WIN32
if(((scan[0]>='A'&&scan[0]<='Z')||(scan[0]>='a'&&scan[0]<='z'))&&scan[1]==':')scan+=2;
#endif
for(char *p=scan+(*scan=='/'?1:0);*p;++p){if(*p!='/')continue;*p='\0';if(*t&&MKDIR(t)!=0&&errno!=EEXIST){free(t);return false;}*p='/';}if(*t&&MKDIR(t)!=0&&errno!=EEXIST){free(t);return false;}free(t);return true;}

static void emit_alias_ex(Codegen *cg,const char *name,const char *body,bool opaque){if(cg->defs.len==cg->defs.cap){size_t nc=cg->defs.cap?cg->defs.cap*2u:128u;cg->defs.items=(AliasDef*)realloc(cg->defs.items,nc*sizeof(*cg->defs.items));cg->defs.cap=nc;}cg->defs.items[cg->defs.len].name=scmd_strdup(name);cg->defs.items[cg->defs.len].body=scmd_strdup(body?body:"");cg->defs.items[cg->defs.len].opaque=opaque;cg->defs.len++;}
static void emit_alias(Codegen *cg,const char *name,const char *body){emit_alias_ex(cg,name,body,false);}
static char *new_label(Codegen *cg){return scmd_format("__scmd_l%zu",cg->next_label++);}

static BExpr *bn(BKind k,BExpr*a,BExpr*b){BExpr*e=(BExpr*)calloc(1,sizeof(*e));if(!e)return NULL;e->kind=k;e->a=a;e->b=b;e->alloc_next=bexpr_arena;bexpr_arena=e;return e;}
static void bexpr_arena_rewind(BExpr *mark){while(bexpr_arena&&bexpr_arena!=mark){BExpr*n=bexpr_arena->alloc_next;free(bexpr_arena);bexpr_arena=n;}}
static BExpr *bc(bool c){BExpr*e=bn(B_CONST,NULL,NULL);e->c=c;return e;}
static BExpr *ba(const char *a){BExpr*e=bn(B_ALIAS,NULL,NULL);e->alias=a;return e;}
static BExpr *bnot(BExpr*a){if(a->kind==B_CONST)return bc(!a->c);return bn(B_NOT,a,NULL);}
static BExpr *band(BExpr*a,BExpr*b){if(a->kind==B_CONST)return a->c?b:bc(false);if(b->kind==B_CONST)return b->c?a:bc(false);return bn(B_AND,a,b);}
static BExpr *bor(BExpr*a,BExpr*b){if(a->kind==B_CONST)return a->c?bc(true):b;if(b->kind==B_CONST)return b->c?bc(true):a;return bn(B_OR,a,b);}
static BExpr *bxor(BExpr*a,BExpr*b){if(a->kind==B_CONST)return a->c?bnot(b):b;if(b->kind==B_CONST)return b->c?bnot(a):a;return bn(B_XOR,a,b);}
static BExpr *bxnor(BExpr*a,BExpr*b){return bnot(bxor(a,b));}

static CGFunction *resolve_function(Codegen *cg,const char *name){for(size_t i=0;i<cg->function_count;++i)if(strcmp(cg->functions[i].ast->name,name)==0)return &cg->functions[i];return NULL;}
static CGBlock *resolve_block(Codegen *cg,const char *name){for(size_t i=0;i<cg->block_count;++i)if(strcmp(cg->blocks[i].ast->name,name)==0)return &cg->blocks[i];return NULL;}
static CGRecord *resolve_record(CGBlock *b,const char *name){if(!b)return NULL;for(size_t i=0;i<b->record_count;++i)if(strcmp(b->records[i].source_name,name)==0)return &b->records[i];return NULL;}
static CGVar *resolve_var(Codegen *cg,CGFunction *fn,const char *name){if(fn)for(size_t i=0;i<fn->local_count;++i)if(strcmp(fn->locals[i].source_name,name)==0)return &fn->locals[i];for(size_t i=0;i<cg->global_count;++i)if(strcmp(cg->globals[i].source_name,name)==0)return &cg->globals[i];return NULL;}

static void alloc_var_bits(Codegen *cg,CGVar *v,const char *prefix,bool preserve_default){int n=v->type==SCMD_TYPE_U8?8:1;for(int i=0;i<n;++i){v->bit[i]=scmd_format("%s_b%d",prefix,i);if(preserve_default||!cg->options.optimize)emit_alias(cg,v->bit[i],"__scmd_false");}}

static void collect_locals_stmt(Codegen *cg,const ScmdStmt *s,CGFunction *fn,size_t fn_id){for(;s;s=s->next){if(s->kind==STMT_VAR_DECL){size_t n=fn->local_count++;fn->locals=(CGVar*)realloc(fn->locals,fn->local_count*sizeof(*fn->locals));CGVar *v=&fn->locals[n];memset(v,0,sizeof(*v));v->source_name=s->as.var_decl.name;v->type=s->as.var_decl.resolved_type;char *p=scmd_format("__scmd_f%zu_v%zu",fn_id,n);alloc_var_bits(cg,v,p,true);free(p);}else if(s->kind==STMT_IF){if(s->as.if_stmt.then_block)collect_locals_stmt(cg,s->as.if_stmt.then_block->as.block_scope.first,fn,fn_id);if(s->as.if_stmt.else_block)collect_locals_stmt(cg,s->as.if_stmt.else_block->as.block_scope.first,fn,fn_id);}else if(s->kind==STMT_WHILE){if(s->as.while_stmt.body)collect_locals_stmt(cg,s->as.while_stmt.body->as.block_scope.first,fn,fn_id);}else if(s->kind==STMT_FOR){if(s->as.for_stmt.init)collect_locals_stmt(cg,s->as.for_stmt.init,fn,fn_id);if(s->as.for_stmt.body)collect_locals_stmt(cg,s->as.for_stmt.body->as.block_scope.first,fn,fn_id);}else if(s->kind==STMT_BLOCK_SCOPE)collect_locals_stmt(cg,s->as.block_scope.first,fn,fn_id);}}

static void setup_symbols(Codegen *cg,const ScmdProgram *p){
    for(const ScmdGlobal*g=p->globals;g;g=g->next)cg->global_count++;cg->globals=(CGVar*)calloc(cg->global_count,sizeof(*cg->globals));size_t gi=0;
    for(const ScmdGlobal*g=p->globals;g;g=g->next,++gi){CGVar*v=&cg->globals[gi];v->source_name=g->name;v->type=g->resolved_type;char*pre=scmd_format("__scmd_g%zu",gi);alloc_var_bits(cg,v,pre,!cg->options.optimize);free(pre);}
    for(const ScmdFunction*f=p->functions;f;f=f->next)cg->function_count++;cg->functions=(CGFunction*)calloc(cg->function_count,sizeof(*cg->functions));size_t fi=0;
    for(const ScmdFunction*f=p->functions;f;f=f->next,++fi){CGFunction*cf=&cg->functions[fi];cf->ast=f;cf->entry_alias=scmd_format("__scmd_fn%zu",fi);cf->ret_alias=scmd_format("__scmd_ret%zu",fi);emit_alias(cg,cf->ret_alias,"__scmd_halt");collect_locals_stmt(cg,f->body,cf,fi);}
    for(const ScmdBlock*b=p->blocks;b;b=b->next)cg->block_count++;
    cg->blocks=(CGBlock*)calloc(cg->block_count,sizeof(*cg->blocks));
    size_t bi=0;
    for(const ScmdBlock*b=p->blocks;b;b=b->next,++bi){
        CGBlock*cb=&cg->blocks[bi];
        cb->ast=b;
        cb->entry_alias=scmd_format("__scmd_blk%zu",bi);
        cb->ret_alias=scmd_format("__scmd_blkret%zu",bi);
        emit_alias(cg,cb->ret_alias,"__scmd_halt");
        for(const ScmdStmt*s=b->body;s;s=s->next) if(s->kind==STMT_RECORD){
            size_t n=cb->record_count++;
            cb->records=(CGRecord*)realloc(cb->records,cb->record_count*sizeof(*cb->records));
            cb->records[n].source_name=s->as.record.name;
            cb->records[n].alias_name=scmd_format("__scmd_blk%zu_r%zu",bi,n);
        }
    }
}

static bool eval_const_u8(const ScmdExpr *e,uint8_t *out);
static bool eval_const_bool(const ScmdExpr *e,bool *out){if(!e)return false;if(e->kind==EXPR_BOOL){*out=e->as.boolean;return true;}if(e->kind==EXPR_BINARY&&(e->as.binary.op==BIN_EQ||e->as.binary.op==BIN_NEQ||e->as.binary.op==BIN_LT||e->as.binary.op==BIN_LE||e->as.binary.op==BIN_GT||e->as.binary.op==BIN_GE)){uint8_t a,b;if(eval_const_u8(e->as.binary.lhs,&a)&&eval_const_u8(e->as.binary.rhs,&b)){switch(e->as.binary.op){case BIN_EQ:*out=a==b;break;case BIN_NEQ:*out=a!=b;break;case BIN_LT:*out=a<b;break;case BIN_LE:*out=a<=b;break;case BIN_GT:*out=a>b;break;default:*out=a>=b;break;}return true;}}return false;}
static bool eval_const_u8(const ScmdExpr *e,uint8_t *out){if(!e)return false;if(e->kind==EXPR_INT){*out=(uint8_t)e->as.integer;return true;}if(e->kind==EXPR_UNARY){uint8_t a;if(!eval_const_u8(e->as.unary.value,&a))return false;if(e->as.unary.op==UNARY_BIT_NOT)*out=(uint8_t)~a;else if(e->as.unary.op==UNARY_NEG)*out=(uint8_t)(0u-a);else return false;return true;}if(e->kind==EXPR_BINARY){uint8_t a,b;if(!eval_const_u8(e->as.binary.lhs,&a)||!eval_const_u8(e->as.binary.rhs,&b))return false;switch(e->as.binary.op){case BIN_ADD:*out=(uint8_t)(a+b);return true;case BIN_SUB:*out=(uint8_t)(a-b);return true;case BIN_MUL:*out=(uint8_t)(a*b);return true;case BIN_DIV:*out=b?(uint8_t)(a/b):0u;return true;case BIN_MOD:*out=b?(uint8_t)(a%b):a;return true;case BIN_SHL:*out=b<8u?(uint8_t)(a<<b):0u;return true;case BIN_SHR:*out=b<8u?(uint8_t)(a>>b):0u;return true;case BIN_BIT_AND:*out=(uint8_t)(a&b);return true;case BIN_BIT_OR:*out=(uint8_t)(a|b);return true;case BIN_BIT_XOR:*out=(uint8_t)(a^b);return true;default:return false;}}return false;}

static BVec vec_const(uint8_t v){BVec r;for(int i=0;i<8;++i)r.b[i]=bc(((v>>i)&1u)!=0);return r;}
static BVec vec_var(CGVar*v){BVec r;for(int i=0;i<8;++i)r.b[i]=ba(v->bit[i]);return r;}
static BVec vec_not(BVec a){BVec r;for(int i=0;i<8;++i)r.b[i]=bnot(a.b[i]);return r;}
static BVec vec_and(BVec a,BVec b){BVec r;for(int i=0;i<8;++i)r.b[i]=band(a.b[i],b.b[i]);return r;}
static BVec vec_or(BVec a,BVec b){BVec r;for(int i=0;i<8;++i)r.b[i]=bor(a.b[i],b.b[i]);return r;}
static BVec vec_xor(BVec a,BVec b){BVec r;for(int i=0;i<8;++i)r.b[i]=bxor(a.b[i],b.b[i]);return r;}
static BVec vec_add(BVec a,BVec b,BExpr *cin){BVec r;BExpr*c=cin;for(int i=0;i<8;++i){BExpr*p=bxor(a.b[i],b.b[i]);r.b[i]=bxor(p,c);c=bor(band(a.b[i],b.b[i]),band(c,p));}return r;}
static BVec vec_neg(BVec a){return vec_add(vec_not(a),vec_const(0),bc(true));}
static BVec vec_sub(BVec a,BVec b){return vec_add(a,vec_not(b),bc(true));}
static BVec vec_shift_const(BVec a,unsigned n,bool left){BVec r;for(int i=0;i<8;++i){int src=left?i-(int)n:i+(int)n;r.b[i]=(src>=0&&src<8)?a.b[src]:bc(false);}return r;}
static BVec vec_mul(BVec a,BVec b){BVec acc=vec_const(0);for(int j=0;j<8;++j){BVec part;for(int i=0;i<8;++i){int src=i-j;part.b[i]=src>=0?band(a.b[src],b.b[j]):bc(false);}acc=vec_add(acc,part,bc(false));}return acc;}
static BExpr *vec_eq(BVec a,BVec b){BExpr*r=bc(true);for(int i=0;i<8;++i)r=band(r,bxnor(a.b[i],b.b[i]));return r;}
static BExpr *vec_lt(BVec a,BVec b){BExpr*eq=bc(true),*lt=bc(false);for(int i=7;i>=0;--i){lt=bor(lt,band(eq,band(bnot(a.b[i]),b.b[i])));eq=band(eq,bxnor(a.b[i],b.b[i]));}return lt;}
static BVec vec_mux(BExpr *cond,BVec t,BVec f){BVec r;for(int i=0;i<8;++i)r.b[i]=bor(band(cond,t.b[i]),band(bnot(cond),f.b[i]));return r;}
static BVec vec_shift_dynamic(BVec a,BVec sh,bool left){BVec cur=a;const unsigned steps[3]={1u,2u,4u};for(int k=0;k<3;++k){BVec shifted=vec_shift_const(cur,steps[k],left);cur=vec_mux(sh.b[k],shifted,cur);}BExpr*high=bc(false);for(int i=3;i<8;++i)high=bor(high,sh.b[i]);return vec_mux(high,vec_const(0),cur);}
static void vec_divmod(BVec dividend,BVec divisor,BVec *qout,BVec *rout){BVec q=vec_const(0),rem=vec_const(0),zero=vec_const(0);BExpr*nonzero=bnot(vec_eq(divisor,zero));for(int i=7;i>=0;--i){BVec shifted=vec_shift_const(rem,1u,true);shifted.b[0]=dividend.b[i];BExpr*ge=bnot(vec_lt(shifted,divisor));BExpr*take=band(nonzero,ge);BVec diff=vec_sub(shifted,divisor);rem=vec_mux(take,diff,shifted);q.b[i]=take;}*qout=q;*rout=rem;}

static BVec build_u8(Codegen *cg,CGFunction *fn,const ScmdExpr *e);
static BExpr *build_bool(Codegen *cg,CGFunction *fn,const ScmdExpr *e){
    if(!e)return bc(false);
    if(cg->options.optimize){bool cv;if(eval_const_bool(e,&cv))return bc(cv);}
    if(e->kind==EXPR_BOOL)return bc(e->as.boolean);
    if(e->kind==EXPR_IDENT){
        CGVar*v=resolve_var(cg,fn,e->as.name);
        if(!v||v->type!=SCMD_TYPE_BOOL){cg->errors++;return bc(false);}
        return ba(v->bit[0]);
    }
    if(e->kind==EXPR_UNARY&&e->as.unary.op==UNARY_NOT)
        return bnot(build_bool(cg,fn,e->as.unary.value));
    if(e->kind==EXPR_BINARY){
        ScmdBinaryOp op=e->as.binary.op;
        if(e->as.binary.lhs->inferred_type==SCMD_TYPE_BOOL){
            BExpr*a=build_bool(cg,fn,e->as.binary.lhs);
            BExpr*b=build_bool(cg,fn,e->as.binary.rhs);
            if(op==BIN_LOGICAL_AND)return band(a,b);
            if(op==BIN_LOGICAL_OR)return bor(a,b);
            if(op==BIN_BIT_XOR)return bxor(a,b);
            if(op==BIN_EQ)return bxnor(a,b);
            if(op==BIN_NEQ)return bxor(a,b);
        }else{
            BVec a=build_u8(cg,fn,e->as.binary.lhs),b=build_u8(cg,fn,e->as.binary.rhs);
            if(op==BIN_EQ)return vec_eq(a,b);
            if(op==BIN_NEQ)return bnot(vec_eq(a,b));
            if(op==BIN_LT)return vec_lt(a,b);
            if(op==BIN_GE)return bnot(vec_lt(a,b));
            if(op==BIN_LE){BExpr*lt=vec_lt(a,b);BExpr*eq=vec_eq(a,b);return bor(lt,eq);}
            if(op==BIN_GT){BExpr*lt=vec_lt(a,b);BExpr*eq=vec_eq(a,b);return band(bnot(lt),bnot(eq));}
        }
    }
    bool cv=false;
    if(eval_const_bool(e,&cv))return bc(cv);
    scmd_error_at(cg->source_path,e->line,e->col,"CS2 backend cannot lower this bool expression yet");
    cg->errors++;
    return bc(false);
}
static BVec build_u8(Codegen *cg,CGFunction *fn,const ScmdExpr *e){
    if(cg->options.optimize){uint8_t cv;if(eval_const_u8(e,&cv))return vec_const(cv);}
    if(e->kind==EXPR_INT)return vec_const((uint8_t)e->as.integer);if(e->kind==EXPR_IDENT){CGVar*v=resolve_var(cg,fn,e->as.name);if(v&&v->type==SCMD_TYPE_U8)return vec_var(v);}
    if(e->kind==EXPR_UNARY){BVec a=build_u8(cg,fn,e->as.unary.value);if(e->as.unary.op==UNARY_BIT_NOT)return vec_not(a);if(e->as.unary.op==UNARY_NEG)return vec_neg(a);}
    if(e->kind==EXPR_BINARY){BVec a=build_u8(cg,fn,e->as.binary.lhs),b=build_u8(cg,fn,e->as.binary.rhs);switch(e->as.binary.op){case BIN_ADD:return vec_add(a,b,bc(false));case BIN_SUB:return vec_sub(a,b);case BIN_MUL:return vec_mul(a,b);case BIN_DIV:{BVec q,r;vec_divmod(a,b,&q,&r);return q;}case BIN_MOD:{BVec q,r;vec_divmod(a,b,&q,&r);return r;}case BIN_BIT_AND:return vec_and(a,b);case BIN_BIT_OR:return vec_or(a,b);case BIN_BIT_XOR:return vec_xor(a,b);case BIN_SHL:return vec_shift_dynamic(a,b,true);case BIN_SHR:return vec_shift_dynamic(a,b,false);default:break;}}
    uint8_t cv;if(eval_const_u8(e,&cv))return vec_const(cv);scmd_error_at(cg->source_path,e->line,e->col,"CS2 backend cannot lower this u8 expression yet");cg->errors++;return vec_const(0);
}

static char *compile_bexpr(Codegen *cg,BExpr *e,const char *on_true,const char *on_false){if(!e)return scmd_strdup(on_false);switch(e->kind){case B_CONST:return scmd_strdup(e->c?on_true:on_false);case B_ALIAS:{char*l=new_label(cg);char*body=scmd_format("alias __scmd_branch_true %s;alias __scmd_branch_false %s;%s",on_true,on_false,e->alias);emit_alias(cg,l,body);free(body);return l;}case B_NOT:return compile_bexpr(cg,e->a,on_false,on_true);case B_AND:{char*r=compile_bexpr(cg,e->b,on_true,on_false);char*l=compile_bexpr(cg,e->a,r,on_false);free(r);return l;}case B_OR:{char*r=compile_bexpr(cg,e->b,on_true,on_false);char*l=compile_bexpr(cg,e->a,on_true,r);free(r);return l;}case B_XOR:{char*rt=compile_bexpr(cg,e->b,on_false,on_true);char*rf=compile_bexpr(cg,e->b,on_true,on_false);char*l=compile_bexpr(cg,e->a,rt,rf);free(rt);free(rf);return l;}}return scmd_strdup(on_false);}
static char *compile_set_bit(Codegen *cg,BExpr *e,const char *target,const char *next){
    if(cg->options.optimize&&e&&e->kind==B_CONST){
        char*l=new_label(cg),*body=scmd_format("alias %s %s;%s",target,e->c?"__scmd_true":"__scmd_false",next);
        emit_alias(cg,l,body);free(body);return l;
    }
    char*t=new_label(cg),*f=new_label(cg);char*bt=scmd_format("alias %s __scmd_true;%s",target,next),*bf=scmd_format("alias %s __scmd_false;%s",target,next);emit_alias(cg,t,bt);emit_alias(cg,f,bf);free(bt);free(bf);char*entry=compile_bexpr(cg,e,t,f);free(t);free(f);return entry;}
static char *compile_materialize_bvec(Codegen *cg,BVec val,char **bits,const char *next){
    char *cont=scmd_strdup(next);
    for(int i=7;i>=0;--i){
        char *n=compile_set_bit(cg,val.b[i],bits[i],cont);
        free(cont);cont=n;
    }
    return cont;
}

/*
 * Runtime-safe ripple add/sub lowering.
 *
 * The original v0.6 implementation represented the whole carry chain as one
 * nested boolean expression. That is mathematically correct, but Source/CS2
 * can lose work in sufficiently deep alias-dispatch paths. Materialize both
 * operands first, then advance one carry latch per bit. This deliberately
 * trades more aliases for a much shallower execution path.
 */
static char *compile_assign_u8_addsub(Codegen *cg,CGFunction *fn,CGVar *v,const ScmdExpr *expr,const char *next){
    const bool is_sub=expr->as.binary.op==BIN_SUB;
    BVec avec=build_u8(cg,fn,expr->as.binary.lhs);
    BVec bvec=build_u8(cg,fn,expr->as.binary.rhs);
    size_t id=cg->next_tmp++;
    char *abits[8],*bbits[8],*sbits[8],*carry[9];
    for(int i=0;i<8;++i){
        abits[i]=scmd_format("__scmd_t%zu_a%d",id,i);
        bbits[i]=scmd_format("__scmd_t%zu_b%d",id,i);
        sbits[i]=scmd_format("__scmd_t%zu_s%d",id,i);
        emit_alias(cg,abits[i],"__scmd_false");
        emit_alias(cg,bbits[i],"__scmd_false");
        emit_alias(cg,sbits[i],"__scmd_false");
    }
    for(int i=0;i<9;++i){
        carry[i]=scmd_format("__scmd_t%zu_c%d",id,i);
        emit_alias(cg,carry[i],(i==0&&is_sub)?"__scmd_true":"__scmd_false");
    }

    /* Copy the completed sum to the destination only after all stages finish. */
    char *cont=scmd_strdup(next);
    for(int i=7;i>=0;--i){
        char *n=compile_set_bit(cg,ba(sbits[i]),v->bit[i],cont);
        free(cont);cont=n;
    }

    /* Build stages backwards so runtime executes bit 0 -> bit 7. */
    for(int i=7;i>=0;--i){
        BExpr *aa=ba(abits[i]);
        BExpr *bb=ba(bbits[i]);
        if(is_sub)bb=bnot(bb);
        BExpr *ci=ba(carry[i]);
        BExpr *p=bxor(aa,bb);
        BExpr *sum=bxor(p,ci);
        BExpr *co=bor(band(aa,bb),band(ci,p));
        char *cset=compile_set_bit(cg,co,carry[i+1],cont);
        free(cont);
        char *sset=compile_set_bit(cg,sum,sbits[i],cset);
        free(cset);
        cont=sset;
    }

    /* Snapshot operands before any destination bit can change. */
    char *bentry=compile_materialize_bvec(cg,bvec,bbits,cont);
    free(cont);
    char *aentry=compile_materialize_bvec(cg,avec,abits,bentry);
    free(bentry);

    for(int i=0;i<8;++i){free(abits[i]);free(bbits[i]);free(sbits[i]);}
    for(int i=0;i<9;++i)free(carry[i]);
    return aentry;
}


/* Runtime-safe shift-and-add multiplication. Keep all intermediate state in
 * shallow alias latches so compile time and CS2 dispatch depth stay bounded. */
static char *compile_assign_u8_mul(Codegen *cg,CGFunction *fn,CGVar *v,const ScmdExpr *expr,const char *next){
    BVec avec=build_u8(cg,fn,expr->as.binary.lhs);
    BVec bvec=build_u8(cg,fn,expr->as.binary.rhs);
    size_t id=cg->next_tmp++;
    char *abits[8],*bbits[8],*acc[8],*carry[8][9];
    for(int i=0;i<8;++i){
        abits[i]=scmd_format("__scmd_t%zu_ma%d",id,i);
        bbits[i]=scmd_format("__scmd_t%zu_mb%d",id,i);
        acc[i]=scmd_format("__scmd_t%zu_mr%d",id,i);
        emit_alias(cg,abits[i],"__scmd_false");
        emit_alias(cg,bbits[i],"__scmd_false");
        emit_alias(cg,acc[i],"__scmd_false");
        for(int k=0;k<9;++k){
            carry[i][k]=scmd_format("__scmd_t%zu_mc%d_%d",id,i,k);
            emit_alias(cg,carry[i][k],"__scmd_false");
        }
    }

    /* Commit only after the full product is available, preserving overlap such
     * as a = a * b. */
    char *cont=scmd_strdup(next);
    for(int i=7;i>=0;--i){
        char *n=compile_set_bit(cg,ba(acc[i]),v->bit[i],cont);
        free(cont);cont=n;
    }

    /* Build partial additions backwards. Runtime order is multiplier bit 0..7,
     * and within each addition bit 0..7. Carry must be written before acc[i]
     * because carry-out reads the old accumulator bit. */
    for(int j=7;j>=0;--j){
        for(int i=7;i>=0;--i){
            BExpr *aa=ba(acc[i]);
            BExpr *add=(i>=j)?band(ba(bbits[j]),ba(abits[i-j])):bc(false);
            BExpr *ci=ba(carry[j][i]);
            BExpr *p=bxor(aa,add);
            BExpr *sum=bxor(p,ci);
            BExpr *co=bor(band(aa,add),band(ci,p));
            char *sset=compile_set_bit(cg,sum,acc[i],cont);
            free(cont);
            char *cset=compile_set_bit(cg,co,carry[j][i+1],sset);
            free(sset);
            cont=cset;
        }
    }

    /* acc is mutable across calls, so reset it every invocation. */
    for(int i=7;i>=0;--i){
        char *n=compile_set_bit(cg,bc(false),acc[i],cont);
        free(cont);cont=n;
    }
    char *bentry=compile_materialize_bvec(cg,bvec,bbits,cont);
    free(cont);
    char *aentry=compile_materialize_bvec(cg,avec,abits,bentry);
    free(bentry);

    for(int i=0;i<8;++i){
        free(abits[i]);free(bbits[i]);free(acc[i]);
        for(int k=0;k<9;++k)free(carry[i][k]);
    }
    return aentry;
}

/* Runtime-safe restoring division. The same eight-bit difference/carry latches
 * are reused for each of the eight long-division rounds. Division by zero is
 * deliberately defined as quotient=0, remainder=dividend, matching SCMD's
 * documented deterministic target semantics. */
static char *compile_assign_u8_divmod(Codegen *cg,CGFunction *fn,CGVar *v,const ScmdExpr *expr,const char *next,bool want_mod){
    BVec avec=build_u8(cg,fn,expr->as.binary.lhs);
    BVec bvec=build_u8(cg,fn,expr->as.binary.rhs);
    size_t id=cg->next_tmp++;
    char *abits[8],*bbits[8],*rem[8],*quot[8],*diff[8],*carry[9];
    char *nonzero=scmd_format("__scmd_t%zu_dnz",id);
    emit_alias(cg,nonzero,"__scmd_false");
    for(int i=0;i<8;++i){
        abits[i]=scmd_format("__scmd_t%zu_da%d",id,i);
        bbits[i]=scmd_format("__scmd_t%zu_db%d",id,i);
        rem[i]=scmd_format("__scmd_t%zu_dr%d",id,i);
        quot[i]=scmd_format("__scmd_t%zu_dq%d",id,i);
        diff[i]=scmd_format("__scmd_t%zu_dd%d",id,i);
        emit_alias(cg,abits[i],"__scmd_false");
        emit_alias(cg,bbits[i],"__scmd_false");
        emit_alias(cg,rem[i],"__scmd_false");
        emit_alias(cg,quot[i],"__scmd_false");
        emit_alias(cg,diff[i],"__scmd_false");
    }
    for(int i=0;i<9;++i){
        carry[i]=scmd_format("__scmd_t%zu_dc%d",id,i);
        emit_alias(cg,carry[i],i==0?"__scmd_true":"__scmd_false");
    }

    char *cont=scmd_strdup(next);
    char **result=want_mod?rem:quot;
    for(int i=7;i>=0;--i){
        char *n=compile_set_bit(cg,ba(result[i]),v->bit[i],cont);
        free(cont);cont=n;
    }

    /* Prepend rounds in reverse build order so runtime consumes dividend bits
     * 7..0, exactly like binary long division. */
    for(int k=0;k<8;++k){
        char *after_round=cont;

        /* carry[8] means rem >= divisor for rem + ~divisor + 1. */
        BExpr *take=band(ba(nonzero),ba(carry[8]));
        char *true_path=scmd_strdup(after_round);
        for(int i=7;i>=0;--i){
            char *n=compile_set_bit(cg,ba(diff[i]),rem[i],true_path);
            free(true_path);true_path=n;
        }
        {
            char *n=compile_set_bit(cg,bc(true),quot[k],true_path);
            free(true_path);true_path=n;
        }
        char *false_path=compile_set_bit(cg,bc(false),quot[k],after_round);
        char *branch=compile_bexpr(cg,take,true_path,false_path);
        free(true_path);free(false_path);free(after_round);
        cont=branch;

        /* Compute rem - divisor into diff without mutating rem. */
        for(int i=7;i>=0;--i){
            BExpr *aa=ba(rem[i]);
            BExpr *bb=bnot(ba(bbits[i]));
            BExpr *ci=ba(carry[i]);
            BExpr *p=bxor(aa,bb);
            BExpr *sum=bxor(p,ci);
            BExpr *co=bor(band(aa,bb),band(ci,p));
            char *cset=compile_set_bit(cg,co,carry[i+1],cont);
            free(cont);
            char *sset=compile_set_bit(cg,sum,diff[i],cset);
            free(cset);
            cont=sset;
        }

        /* rem = (rem << 1) | dividend[k]. Runtime must write high->low. */
        for(int i=0;i<8;++i){
            BExpr *src=(i==0)?ba(abits[k]):ba(rem[i-1]);
            char *n=compile_set_bit(cg,src,rem[i],cont);
            free(cont);cont=n;
        }
    }

    /* Initialize per-call state, then snapshot operands. */
    for(int i=7;i>=0;--i){
        char *n=compile_set_bit(cg,bc(false),rem[i],cont);
        free(cont);cont=n;
    }
    BExpr *nz=bc(false);
    for(int i=0;i<8;++i)nz=bor(nz,ba(bbits[i]));
    {
        char *n=compile_set_bit(cg,nz,nonzero,cont);
        free(cont);cont=n;
    }
    char *bentry=compile_materialize_bvec(cg,bvec,bbits,cont);
    free(cont);
    char *aentry=compile_materialize_bvec(cg,avec,abits,bentry);
    free(bentry);

    free(nonzero);
    for(int i=0;i<8;++i){free(abits[i]);free(bbits[i]);free(rem[i]);free(quot[i]);free(diff[i]);}
    for(int i=0;i<9;++i)free(carry[i]);
    return aentry;
}

static char *compile_assign_u8_const(Codegen *cg,CGVar *v,uint8_t value,const char *next){
    char *cont=scmd_strdup(next);
    for(int i=7;i>=0;--i){char*n=compile_set_bit(cg,bc(((value>>i)&1u)!=0),v->bit[i],cont);free(cont);cont=n;}
    return cont;
}

static char *compile_assign_var(Codegen *cg,CGFunction *fn,CGVar *v,const ScmdExpr *expr,const char *next){
    if(v->type==SCMD_TYPE_BOOL)return compile_set_bit(cg,build_bool(cg,fn,expr),v->bit[0],next);
    if(cg->options.optimize){uint8_t cv;if(eval_const_u8(expr,&cv))return compile_assign_u8_const(cg,v,cv,next);}
    if(expr&&expr->kind==EXPR_BINARY){
        if(expr->as.binary.op==BIN_ADD||expr->as.binary.op==BIN_SUB)
            return compile_assign_u8_addsub(cg,fn,v,expr,next);
        if(expr->as.binary.op==BIN_MUL)
            return compile_assign_u8_mul(cg,fn,v,expr,next);
        if(expr->as.binary.op==BIN_DIV||expr->as.binary.op==BIN_MOD)
            return compile_assign_u8_divmod(cg,fn,v,expr,next,expr->as.binary.op==BIN_MOD);
    }
    BVec val=build_u8(cg,fn,expr);
    char *tmp[8];size_t tmpid=cg->next_tmp++;
    for(int i=0;i<8;++i){tmp[i]=scmd_format("__scmd_t%zu_b%d",tmpid,i);emit_alias(cg,tmp[i],"__scmd_false");}
    char*cont=scmd_strdup(next);
    for(int i=7;i>=0;--i){char*n=compile_set_bit(cg,ba(tmp[i]),v->bit[i],cont);free(cont);cont=n;}
    for(int i=7;i>=0;--i){char*n=compile_set_bit(cg,val.b[i],tmp[i],cont);free(cont);cont=n;}
    for(int i=0;i<8;++i)free(tmp[i]);return cont;
}

static bool unsafe_alias_body_text(const char*s){for(;*s;++s)if(*s=='"'||*s=='\n'||*s=='\r')return true;return false;}
static bool unsafe_echo_text(const char*s){return unsafe_alias_body_text(s)||strchr(s,';')!=NULL;}

static char *worker_path(Codegen *cg,size_t id){char*dir=path_dirname(cg->output_path);char*stem=path_stem(cg->output_path);char*ret;if(cg->options.organized_output){char*d=scmd_format("%s/async",dir);mkdirs(d);ret=scmd_format("%s/%03zu.cfg",d,id);free(d);}else{char*d=scmd_format("%s/%s.async",dir,stem);mkdirs(d);ret=scmd_format("%s/%03zu.cfg",d,id);free(d);}free(dir);free(stem);return ret;}
static char *worker_ref(Codegen *cg,size_t id){if(cg->options.exec_prefix&&cg->options.exec_prefix[0])return scmd_format("%s/async/%03zu.cfg",cg->options.exec_prefix,id);char*stem=path_stem(cg->output_path);char*r=scmd_format("%s.async/%03zu.cfg",stem,id);free(stem);return r;}
static const char *add_worker(Codegen *cg,const char *entry,int delay,bool clear_first){if(cg->workers.len==cg->workers.cap){size_t nc=cg->workers.cap?cg->workers.cap*2u:8u;cg->workers.items=(AsyncWorker*)realloc(cg->workers.items,nc*sizeof(*cg->workers.items));cg->workers.cap=nc;}size_t id=cg->workers.len;AsyncWorker*w=&cg->workers.items[cg->workers.len++];memset(w,0,sizeof(*w));w->file_path=worker_path(cg,id);w->exec_ref=worker_ref(cg,id);w->entry_alias=scmd_strdup(entry);w->delay_ms=delay;w->clear_first=clear_first;return w->exec_ref;}

static char *compile_stmt_list(Codegen*,CGFunction*,CGBlock*,const ScmdStmt*,const char*);
static char *compile_stmt(Codegen *cg,CGFunction *fn,CGBlock *blk,const ScmdStmt *s,const char *next){
    switch(s->kind){
        case STMT_VAR_DECL:{CGVar*v=resolve_var(cg,fn,s->as.var_decl.name);return v?compile_assign_var(cg,fn,v,s->as.var_decl.init,next):scmd_strdup(next);}
        case STMT_ASSIGN:{CGVar*v=resolve_var(cg,fn,s->as.assign.name);if(!v)return scmd_strdup(next);const ScmdExpr*rhs=s->as.assign.value;if(s->as.assign.op==ASSIGN_SET)return compile_assign_var(cg,fn,v,rhs,next);
            /* Desugar compound assignment into a synthetic binary expression. */
            ScmdExpr lhs={0},bin={0};lhs.kind=EXPR_IDENT;lhs.inferred_type=v->type;lhs.as.name=(char*)s->as.assign.name;bin.kind=EXPR_BINARY;bin.inferred_type=v->type;bin.as.binary.lhs=&lhs;bin.as.binary.rhs=(ScmdExpr*)rhs;
            switch(s->as.assign.op){case ASSIGN_ADD:bin.as.binary.op=BIN_ADD;break;case ASSIGN_SUB:bin.as.binary.op=BIN_SUB;break;case ASSIGN_MUL:bin.as.binary.op=BIN_MUL;break;case ASSIGN_DIV:bin.as.binary.op=BIN_DIV;break;case ASSIGN_MOD:bin.as.binary.op=BIN_MOD;break;case ASSIGN_BIT_AND:bin.as.binary.op=BIN_BIT_AND;break;case ASSIGN_BIT_OR:bin.as.binary.op=BIN_BIT_OR;break;case ASSIGN_BIT_XOR:bin.as.binary.op=BIN_BIT_XOR;break;case ASSIGN_SHL:bin.as.binary.op=BIN_SHL;break;case ASSIGN_SHR:bin.as.binary.op=BIN_SHR;break;default:bin.as.binary.op=BIN_ADD;break;}return compile_assign_var(cg,fn,v,&bin,next);}
        case STMT_IF:{if(cg->options.optimize){bool cv;if(eval_const_bool(s->as.if_stmt.cond,&cv)){const ScmdStmt*chosen=cv?(s->as.if_stmt.then_block?s->as.if_stmt.then_block->as.block_scope.first:NULL):(s->as.if_stmt.else_block?s->as.if_stmt.else_block->as.block_scope.first:NULL);return compile_stmt_list(cg,fn,blk,chosen,next);}}char*t=compile_stmt_list(cg,fn,blk,s->as.if_stmt.then_block?s->as.if_stmt.then_block->as.block_scope.first:NULL,next);char*f=s->as.if_stmt.else_block?compile_stmt_list(cg,fn,blk,s->as.if_stmt.else_block->as.block_scope.first,next):scmd_strdup(next);char*e=compile_bexpr(cg,build_bool(cg,fn,s->as.if_stmt.cond),t,f);free(t);free(f);return e;}
        case STMT_WHILE:{char*cond=new_label(cg);char*body=compile_stmt_list(cg,fn,blk,s->as.while_stmt.body?s->as.while_stmt.body->as.block_scope.first:NULL,cond);char*ce=compile_bexpr(cg,build_bool(cg,fn,s->as.while_stmt.cond),body,next);emit_alias(cg,cond,ce);free(body);free(ce);return cond;}
        case STMT_FOR:{char*cond=new_label(cg);char*step=s->as.for_stmt.step?compile_stmt(cg,fn,blk,s->as.for_stmt.step,cond):scmd_strdup(cond);char*body=compile_stmt_list(cg,fn,blk,s->as.for_stmt.body?s->as.for_stmt.body->as.block_scope.first:NULL,step);char*ce=s->as.for_stmt.cond?compile_bexpr(cg,build_bool(cg,fn,s->as.for_stmt.cond),body,next):scmd_strdup(body);emit_alias(cg,cond,ce);free(step);free(body);free(ce);if(s->as.for_stmt.init){char*entry=compile_stmt(cg,fn,blk,s->as.for_stmt.init,cond);free(cond);return entry;}return cond;}
        case STMT_BUILTIN:{
            if(s->as.builtin.kind==BUILTIN_CONSOLE_CLEAR){char*l=new_label(cg);if(cg->options.console_mode==SCMD_CONSOLE_ASYNC){const char*r=add_worker(cg,next,cg->options.console_settle_ms,true);char*b=scmd_format("exec_async %s",r);emit_alias(cg,l,b);free(b);}else{char*b=scmd_format("clear;%s",next);emit_alias(cg,l,b);free(b);}return l;}
            const char*prefix=s->as.builtin.kind==BUILTIN_CONSOLE_PRINT?"echoln ":s->as.builtin.kind==BUILTIN_CHAT_SEND?"say ":s->as.builtin.kind==BUILTIN_TEAMCHAT_SEND?"say_team ":"";
            if(s->as.builtin.kind==BUILTIN_CONSOLE_PRINT&&unsafe_echo_text(s->as.builtin.text)){scmd_error_at(cg->source_path,s->line,s->col,"console.print text cannot contain quote, newline, or ';'");cg->errors++;return scmd_strdup(next);}if(unsafe_alias_body_text(s->as.builtin.text)){scmd_error_at(cg->source_path,s->line,s->col,"text cannot contain quote or newline");cg->errors++;return scmd_strdup(next);}
            char*l=new_label(cg);char*b=s->as.builtin.kind==BUILTIN_COMMAND_EXEC?scmd_format("%s;%s",s->as.builtin.text,next):scmd_format("%s%s;%s",prefix,s->as.builtin.text,next);emit_alias_ex(cg,l,b,s->as.builtin.kind==BUILTIN_COMMAND_EXEC);free(b);return l;}
        case STMT_WAIT:{uint64_t ms=s->as.wait_stmt.amount;if(s->as.wait_stmt.unit==WAIT_SECONDS)ms*=1000u;else if(s->as.wait_stmt.unit==WAIT_TICKS)ms*=(uint64_t)cg->options.tick_ms;if(ms>2147483647u)ms=2147483647u;char*l=new_label(cg);const char*r=add_worker(cg,next,(int)ms,false);char*b=scmd_format("exec_async %s",r);emit_alias(cg,l,b);free(b);return l;}
        case STMT_RETURN:{char*l=new_label(cg);const char*r=fn?(strcmp(fn->ast->name,"main")==0?"__scmd_halt":fn->ret_alias):(blk?blk->ret_alias:"__scmd_halt");emit_alias(cg,l,r);return l;}
        case STMT_CALL:{CGFunction*t=resolve_function(cg,s->as.call.name);if(!t)return scmd_strdup(next);char*l=new_label(cg),*b=scmd_format("alias %s %s;%s",t->ret_alias,next,t->entry_alias);emit_alias(cg,l,b);free(b);return l;}
        case STMT_BLOCK_SCOPE:return compile_stmt_list(cg,fn,blk,s->as.block_scope.first,next);
        case STMT_RECORD:{if(blk){CGRecord*r=resolve_record(blk,s->as.record.name);if(r)emit_alias(cg,r->alias_name,next);}return scmd_strdup(next);}
        case STMT_JUMP:{if(blk){CGRecord*r=resolve_record(blk,s->as.jump.record_name);if(r)return scmd_strdup(r->alias_name);}return scmd_strdup(next);}
        case STMT_BLOCK_CALL:{CGBlock*b=resolve_block(cg,s->as.block_call.block_name);if(!b)return scmd_strdup(next);const char*entry=b->entry_alias;if(s->as.block_call.kind==BLOCK_CALL_JUMP){CGRecord*r=resolve_record(b,s->as.block_call.record_a);if(r)entry=r->alias_name;}char*l=new_label(cg),*body=scmd_format("alias %s %s;%s",b->ret_alias,next,entry);emit_alias(cg,l,body);free(body);return l;}
    }
    return scmd_strdup(next);
}
static char *compile_stmt_list(Codegen*cg,CGFunction*fn,CGBlock*blk,const ScmdStmt*first,const char*next){size_t n=0;for(const ScmdStmt*s=first;s;s=s->next)n++;if(!n)return scmd_strdup(next);const ScmdStmt**it=(const ScmdStmt**)malloc(n*sizeof(*it));size_t i=0;for(const ScmdStmt*s=first;s;s=s->next)it[i++]=s;char*cont=scmd_strdup(next);for(size_t k=n;k-->0;){char*e=compile_stmt(cg,fn,blk,it[k],cont);free(cont);cont=e;}free(it);return cont;}

static bool const_global_init(Codegen*cg,const ScmdGlobal*g,CGVar*v){if(v->type==SCMD_TYPE_BOOL){bool b;if(!eval_const_bool(g->init,&b)){scmd_error_at(cg->source_path,g->line,g->col,"global bool initializer must be a compile-time constant");return false;}emit_alias(cg,v->bit[0],b?"__scmd_true":"__scmd_false");return true;}uint8_t x;if(!eval_const_u8(g->init,&x)){scmd_error_at(cg->source_path,g->line,g->col,"global u8 initializer must be a compile-time constant");return false;}for(int i=0;i<8;++i)emit_alias(cg,v->bit[i],((x>>i)&1u)?"__scmd_true":"__scmd_false");return true;}

typedef struct CfgOptSlot { const char *key; size_t index; } CfgOptSlot;
typedef struct CfgOptMap { CfgOptSlot *slots; size_t cap; } CfgOptMap;
typedef struct CfgOptRefScan {
    const CfgOptMap *map;
    const size_t *redirect;
    size_t count;
    bool *live;
    size_t *queue;
    size_t *queue_len;
    size_t queue_cap;
    size_t *incoming;
    size_t wanted;
    bool found;
    bool repair;
} CfgOptRefScan;
typedef struct CfgOptBuf { char *data; size_t len,cap; } CfgOptBuf;
typedef struct CfgOptTokenSlot { const char *start; size_t len,hash; bool used; } CfgOptTokenSlot;
typedef struct CfgOptTokenSet { CfgOptTokenSlot *slots; size_t cap,used; } CfgOptTokenSet;

#define CFG_OPT_NONE ((size_t)-1)

static size_t cfg_opt_hash(const char *s){size_t h=(size_t)2166136261u;for(;*s;++s)h=(h^(unsigned char)*s)*(size_t)16777619u;return h;}
static size_t cfg_opt_hash_span(const char *s,size_t n){size_t h=(size_t)2166136261u;for(size_t i=0;i<n;++i)h=(h^(unsigned char)s[i])*(size_t)16777619u;return h;}
static bool cfg_opt_name_char(unsigned char c){return c>=128u||isalnum(c)||c=='_';}
static const char *cfg_opt_skip_space(const char *p,const char *end){while(p<end&&isspace((unsigned char)*p))++p;return p;}
static const char *cfg_opt_word_end(const char *p,const char *end){while(p<end&&cfg_opt_name_char((unsigned char)*p))++p;return p;}
static bool cfg_opt_word_is(const char *p,const char *end,const char *word){size_t n=strlen(word);return (size_t)(end-p)==n&&memcmp(p,word,n)==0;}
static bool cfg_opt_passthrough(const char *p,const char *end){return cfg_opt_word_is(p,end,"echoln")||cfg_opt_word_is(p,end,"say")||cfg_opt_word_is(p,end,"say_team")||cfg_opt_word_is(p,end,"exec_async")||cfg_opt_word_is(p,end,"exec")||cfg_opt_word_is(p,end,"sleep")||cfg_opt_word_is(p,end,"clear");}

static bool cfg_opt_map_init(CfgOptMap *m,const AliasVec *defs){size_t cap=1,need=defs->len>=(size_t)-1/2?(size_t)-1:defs->len*2u+1u;while(cap<need){if(cap>(size_t)-1/2)return false;cap*=2u;}m->slots=(CfgOptSlot*)calloc(cap,sizeof(*m->slots));if(!m->slots)return false;m->cap=cap;for(size_t i=0;i<defs->len;++i){size_t pos=cfg_opt_hash(defs->items[i].name)&(cap-1u);while(m->slots[pos].key&&strcmp(m->slots[pos].key,defs->items[i].name)!=0)pos=(pos+1u)&(cap-1u);m->slots[pos].key=defs->items[i].name;m->slots[pos].index=i;}return true;}
static void cfg_opt_map_dispose(CfgOptMap *m){free(m->slots);m->slots=NULL;m->cap=0;}
static bool cfg_opt_map_find_span(const CfgOptMap *m,const char *s,size_t n,size_t *out){if(!m||!m->slots||!m->cap)return false;size_t pos=cfg_opt_hash_span(s,n)&(m->cap-1u);for(;;){const char *key=m->slots[pos].key;if(!key)return false;if(strlen(key)==n&&memcmp(key,s,n)==0){if(out)*out=m->slots[pos].index;return true;}pos=(pos+1u)&(m->cap-1u);}}
static size_t cfg_opt_redirect(const size_t *redirect,size_t count,size_t index){if(!redirect||index>=count)return index;for(size_t i=0;i<count;++i){size_t next=redirect[index];if(next==CFG_OPT_NONE||next>=count)return index;if(next==index)return index;index=next;}return index;}
static bool cfg_opt_is_internal(const char *name){return strncmp(name,"__scmd_",7)==0;}
static bool cfg_opt_is_function_entry(const char *name){if(strncmp(name,"__scmd_fn",9)!=0||!name[9])return false;for(const char *p=name+9;*p;++p)if(!isdigit((unsigned char)*p))return false;return true;}

static bool cfg_opt_last_token(const char *body,const char **out_start,const char **out_end);
static void cfg_opt_scan_all_words(const char *begin,const char *end,const CfgOptMap *map,bool *preserve){const char *p=begin;while(p<end){while(p<end&&!cfg_opt_name_char((unsigned char)*p))++p;const char *q=p;while(q<end&&cfg_opt_name_char((unsigned char)*q))++q;if(q>p&&preserve){size_t index;if(cfg_opt_map_find_span(map,p,(size_t)(q-p),&index))preserve[index]=true;}p=q;}}
static void cfg_opt_scan_metadata(const AliasDef *def,const CfgOptMap *map,bool *mutable_name,bool *preserve){const char *body=def->body,*last=body+strlen(body),*tail_start=NULL,*tail_end=NULL;bool has_tail=cfg_opt_last_token(body,&tail_start,&tail_end);const char *seg=body;while(seg<last){const char *end=(const char*)memchr(seg,';', (size_t)(last-seg));if(!end)end=last;const char *p=cfg_opt_skip_space(seg,end),*q=cfg_opt_word_end(p,end);if(def->opaque&&(!has_tail||p!=tail_start||q!=tail_end||cfg_opt_skip_space(q,end)!=end))cfg_opt_scan_all_words(p,end,map,preserve);if(cfg_opt_word_is(p,q,"alias")){const char *target=cfg_opt_skip_space(q,end),*target_end=cfg_opt_word_end(target,end);size_t index;if(cfg_opt_map_find_span(map,target,(size_t)(target_end-target),&index))mutable_name[index]=true;}if(end==last)break;seg=end+1u;}}

static void cfg_opt_note_ref(CfgOptRefScan *scan,size_t index){if(scan->redirect)index=cfg_opt_redirect(scan->redirect,scan->count,index);if(index>=scan->count)return;if(scan->wanted!=CFG_OPT_NONE&&index==scan->wanted)scan->found=true;if(scan->incoming)scan->incoming[index]++;if(scan->repair&&!scan->live[index]&&(!scan->redirect||scan->redirect[index]==CFG_OPT_NONE))scan->live[index]=true;if(scan->live&&scan->queue&&scan->queue_len&&!scan->live[index]){scan->live[index]=true;if(*scan->queue_len<scan->queue_cap)scan->queue[(*scan->queue_len)++]=index;}}
static void cfg_opt_scan_body_refs(const AliasDef *def,CfgOptRefScan *scan){const char *body=def->body,*last=body+strlen(body),*seg=body;while(seg<last){const char *end=(const char*)memchr(seg,';', (size_t)(last-seg));if(!end)end=last;const char *p=cfg_opt_skip_space(seg,end),*q=cfg_opt_word_end(p,end);if(def->opaque){cfg_opt_scan_all_words(p,end,scan->map,NULL);const char *r=p;while(r<end){while(r<end&&!cfg_opt_name_char((unsigned char)*r))++r;const char *a=r;while(r<end&&cfg_opt_name_char((unsigned char)*r))++r;if(r>a){size_t index;if(cfg_opt_map_find_span(scan->map,a,(size_t)(r-a),&index))cfg_opt_note_ref(scan,index);}}}else if(cfg_opt_word_is(p,q,"alias")){const char *value=cfg_opt_skip_space(cfg_opt_word_end(cfg_opt_skip_space(q,end),end),end),*value_end=cfg_opt_word_end(value,end);size_t index;if(cfg_opt_map_find_span(scan->map,value,(size_t)(value_end-value),&index))cfg_opt_note_ref(scan,index);}else if(!cfg_opt_passthrough(p,q)){size_t index;if(cfg_opt_map_find_span(scan->map,p,(size_t)(q-p),&index))cfg_opt_note_ref(scan,index);}if(end==last)break;seg=end+1u;}}

static void cfg_opt_root_name(const CfgOptMap *map,const char *name,bool *live,bool *force,size_t *queue,size_t *queue_len,size_t queue_cap){size_t index;if(!cfg_opt_map_find_span(map,name,strlen(name),&index))return;force[index]=true;if(!live[index]&&*queue_len<queue_cap){live[index]=true;queue[(*queue_len)++]=index;}}
static void cfg_opt_root_var(const CfgOptMap *map,CGVar *v,bool *live,bool *force,size_t *queue,size_t *queue_len,size_t queue_cap){int n=v->type==SCMD_TYPE_U8?8:1;for(int i=0;i<n;++i)cfg_opt_root_name(map,v->bit[i],live,force,queue,queue_len,queue_cap);}

static bool cfg_opt_single_token(const char *body,const char **out_start,const char **out_end){const char *last=body+strlen(body),*p=cfg_opt_skip_space(body,last),*q=cfg_opt_word_end(p,last);if(p==q||cfg_opt_skip_space(q,last)!=last)return false;if(out_start)*out_start=p;if(out_end)*out_end=q;return true;}
static bool cfg_opt_last_token(const char *body,const char **out_start,const char **out_end){const char *last=body+strlen(body),*seg=body,*found=NULL,*found_end=NULL;while(seg<last){const char *end=(const char*)memchr(seg,';', (size_t)(last-seg));if(!end)end=last;const char *p=cfg_opt_skip_space(seg,end),*q=cfg_opt_word_end(p,end);if(p<q&&cfg_opt_skip_space(q,end)==end){found=p;found_end=q;}if(end==last)break;seg=end+1u;}if(!found)return false;if(out_start)*out_start=found;if(out_end)*out_end=found_end;return true;}
static bool cfg_opt_body_refs_index(const AliasDef *def,const CfgOptMap *map,const size_t *redirect,size_t count,size_t wanted){CfgOptRefScan scan={map,redirect,count,NULL,NULL,NULL,0,NULL,wanted,false,false};cfg_opt_scan_body_refs(def,&scan);return scan.found;}

static bool cfg_opt_forward_pass(const AliasVec *defs,const CfgOptMap *map,bool *live,const bool *force,const bool *mutable_name,const bool *preserve,size_t *redirect){bool changed=false,again=true;while(again){again=false;for(size_t i=0;i<defs->len;++i){if(!live[i]||force[i]||mutable_name[i]||preserve[i]||defs->items[i].opaque)continue;const char *start,*end;if(!cfg_opt_single_token(defs->items[i].body,&start,&end))continue;size_t target;if(!cfg_opt_map_find_span(map,start,(size_t)(end-start),&target))continue;target=cfg_opt_redirect(redirect,defs->len,target);if(target==i||target>=defs->len||!live[target])continue;redirect[i]=target;live[i]=false;changed=again=true;}}return changed;}

typedef struct CfgOptBodySlot { size_t hash,index; bool used; } CfgOptBodySlot;
static size_t cfg_opt_hash_text(const char *s){size_t h=(size_t)2166136261u;for(;*s;++s)h=(h^(unsigned char)*s)*(size_t)16777619u;return h;}
static bool cfg_opt_dedup_pass(const AliasVec *defs,const CfgOptMap *map,bool *live,const bool *force,const bool *mutable_name,const bool *preserve,size_t *redirect){size_t cap=1,need=defs->len>=(size_t)-1/2?(size_t)-1:defs->len*2u+1u;while(cap<need){if(cap>(size_t)-1/2)return false;cap*=2u;}CfgOptBodySlot *slots=(CfgOptBodySlot*)calloc(cap,sizeof(*slots));if(!slots)return false;bool changed=false;size_t halt=CFG_OPT_NONE;cfg_opt_map_find_span(map,"__scmd_halt",11u,&halt);for(size_t i=0;i<defs->len;++i){if(!live[i]||force[i]||mutable_name[i]||preserve[i]||defs->items[i].opaque)continue;const char *body=defs->items[i].body;if(!*body&&halt!=CFG_OPT_NONE&&halt!=i&&live[halt]){redirect[i]=halt;live[i]=false;changed=true;continue;}size_t pos=cfg_opt_hash_text(body)&(cap-1u);for(;;){if(!slots[pos].used){slots[pos].used=true;slots[pos].hash=cfg_opt_hash_text(body);slots[pos].index=i;break;}if(slots[pos].hash==cfg_opt_hash_text(body)&&strcmp(defs->items[slots[pos].index].body,body)==0){size_t target=cfg_opt_redirect(redirect,defs->len,slots[pos].index);if(target!=i&&live[target]){redirect[i]=target;live[i]=false;changed=true;}break;}pos=(pos+1u)&(cap-1u);}}free(slots);return changed;}

static bool cfg_opt_fuse_pass(Codegen *cg,const AliasVec *defs,const CfgOptMap *map,bool *live,const bool *force,const bool *mutable_name,const bool *preserve,size_t *redirect){size_t *incoming=(size_t*)calloc(defs->len,sizeof(*incoming));if(!incoming)return false;for(size_t i=0;i<defs->len;++i)if(live[i]){CfgOptRefScan scan={map,redirect,defs->len,NULL,NULL,NULL,0,incoming,CFG_OPT_NONE,false,false};cfg_opt_scan_body_refs(&defs->items[i],&scan);}bool changed=false;size_t max_body=cg->options.page_bytes>256u?cg->options.page_bytes-256u:1024u;for(size_t i=0;i<defs->len;++i){if(!live[i]||defs->items[i].opaque)continue;const char *start,*end;if(!cfg_opt_last_token(defs->items[i].body,&start,&end)||!strchr(defs->items[i].body,';'))continue;size_t target;if(!cfg_opt_map_find_span(map,start,(size_t)(end-start),&target))continue;target=cfg_opt_redirect(redirect,defs->len,target);if(target==i||target>=defs->len||!live[target]||force[target]||mutable_name[target]||preserve[target]||defs->items[target].opaque||incoming[target]!=1)continue;if(!*defs->items[target].body||cfg_opt_body_refs_index(&defs->items[target],map,redirect,defs->len,i))continue;size_t prefix=(size_t)(start-defs->items[i].body),target_len=strlen(defs->items[target].body);if(prefix>max_body||target_len>max_body-prefix)continue;char *body=(char*)malloc(prefix+target_len+1u);if(!body)continue;memcpy(body,defs->items[i].body,prefix);memcpy(body+prefix,defs->items[target].body,target_len);body[prefix+target_len]='\0';free(defs->items[i].body);defs->items[i].body=body;live[target]=false;changed=true;}free(incoming);return changed;}

static bool cfg_opt_token_set_init(CfgOptTokenSet *set,size_t hint){size_t cap=1,need=hint>=(size_t)-1/8?(size_t)-1:hint*8u+1u;while(cap<need){if(cap>(size_t)-1/2)return false;cap*=2u;}set->slots=(CfgOptTokenSlot*)calloc(cap,sizeof(*set->slots));if(!set->slots)return false;set->cap=cap;set->used=0;return true;}
static bool cfg_opt_token_set_insert(CfgOptTokenSet *set,const char *start,size_t len){size_t hash=cfg_opt_hash_span(start,len),pos=hash&(set->cap-1u);for(;;){CfgOptTokenSlot *slot=&set->slots[pos];if(!slot->used){slot->used=true;slot->start=start;slot->len=len;slot->hash=hash;set->used++;return true;}if(slot->hash==hash&&slot->len==len&&memcmp(slot->start,start,len)==0)return true;pos=(pos+1u)&(set->cap-1u);}}
static bool cfg_opt_token_set_rehash(CfgOptTokenSet *set,size_t new_cap){CfgOptTokenSlot *old=set->slots;size_t old_cap=set->cap,old_used=set->used;CfgOptTokenSlot *slots=(CfgOptTokenSlot*)calloc(new_cap,sizeof(*slots));if(!slots)return false;set->slots=slots;set->cap=new_cap;set->used=0;for(size_t i=0;i<old_cap;++i)if(old[i].used&&!cfg_opt_token_set_insert(set,old[i].start,old[i].len)){free(set->slots);set->slots=old;set->cap=old_cap;set->used=old_used;return false;}free(old);return true;}
static bool cfg_opt_token_set_add(CfgOptTokenSet *set,const char *start,size_t len){if(set->used+1u>=set->cap/2u){if(set->cap>(size_t)-1/2||!cfg_opt_token_set_rehash(set,set->cap*2u))return false;}return cfg_opt_token_set_insert(set,start,len);}
static bool cfg_opt_token_set_add_text(CfgOptTokenSet *set,const char *text){size_t len=strlen(text);for(size_t i=0;i<len;){while(i<len&&!cfg_opt_name_char((unsigned char)text[i]))++i;size_t start=i;while(i<len&&cfg_opt_name_char((unsigned char)text[i]))++i;if(i>start&&!cfg_opt_token_set_add(set,text+start,i-start))return false;}return true;}
static bool cfg_opt_token_set_build(CfgOptTokenSet *set,const AliasVec *defs){if(!cfg_opt_token_set_init(set,defs->len))return false;for(size_t i=0;i<defs->len;++i)if(!cfg_opt_token_set_add_text(set,defs->items[i].name)||!cfg_opt_token_set_add_text(set,defs->items[i].body)){free(set->slots);set->slots=NULL;set->cap=0;set->used=0;return false;}return true;}
static bool cfg_opt_token_set_has(const CfgOptTokenSet *set,const char *text){size_t len=strlen(text),hash=cfg_opt_hash_span(text,len),pos=hash&(set->cap-1u);for(;;){const CfgOptTokenSlot *slot=&set->slots[pos];if(!slot->used)return false;if(slot->hash==hash&&slot->len==len&&memcmp(slot->start,text,len)==0)return true;pos=(pos+1u)&(set->cap-1u);}}
static void cfg_opt_token_set_dispose(CfgOptTokenSet *set){free(set->slots);set->slots=NULL;set->cap=0;set->used=0;}
static _Thread_local CfgOptTokenSet cfg_opt_reserved_cache={0};
static _Thread_local const AliasDef *cfg_opt_reserved_items=NULL;
static _Thread_local size_t cfg_opt_reserved_len=0;
static bool cfg_opt_name_reserved(const AliasVec *defs,const char *candidate){if(cfg_opt_reserved_items!=defs->items||cfg_opt_reserved_len!=defs->len){cfg_opt_token_set_dispose(&cfg_opt_reserved_cache);cfg_opt_reserved_items=defs->items;cfg_opt_reserved_len=defs->len;if(!cfg_opt_token_set_build(&cfg_opt_reserved_cache,defs))return false;}if(!cfg_opt_reserved_cache.cap)return false;if(cfg_opt_token_set_has(&cfg_opt_reserved_cache,candidate))return true;cfg_opt_token_set_add(&cfg_opt_reserved_cache,candidate,strlen(candidate));return false;}
static bool cfg_opt_buf_append(CfgOptBuf *b,const char *s,size_t n){if(n>(size_t)-1-b->len-1u)return false;size_t need=b->len+n+1u;if(need>b->cap){size_t cap=b->cap?b->cap:128u;while(cap<need){if(cap>(size_t)-1/2)return false;cap*=2u;}char *p=(char*)realloc(b->data,cap);if(!p)return false;b->data=p;b->cap=cap;}memcpy(b->data+b->len,s,n);b->len+=n;b->data[b->len]='\0';return true;}
static bool cfg_opt_buf_append_word(CfgOptBuf *b,const char *start,const char *end,const CfgOptMap *map,const size_t *redirect,const char *const *rename,size_t count){size_t index;if(cfg_opt_map_find_span(map,start,(size_t)(end-start),&index)){index=cfg_opt_redirect(redirect,count,index);if(rename[index])return cfg_opt_buf_append(b,rename[index],strlen(rename[index]));}return cfg_opt_buf_append(b,start,(size_t)(end-start));}
static char *cfg_opt_rewrite_opaque_body(const AliasDef *def,const CfgOptMap *map,const size_t *redirect,const char *const *rename,size_t count){const char *start,*end;if(!cfg_opt_last_token(def->body,&start,&end))return scmd_strdup(def->body);CfgOptBuf out={0};if(!cfg_opt_buf_append(&out,def->body,(size_t)(start-def->body))||!cfg_opt_buf_append_word(&out,start,end,map,redirect,rename,count)||!cfg_opt_buf_append(&out,end,strlen(end)))goto fail;return out.data;fail:free(out.data);return NULL;}
static char *cfg_opt_rewrite_body(const AliasDef *def,const CfgOptMap *map,const size_t *redirect,const char *const *rename,size_t count){if(def->opaque)return cfg_opt_rewrite_opaque_body(def,map,redirect,rename,count);if(!def->body[0])return scmd_strdup(def->body);CfgOptBuf out={0};const char *body=def->body,*last=body+strlen(body),*seg=body;while(seg<last){const char *end=(const char*)memchr(seg,';', (size_t)(last-seg));if(!end)end=last;const char *p=cfg_opt_skip_space(seg,end),*q=cfg_opt_word_end(p,end);if(cfg_opt_word_is(p,q,"alias")){if(!cfg_opt_buf_append(&out,seg,(size_t)(p-seg))||!cfg_opt_buf_append(&out,p,(size_t)(q-p)))goto fail;const char *a=cfg_opt_skip_space(q,end);if(!cfg_opt_buf_append(&out,q,(size_t)(a-q)))goto fail;const char *ae=cfg_opt_word_end(a,end);if(!cfg_opt_buf_append_word(&out,a,ae,map,redirect,rename,count))goto fail;const char *v=cfg_opt_skip_space(ae,end);if(!cfg_opt_buf_append(&out,ae,(size_t)(v-ae)))goto fail;const char *ve=cfg_opt_word_end(v,end);if(!cfg_opt_buf_append_word(&out,v,ve,map,redirect,rename,count)||!cfg_opt_buf_append(&out,ve,(size_t)(end-ve)))goto fail;}else if(p<q&&!cfg_opt_passthrough(p,q)){if(!cfg_opt_buf_append(&out,seg,(size_t)(p-seg))||!cfg_opt_buf_append_word(&out,p,q,map,redirect,rename,count)||!cfg_opt_buf_append(&out,q,(size_t)(end-q)))goto fail;}else if(!cfg_opt_buf_append(&out,seg,(size_t)(end-seg)))goto fail;if(end<last&&!cfg_opt_buf_append(&out,";",1u))goto fail;if(end==last)break;seg=end+1u;}return out.data;fail:free(out.data);return NULL;}

static void cfg_opt_repair_refs(const AliasVec *defs,const CfgOptMap *map,bool *live,const size_t *redirect){for(;;){size_t before=0;for(size_t i=0;i<defs->len;++i)if(live[i])++before;for(size_t i=0;i<defs->len;++i)if(live[i]){CfgOptRefScan scan={map,redirect,defs->len,live,NULL,NULL,0,NULL,CFG_OPT_NONE,false,true};cfg_opt_scan_body_refs(&defs->items[i],&scan);}size_t after=0;for(size_t i=0;i<defs->len;++i)if(live[i])++after;if(after==before)break;}}

static void optimize_cfg(Codegen *cg){if(!cg->options.optimize||cg->defs.len<2u)return;CfgOptMap map={0};if(!cfg_opt_map_init(&map,&cg->defs))return;size_t n=cg->defs.len;bool *live=(bool*)calloc(n,sizeof(*live)),*force=(bool*)calloc(n,sizeof(*force)),*mutable_name=(bool*)calloc(n,sizeof(*mutable_name)),*preserve=(bool*)calloc(n,sizeof(*preserve));size_t *queue=(size_t*)malloc(n*sizeof(*queue)),*redirect=(size_t*)malloc(n*sizeof(*redirect));if(!live||!force||!mutable_name||!preserve||!queue||!redirect){free(live);free(force);free(mutable_name);free(preserve);free(queue);free(redirect);cfg_opt_map_dispose(&map);return;}for(size_t i=0;i<n;++i)redirect[i]=CFG_OPT_NONE;for(size_t i=0;i<n;++i)cfg_opt_scan_metadata(&cg->defs.items[i],&map,mutable_name,preserve);size_t queue_len=0;cfg_opt_root_name(&map,"__scmd_halt",live,force,queue,&queue_len,n);for(size_t i=0;i<cg->function_count;++i)cfg_opt_root_name(&map,cg->functions[i].entry_alias,live,force,queue,&queue_len,n);for(size_t i=0;i<cg->workers.len;++i)cfg_opt_root_name(&map,cg->workers.items[i].entry_alias,live,force,queue,&queue_len,n);for(size_t i=0;i<cg->global_count;++i)cfg_opt_root_var(&map,&cg->globals[i],live,force,queue,&queue_len,n);for(size_t i=0;i<cg->function_count;++i)for(size_t j=0;j<cg->functions[i].local_count;++j)cfg_opt_root_var(&map,&cg->functions[i].locals[j],live,force,queue,&queue_len,n);while(queue_len){size_t index=queue[--queue_len];CfgOptRefScan scan={&map,redirect,n,live,queue,&queue_len,n,NULL,CFG_OPT_NONE,false,false};cfg_opt_scan_body_refs(&cg->defs.items[index],&scan);}for(size_t round=0;round<16u;++round){bool changed=cfg_opt_forward_pass(&cg->defs,&map,live,force,mutable_name,preserve,redirect);if(cfg_opt_dedup_pass(&cg->defs,&map,live,force,mutable_name,preserve,redirect))changed=true;if(cfg_opt_fuse_pass(cg,&cg->defs,&map,live,force,mutable_name,preserve,redirect))changed=true;if(!changed)break;}cfg_opt_repair_refs(&cg->defs,&map,live,redirect);const char **rename=(const char**)calloc(n,sizeof(*rename));if(!rename)goto optimize_cleanup;size_t serial=0;for(size_t i=0;i<n;++i){if(!live[i]||force[i]||preserve[i]||!cfg_opt_is_internal(cg->defs.items[i].name)||cfg_opt_is_function_entry(cg->defs.items[i].name))continue;char *candidate=NULL;do{free(candidate);candidate=scmd_format("__s%zu",serial++);}while(candidate&&cfg_opt_name_reserved(&cg->defs,candidate));if(!candidate)goto optimize_cleanup;rename[i]=candidate;}char **rewritten=(char**)calloc(n,sizeof(*rewritten));if(!rewritten)goto optimize_cleanup;size_t kept=0;for(size_t i=0;i<n;++i)if(live[i]){rewritten[i]=cfg_opt_rewrite_body(&cg->defs.items[i],&map,redirect,rename,n);if(!rewritten[i]){for(size_t j=0;j<n;++j)free(rewritten[j]);free(rewritten);goto optimize_cleanup;}++kept;}AliasDef *new_defs=(AliasDef*)calloc(kept?kept:1u,sizeof(*new_defs));if(!new_defs){for(size_t j=0;j<n;++j)free(rewritten[j]);free(rewritten);goto optimize_cleanup;}size_t out=0;for(size_t i=0;i<n;++i)if(live[i]){new_defs[out].name=rename[i]? (char*)rename[i] : scmd_strdup(cg->defs.items[i].name);new_defs[out].body=rewritten[i];new_defs[out].opaque=cg->defs.items[i].opaque;rename[i]=NULL;rewritten[i]=NULL;if(!new_defs[out].name||!new_defs[out].body){for(size_t j=0;j<=out;++j){free(new_defs[j].name);free(new_defs[j].body);}free(new_defs);for(size_t j=0;j<n;++j)free(rewritten[j]);free(rewritten);goto optimize_cleanup;}++out;}for(size_t i=0;i<n;++i){free(cg->defs.items[i].name);free(cg->defs.items[i].body);}free(cg->defs.items);cg->defs.items=new_defs;cg->defs.len=kept;cg->defs.cap=kept;for(size_t i=0;i<n;++i)free(rewritten[i]);free(rewritten);free((void*)rename);rename=NULL;optimize_cleanup:if(rename){for(size_t i=0;i<n;++i)free((void*)rename[i]);free((void*)rename);}free(live);free(force);free(mutable_name);free(preserve);free(queue);free(redirect);cfg_opt_map_dispose(&map);}

static void remove_stale_workers(Codegen*cg,size_t first_unused){for(size_t id=first_unused;id<1000000u;++id){char*path=worker_path(cg,id);int rc=remove(path);free(path);if(rc!=0)break;}}
static bool write_workers(Codegen*cg){for(size_t i=0;i<cg->workers.len;++i){AsyncWorker*w=&cg->workers.items[i];FILE*f=fopen(w->file_path,"wb");if(!f)return false;fprintf(f,"// generated by scmdc v%s async continuation %zu\n",SCMD_VERSION,i);if(w->clear_first)fprintf(f,"clear\n");if(w->delay_ms>0)fprintf(f,"sleep %d\n",w->delay_ms);fprintf(f,"%s\n",w->entry_alias);fclose(f);}remove_stale_workers(cg,cg->workers.len);return true;}
static size_t alias_line_bytes(const AliasDef*d){return strlen("alias ")+strlen(d->name)+2u+strlen(d->body)+3u;}
static char *page_path(Codegen*cg,size_t id){char*dir=path_dirname(cg->output_path),*stem=path_stem(cg->output_path),*r;if(cg->options.organized_output){char*d=scmd_format("%s/pages",dir);mkdirs(d);r=scmd_format("%s/%03zu.cfg",d,id);free(d);}else{char*d=scmd_format("%s/%s.pages",dir,stem);mkdirs(d);r=scmd_format("%s/%03zu.cfg",d,id);free(d);}free(dir);free(stem);return r;}
static char *page_ref(Codegen*cg,size_t id){if(cg->options.exec_prefix&&cg->options.exec_prefix[0])return scmd_format("%s/pages/%03zu.cfg",cg->options.exec_prefix,id);char*stem=path_stem(cg->output_path);char*r=scmd_format("%s.pages/%03zu.cfg",stem,id);free(stem);return r;}
static void remove_stale_pages(Codegen*cg,size_t first_unused){for(size_t id=first_unused;id<1000000u;++id){char*path=page_path(cg,id);int rc=remove(path);free(path);if(rc!=0)break;}}
static bool write_output(Codegen*cg){CGFunction*mf=resolve_function(cg,"main");const char*entry=mf?mf->entry_alias:"__scmd_halt";size_t reserve=256,bl=cg->options.page_bytes>reserve?cg->options.page_bytes-reserve:cg->options.page_bytes,cl=cg->options.page_commands>1?cg->options.page_commands-1:1;size_t *st=NULL,*en=NULL,np=0,i=0;while(i<cg->defs.len){size_t s=i,bytes=0,cmd=0;while(i<cg->defs.len){size_t lb=alias_line_bytes(&cg->defs.items[i]);if(cmd&&(cmd+1>cl||bytes+lb>bl))break;bytes+=lb;cmd++;i++;}st=(size_t*)realloc(st,(np+1u)*sizeof(*st));en=(size_t*)realloc(en,(np+1u)*sizeof(*en));st[np]=s;en[np]=i;np++;}if(!np){st=(size_t*)realloc(st,sizeof(*st));en=(size_t*)realloc(en,sizeof(*en));st[0]=en[0]=0;np=1;}char**pp=(char**)calloc(np,sizeof(*pp)),**pr=(char**)calloc(np,sizeof(*pr));for(size_t p=0;p<np;++p){pp[p]=page_path(cg,p);pr[p]=page_ref(cg,p);}FILE*l=fopen(cg->output_path,"wb");if(!l)return false;fprintf(l,"// generated by scmdc v%s\n// %zu command-buffer-safe page(s)\nexec %s\n",SCMD_VERSION,np,pr[0]);fclose(l);for(size_t p=0;p<np;++p){FILE*f=fopen(pp[p],"wb");if(!f)return false;fprintf(f,"// scmdc page %zu/%zu\n",p+1,np);for(size_t d=st[p];d<en[p];++d)fprintf(f,"alias %s \"%s\"\n",cg->defs.items[d].name,cg->defs.items[d].body);if(p+1<np)fprintf(f,"exec %s\n",pr[p+1]);else fprintf(f,"%s\n",entry);fclose(f);}remove_stale_pages(cg,np);for(size_t p=0;p<np;++p){free(pp[p]);free(pr[p]);}free(pp);free(pr);free(st);free(en);return true;}

static void codegen_dispose(Codegen *cg){
    if(!cg)return;
    cfg_opt_token_set_dispose(&cfg_opt_reserved_cache);cfg_opt_reserved_items=NULL;cfg_opt_reserved_len=0;
    for(size_t i=0;i<cg->defs.len;++i){free(cg->defs.items[i].name);free(cg->defs.items[i].body);}
    free(cg->defs.items);
    for(size_t i=0;i<cg->workers.len;++i){free(cg->workers.items[i].file_path);free(cg->workers.items[i].exec_ref);free(cg->workers.items[i].entry_alias);}
    free(cg->workers.items);
    for(size_t i=0;i<cg->global_count;++i)for(int b=0;b<8;++b)free(cg->globals[i].bit[b]);
    free(cg->globals);
    for(size_t i=0;i<cg->function_count;++i){
        CGFunction*f=&cg->functions[i];
        free(f->entry_alias);free(f->ret_alias);
        for(size_t j=0;j<f->local_count;++j)for(int b=0;b<8;++b)free(f->locals[j].bit[b]);
        free(f->locals);
    }
    free(cg->functions);
    for(size_t i=0;i<cg->block_count;++i){
        CGBlock*b=&cg->blocks[i];
        free(b->entry_alias);free(b->ret_alias);
        for(size_t j=0;j<b->record_count;++j)free(b->records[j].alias_name);
        free(b->records);
    }
    free(cg->blocks);
    memset(cg,0,sizeof(*cg));
}

bool scmd_codegen_cfg_ex(const char *source_path,const ScmdProgram *program,const char *output_path,const ScmdCodegenOptions *options){Codegen cg={0};BExpr*arena_mark=bexpr_arena;cg.source_path=source_path;cg.output_path=output_path;cg.options=(ScmdCodegenOptions){SCMD_CONSOLE_ASYNC,16,16,NULL,4096,40,false,true};if(options)cg.options=*options;if(cg.options.console_settle_ms<0)cg.options.console_settle_ms=0;if(cg.options.tick_ms<=0)cg.options.tick_ms=16;if(cg.options.page_bytes<512)cg.options.page_bytes=4096;if(cg.options.page_commands<4)cg.options.page_commands=40;emit_alias(&cg,"__scmd_branch_true","");emit_alias(&cg,"__scmd_branch_false","");emit_alias(&cg,"__scmd_true","__scmd_branch_true");emit_alias(&cg,"__scmd_false","__scmd_branch_false");emit_alias(&cg,"__scmd_halt","");setup_symbols(&cg,program);
    /* setup_symbols initializes variables to false; append constant global initial values later so last definition wins. */
    size_t gi=0;for(const ScmdGlobal*g=program->globals;g;g=g->next,++gi)if(!const_global_init(&cg,g,&cg.globals[gi]))cg.errors++;
    for(size_t bi=0;bi<cg.block_count;++bi){CGBlock*b=&cg.blocks[bi];char*e=compile_stmt_list(&cg,NULL,b,b->ast->body,b->ret_alias);emit_alias(&cg,b->entry_alias,e);free(e);}
    for(size_t fi=0;fi<cg.function_count;++fi){CGFunction*f=&cg.functions[fi];const char*fall=strcmp(f->ast->name,"main")==0?"__scmd_halt":f->ret_alias;char*e=compile_stmt_list(&cg,f,NULL,f->ast->body,fall);emit_alias(&cg,f->entry_alias,e);free(e);}
    if(cg.errors==0)optimize_cfg(&cg);bool ok=cg.errors==0&&write_output(&cg);if(ok)ok=write_workers(&cg);if(!ok&&cg.errors==0)scmd_error_at(source_path,1,1,"could not write generated cfg files");codegen_dispose(&cg);bexpr_arena_rewind(arena_mark);return ok;}
bool scmd_codegen_cfg(const char *source_path,const ScmdProgram *program,const char *output_path){ScmdCodegenOptions o={SCMD_CONSOLE_ASYNC,16,16,NULL,4096,40,false,true};return scmd_codegen_cfg_ex(source_path,program,output_path,&o);}
