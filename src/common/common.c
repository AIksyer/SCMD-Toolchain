#include "scmd/common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *scmd_read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return NULL; }
    buf[n] = '\0';
    if (size_out) *size_out = (size_t)n;
    return buf;
}

char *scmd_strdup(const char *s) { return scmd_strndup(s, strlen(s)); }
char *scmd_strndup(const char *s, size_t n) { char *out=(char*)malloc(n+1u); if(!out)return NULL; memcpy(out,s,n); out[n]='\0'; return out; }
char *scmd_format(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); va_list cp; va_copy(cp,ap); int n=vsnprintf(NULL,0,fmt,cp); va_end(cp);
    if(n<0){va_end(ap);return NULL;} char*out=(char*)malloc((size_t)n+1u); if(!out){va_end(ap);return NULL;} vsnprintf(out,(size_t)n+1u,fmt,ap);va_end(ap);return out;
}

static char *read_source_line(const char *path,int target){
    if(!path) return NULL;
    FILE *f=fopen(path,"rb"); if(!f)return NULL; char *buf=NULL; size_t cap=0,len=0; int line=1; int ch;
    while((ch=fgetc(f))!=EOF){ if(line==target){ if(len+2u>cap){size_t nc=cap?cap*2u:128u;char*nb=(char*)realloc(buf,nc);if(!nb){free(buf);fclose(f);return NULL;}buf=nb;cap=nc;} if(ch=='\n')break; if(ch!='\r')buf[len++]=(char)ch; } else if(ch=='\n') line++; if(line>target)break; }
    fclose(f); if(!buf)return NULL; buf[len]='\0'; return buf;
}

void scmd_error_at(const char *path, int line, int col, const char *fmt, ...) {
    const char *p=path?path:"<input>";
    fprintf(stderr,"error: "); va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap); fputc('\n',stderr);
    fprintf(stderr," --> %s:%d:%d\n",p,line,col);
    char *src=read_source_line(path,line);
    if(src){
        int width=1,tmp=line; while(tmp>=10){width++;tmp/=10;}
        fprintf(stderr," %*d | %s\n",width,line,src);
        fprintf(stderr," %*s | ",width,"");
        for(int i=1;i<col;++i) fputc(' ',stderr);
        fputs("^\n",stderr);
        free(src);
    }
}
