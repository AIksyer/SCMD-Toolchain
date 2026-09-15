#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "scmd/project.h"
#include "scmd/common.h"
#include "scmd/comptime.h"
#include "scmd/lexer.h"
#include "scmd/loader.h"
#include "scmd/sema.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(path) _mkdir(path)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MKDIR(path) mkdir(path,0777)
#endif

static int is_sep(char c){return c=='/'||c=='\\';}
static char *dirname_dup(const char *path){const char*last=NULL;for(const char*p=path;*p;++p)if(is_sep(*p))last=p;if(!last)return scmd_strdup(".");if(last==path)return scmd_strdup("/");return scmd_strndup(path,(size_t)(last-path));}
static const char *basename_ptr(const char *path){const char*b=path;for(const char*p=path;*p;++p)if(is_sep(*p))b=p+1;return b;}
static char *stem_dup(const char *path){const char*b=basename_ptr(path),*d=strrchr(b,'.');return scmd_strndup(b,d?(size_t)(d-b):strlen(b));}
static int is_absolute(const char*p){return p&&p[0]&&(p[0]=='/'||p[0]=='\\'||(p[1]&&isalpha((unsigned char)p[0])&&p[1]==':'));}
static char *join_path(const char*b,const char*c){if(is_absolute(c))return scmd_strdup(c);size_t n=strlen(b);return scmd_format("%s%s%s",b,(n&&is_sep(b[n-1]))?"":"/",c);}
static bool mkdirs(const char*path){char*t=scmd_strdup(path);if(!t)return false;for(char*p=t;*p;++p)if(*p=='\\')*p='/';char*scan=t;
#ifdef _WIN32
if(isalpha((unsigned char)scan[0])&&scan[1]==':')scan+=2;
#endif
for(char*p=scan+(*scan=='/'?1:0);*p;++p){if(*p!='/')continue;*p='\0';if(*t&&MKDIR(t)!=0&&errno!=EEXIST){free(t);return false;}*p='/';}if(*t&&MKDIR(t)!=0&&errno!=EEXIST){free(t);return false;}free(t);return true;}
static bool remove_tree(const char*path){
#ifdef _WIN32
    DWORD attr=GetFileAttributesA(path);
    if(attr==INVALID_FILE_ATTRIBUTES){DWORD e=GetLastError();return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND;}
    if(!(attr&FILE_ATTRIBUTE_DIRECTORY))return DeleteFileA(path)!=0;
    if(attr&FILE_ATTRIBUTE_REPARSE_POINT)return RemoveDirectoryA(path)!=0;
    char*pattern=scmd_format("%s\\*",path);if(!pattern)return false;
    WIN32_FIND_DATAA fd;HANDLE h=FindFirstFileA(pattern,&fd);free(pattern);
    if(h==INVALID_HANDLE_VALUE){DWORD e=GetLastError();return e==ERROR_FILE_NOT_FOUND?RemoveDirectoryA(path)!=0:false;}
    bool ok=true;
    do{
        if(strcmp(fd.cFileName,".")==0||strcmp(fd.cFileName,"..")==0)continue;
        char*child=scmd_format("%s\\%s",path,fd.cFileName);if(!child){ok=false;break;}
        if(!remove_tree(child))ok=false;free(child);if(!ok)break;
    }while(FindNextFileA(h,&fd));
    FindClose(h);if(!ok)return false;return RemoveDirectoryA(path)!=0;
#else
    struct stat st;if(lstat(path,&st)!=0)return errno==ENOENT;
    if(!S_ISDIR(st.st_mode))return unlink(path)==0;
    DIR*d=opendir(path);if(!d)return false;bool ok=true;struct dirent*de;
    while((de=readdir(d))!=NULL){if(strcmp(de->d_name,".")==0||strcmp(de->d_name,"..")==0)continue;char*child=scmd_format("%s/%s",path,de->d_name);if(!child){ok=false;break;}if(!remove_tree(child))ok=false;free(child);if(!ok)break;}
    closedir(d);if(!ok)return false;return rmdir(path)==0;
#endif
}
static bool write_text(const char*path,const char*text){FILE*f=fopen(path,"wb");if(!f)return false;fputs(text,f);fclose(f);return true;}
static bool safe_package(const char*s){if(!s||!*s||strstr(s,"..")||strchr(s,':'))return false;for(const unsigned char*p=(const unsigned char*)s;*p;++p)if(!isalnum(*p)&&*p!='_'&&*p!='-'&&*p!='/'&&*p!='\\')return false;return true;}

typedef struct PP { const char *path; ScmdLexer lx; ScmdToken cur; int errors; } PP;
static void pn(PP*p){p->cur=scmd_lexer_next(&p->lx);if(p->cur.kind==TOK_ERROR){scmd_error_at(p->path,p->cur.line,p->cur.col,"invalid project token");p->errors++;}}
static bool pe(PP*p,ScmdTokenKind k,const char*what){if(p->cur.kind==k){pn(p);return true;}scmd_error_at(p->path,p->cur.line,p->cur.col,"expected %s, got %s",what,scmd_token_name(p->cur.kind));p->errors++;return false;}
static bool tok_is(ScmdToken t,const char*s){size_t n=strlen(s);return t.len==n&&memcmp(t.start,s,n)==0;}
static char *td(ScmdToken t){return scmd_strndup(t.start,t.len);}
static char *strval(ScmdToken t){char*out=(char*)malloc(t.len+1u);size_t w=0;for(size_t i=0;i<t.len;++i){char c=t.start[i];if(c=='\\'&&i+1<t.len){char n=t.start[++i];out[w++]=n=='n'?'\n':n=='t'?'\t':n;}else out[w++]=c;}out[w]='\0';return out;}
static unsigned long numval(ScmdToken t){char*s=td(t);char*w=s;for(char*r=s;*r;++r)if(*r!='_')*w++=*r;*w='\0';unsigned long v=strtoul(s,NULL,10);free(s);return v;}
static void skip_value(PP*p){while(p->cur.kind!=TOK_SEMI&&p->cur.kind!=TOK_EOF&&p->cur.kind!=TOK_RBRACE)pn(p);if(p->cur.kind==TOK_SEMI)pn(p);}

static bool parse_string_prop(PP*p,char **dst){pe(p,TOK_ASSIGN,"'='");ScmdToken t=p->cur;if(!pe(p,TOK_STRING,"string literal"))return false;char*v=strval(t);free(*dst);*dst=v;pe(p,TOK_SEMI,"';'");return true;}
static bool parse_bool_prop(PP*p,bool *dst){pe(p,TOK_ASSIGN,"'='");if(p->cur.kind==TOK_TRUE){*dst=true;pn(p);}else if(p->cur.kind==TOK_FALSE){*dst=false;pn(p);}else{scmd_error_at(p->path,p->cur.line,p->cur.col,"expected true or false");p->errors++;}pe(p,TOK_SEMI,"';'");return true;}
static int parse_duration_ms(PP*p){ScmdToken n=p->cur;if(!pe(p,TOK_NUMBER,"duration number"))return 0;unsigned long v=numval(n);if(p->cur.kind==TOK_MS){pn(p);return(int)v;}if(p->cur.kind==TOK_S){pn(p);return(int)(v*1000ul);}if(p->cur.kind==TOK_TICK||p->cur.kind==TOK_TICKS){pn(p);return(int)v;}scmd_error_at(p->path,p->cur.line,p->cur.col,"expected ms, s, tick, or ticks");p->errors++;return(int)v;}

static void parse_console(PP*p,ScmdProject*out){pe(p,TOK_LBRACE,"'{'");while(p->cur.kind!=TOK_RBRACE&&p->cur.kind!=TOK_EOF){ScmdToken key=p->cur;if(p->cur.kind!=TOK_IDENT && p->cur.kind!=TOK_TICK){scmd_error_at(p->path,p->cur.line,p->cur.col,"expected console option");p->errors++;skip_value(p);continue;}pn(p);if(tok_is(key,"mode")){pe(p,TOK_ASSIGN,"'='");ScmdToken v=p->cur;if(tok_is(v,"async")){out->codegen.console_mode=SCMD_CONSOLE_ASYNC;pn(p);}else if(tok_is(v,"sync")){out->codegen.console_mode=SCMD_CONSOLE_SYNC;pn(p);}else{scmd_error_at(p->path,v.line,v.col,"console.mode expects async or sync");p->errors++;pn(p);}pe(p,TOK_SEMI,"';'");}
else if(tok_is(key,"settle")){pe(p,TOK_ASSIGN,"'='");out->codegen.console_settle_ms=parse_duration_ms(p);pe(p,TOK_SEMI,"';'");}
else if(tok_is(key,"tick")){pe(p,TOK_ASSIGN,"'='");out->codegen.tick_ms=parse_duration_ms(p);pe(p,TOK_SEMI,"';'");}
else{char*k=td(key);fprintf(stderr,"warning: unknown console option '%s'\n",k);free(k);skip_value(p);}}pe(p,TOK_RBRACE,"'}'");}
static void parse_paging(PP*p,ScmdProject*out){pe(p,TOK_LBRACE,"'{'");while(p->cur.kind!=TOK_RBRACE&&p->cur.kind!=TOK_EOF){ScmdToken key=p->cur;if(!pe(p,TOK_IDENT,"paging option")){skip_value(p);continue;}pe(p,TOK_ASSIGN,"'='");ScmdToken n=p->cur;if(!pe(p,TOK_NUMBER,"number")){skip_value(p);continue;}unsigned long v=numval(n);if(tok_is(key,"max_bytes"))out->codegen.page_bytes=(size_t)v;else if(tok_is(key,"max_commands"))out->codegen.page_commands=(size_t)v;else{char*k=td(key);fprintf(stderr,"warning: unknown paging option '%s'\n",k);free(k);}pe(p,TOK_SEMI,"';'");}pe(p,TOK_RBRACE,"'}'");}
static void parse_target(PP*p,ScmdProject*out){ScmdToken target=p->cur;if(!pe(p,TOK_IDENT,"target name"))return;if(!tok_is(target,"cs2")){char*t=td(target);scmd_error_at(p->path,target.line,target.col,"unsupported target '%s'",t);free(t);p->errors++;}pe(p,TOK_LBRACE,"'{'");while(p->cur.kind!=TOK_RBRACE&&p->cur.kind!=TOK_EOF){ScmdToken sec=p->cur;if(!pe(p,TOK_IDENT,"target section"))break;if(tok_is(sec,"console"))parse_console(p,out);else if(tok_is(sec,"paging"))parse_paging(p,out);else{char*s=td(sec);scmd_error_at(p->path,sec.line,sec.col,"unknown target section '%s'",s);free(s);p->errors++;skip_value(p);}}pe(p,TOK_RBRACE,"'}'");}

bool scmd_project_load(const char *path,ScmdProject*out){memset(out,0,sizeof(*out));size_t n=0;char*text=scmd_read_file(path,&n);(void)n;if(!text){fprintf(stderr,"error: cannot read project '%s'\n",path);return false;}const char*q=text;while(*q&&isspace((unsigned char)*q))q++;if(*q=='['){scmd_error_at(path,1,1,"v0.5 INI-style .scmdproj is deprecated; create or migrate to project { ... } syntax");free(text);return false;}
    out->project_path=scmd_strdup(path);out->base_dir=dirname_dup(path);out->name=stem_dup(path);out->entry=scmd_strdup("src/main.scmd");out->output_dir=scmd_strdup("build");out->package=scmd_strdup(out->name);out->bootstrap=true;out->codegen=(ScmdCodegenOptions){SCMD_CONSOLE_ASYNC,16,16,NULL,4096,40,true,true};
    PP p={0};p.path=path;scmd_lexer_init(&p.lx,text);pn(&p);ScmdToken pk=p.cur;if(!tok_is(pk,"project")){scmd_error_at(path,pk.line,pk.col,"project file must start with project \"name\"");p.errors++;}else pn(&p);ScmdToken name=p.cur;if(pe(&p,TOK_STRING,"project name")){free(out->name);out->name=strval(name);free(out->package);out->package=scmd_strdup(out->name);}pe(&p,TOK_LBRACE,"'{'");
    while(p.cur.kind!=TOK_RBRACE&&p.cur.kind!=TOK_EOF){ScmdToken key=p.cur;if(!pe(&p,TOK_IDENT,"project property")){skip_value(&p);continue;}if(tok_is(key,"entry"))parse_string_prop(&p,&out->entry);else if(tok_is(key,"output"))parse_string_prop(&p,&out->output_dir);else if(tok_is(key,"package"))parse_string_prop(&p,&out->package);else if(tok_is(key,"bootstrap"))parse_bool_prop(&p,&out->bootstrap);else if(tok_is(key,"target"))parse_target(&p,out);else{char*k=td(key);fprintf(stderr,"warning: unknown project property '%s'\n",k);free(k);skip_value(&p);}}
    pe(&p,TOK_RBRACE,"'}'");if(p.cur.kind!=TOK_EOF){scmd_error_at(path,p.cur.line,p.cur.col,"unexpected token after project block");p.errors++;}free(text);if(!safe_package(out->package)){scmd_error_at(path,1,1,"invalid package path '%s'",out->package);p.errors++;}if(out->codegen.page_bytes<512||out->codegen.page_bytes>65536){scmd_error_at(path,1,1,"paging.max_bytes must be 512..65536");p.errors++;}if(out->codegen.page_commands<4||out->codegen.page_commands>512){scmd_error_at(path,1,1,"paging.max_commands must be 4..512");p.errors++;}if(p.errors){scmd_project_dispose(out);return false;}return true;}

static bool write_launcher(const char*path,const char*exec_ref){FILE*f=fopen(path,"wb");if(!f)return false;fprintf(f,"// SCMD bootstrap\nexec %s\n",exec_ref);fclose(f);return true;}
static void json_escape(FILE*f,const char*s){for(;*s;++s){if(*s=='"'||*s=='\\')fputc('\\',f);if(*s=='\n')fputs("\\n",f);else fputc(*s,f);}}
static bool write_manifest(const char*path,const ScmdProject*p,const ScmdSourceList*sources){
    FILE*f=fopen(path,"wb");
    if(!f)return false;
    fprintf(f,"{\n  \"format\": 1,\n  \"name\": \"");json_escape(f,p->name);
    fprintf(f,"\",\n  \"package\": \"");json_escape(f,p->package);
    fprintf(f,"\",\n  \"entry\": \"");json_escape(f,p->entry);
    fprintf(f,"\",\n  \"target\": \"cs2\",\n  \"loading\": {\"mode\": \"demand\", \"mandatory\": true},\n  \"console\": {\"mode\": \"%s\", \"settle_ms\": %d, \"tick_ms\": %d},\n  \"paging\": {\"max_bytes\": %zu, \"max_commands\": %zu},\n  \"sources\": [",p->codegen.console_mode==SCMD_CONSOLE_ASYNC?"async":"sync",p->codegen.console_settle_ms,p->codegen.tick_ms,p->codegen.page_bytes,p->codegen.page_commands);
    for(size_t i=0;i<sources->count;++i){if(i)fputs(", ",f);fputc('"',f);json_escape(f,sources->items[i]);fputc('"',f);}
    fputs("]\n}\n",f);
    bool ok=ferror(f)==0&&fclose(f)==0;
    return ok;
}

bool scmd_project_build(const ScmdProject*project){
    bool ok=false;
    char *entry=NULL,*out_root=NULL,*pkg=NULL,*internal=NULL,*manifest=NULL;
    ScmdProgram program={0};
    ScmdSourceList sources={0};

    if(!project){fprintf(stderr,"error: null project\n");return false;}
    entry=join_path(project->base_dir,project->entry);
    if(!entry){fprintf(stderr,"error: out of memory while resolving project entry\n");goto cleanup;}
    if(!scmd_load_program(entry,&program,&sources))goto cleanup;
    if(!scmd_comptime_run(entry,&program))goto cleanup;
    if(!scmd_sema_check(entry,&program))goto cleanup;

    out_root=join_path(project->base_dir,project->output_dir);
    if(!out_root){fprintf(stderr,"error: out of memory while resolving output directory\n");goto cleanup;}
    pkg=join_path(out_root,project->package);
    if(!pkg){fprintf(stderr,"error: out of memory while resolving package directory\n");goto cleanup;}
    /* Project builds are snapshots, not overlays. Leaving stale lazy/pages files
     * behind can silently bloat a deploy or expose dead modules after the source
     * call graph changes. The package path is already validated as a safe
     * project-relative path, so remove only that exact generated package tree. */
    if(!remove_tree(pkg)){fprintf(stderr,"error: cannot clean generated package '%s'\n",pkg);goto cleanup;}
    if(!mkdirs(pkg)){fprintf(stderr,"error: cannot create '%s'\n",pkg);goto cleanup;}

    internal=join_path(pkg,"entry.cfg");
    if(!internal){fprintf(stderr,"error: out of memory while resolving package entry\n");goto cleanup;}
    ScmdCodegenOptions opts=project->codegen;
    opts.exec_prefix=project->package;
    opts.organized_output=true;
    if(!scmd_codegen_cfg_ex(entry,&program,internal,&opts))goto cleanup;

    if(project->bootstrap){
        char*n=scmd_format("%s.cfg",project->name);
        char*lp=n?join_path(out_root,n):NULL;
        char*er=scmd_format("%s/entry",project->package);
        if(!n||!lp||!er){fprintf(stderr,"error: out of memory while preparing bootstrap\n");free(n);free(lp);free(er);goto cleanup;}
        if(!write_launcher(lp,er)){fprintf(stderr,"error: cannot write bootstrap '%s'\n",lp);free(n);free(lp);free(er);goto cleanup;}
        free(n);free(lp);free(er);
    }

    manifest=join_path(pkg,"manifest.json");
    if(!manifest||!write_manifest(manifest,project,&sources)){
        fprintf(stderr,"error: cannot write package manifest\n");
        goto cleanup;
    }
    printf("scmdc: project '%s' built\n  sources : %zu\n  package : %s\n  run     : exec %s%s\n",project->name,sources.count,pkg,project->bootstrap?project->name:project->package,project->bootstrap?"":"/entry");
    ok=true;

cleanup:
    free(manifest);free(internal);free(pkg);free(out_root);free(entry);
    scmd_source_list_dispose(&sources);
    scmd_program_dispose(&program);
    return ok;
}

bool scmd_project_init(const char*directory,const char*name_override){if(!mkdirs(directory))return false;char*src=join_path(directory,"src");if(!mkdirs(src)){free(src);return false;}char*name=name_override?scmd_strdup(name_override):scmd_strdup(basename_ptr(directory));char*pnm=scmd_format("%s.scmdproj",name),*pp=join_path(directory,pnm);char*proj=scmd_format(
"project \"%s\"\n{\n    entry = \"src/main.scmd\";\n    output = \"build\";\n    package = \"%s\";\n    bootstrap = true;\n\n    target cs2\n    {\n        console\n        {\n            mode = async;\n            settle = 16ms;\n            tick = 16ms;\n        }\n\n        paging\n        {\n            max_bytes = 4096;\n            max_commands = 40;\n        }\n    }\n}\n",name,name);
    const char*main_src="get \"hello.scmd\";\n\nbool enabled = true;\n\nfunction main()\n{\n    console.clear();\n    console.print(\"================================\");\n    console.print(\"SCMD PROJECT IS ALIVE\");\n    hello();\n\n    if(enabled)\n    {\n        console.print(\"get + function + console: PASS\");\n    }\n\n    console.print(\"================================\");\n}\n";
    const char*lib_src="function hello()\n{\n    console.print(\"hello from src/hello.scmd\");\n    return;\n}\n";char*mp=join_path(src,"main.scmd"),*lp=join_path(src,"hello.scmd"),*psp=join_path(directory,"build.ps1"),*shp=join_path(directory,"build.sh"),*gp=join_path(directory,".gitignore");char*ps=scmd_format("$ErrorActionPreference = 'Stop'\nscmdc build .\\%s\n",pnm),*sh=scmd_format("#!/usr/bin/env sh\nset -eu\nscmdc build ./%s\n",pnm);bool ok=write_text(pp,proj)&&write_text(mp,main_src)&&write_text(lp,lib_src)&&write_text(psp,ps)&&write_text(shp,sh)&&write_text(gp,"build/\n");if(ok)printf("scmdc: initialized project %s at %s\n",name,directory);free(src);free(name);free(pnm);free(pp);free(proj);free(mp);free(lp);free(psp);free(shp);free(gp);free(ps);free(sh);return ok;}

void scmd_project_dispose(ScmdProject *project){
    if(!project)return;
    free(project->project_path);
    free(project->base_dir);
    free(project->name);
    free(project->entry);
    free(project->output_dir);
    free(project->package);
    memset(project,0,sizeof(*project));
}
