/* Bounded C89 implementation of MiJoe's internal line-oriented pipelines. */
#include "types.h"
#include "vimpipe.h"
#include <ctype.h>
#include <stdlib.h>

#define VP_STAGES 16
#define VP_FIELDS 4096
#define VP_ITEMS 64
enum { VP_GREP, VP_AWK, VP_HEAD, VP_MORE, VP_LESS };
enum { A_FIELD, A_NR, A_NF, A_STRING, A_NUMBER };
enum { C_NONE, C_REGEX, C_EQ, C_NE, C_LT, C_LE, C_GT, C_GE, C_MATCH, C_NOMATCH };
struct atom { int type, field, space; char *text; double number; };
struct stage {
    int kind, condition, print, count;
    char separator[8];
    struct atom left, right, items[VP_ITEMS];
    struct regcomp *regex;
    JOE_LONG_LONG seen, limit;
};
struct field { const char *text; ptrdiff_t length; };
struct vim_pipeline {
    struct stage stages[VP_STAGES];
    int count, to_file;
    B *scratch;
    struct field fields[VP_FIELDS];
    int nf;
    const char *record, *error;
    int (*emit)(JOE_LONG_LONG, JOE_LONG_LONG, const char *, void *);
    void *context;
};
struct string { char *data; ptrdiff_t length, capacity; };

static int blank(int c) { return c==' ' || c=='\t' || c=='\r' || c=='\v' || c=='\f'; }
static char *skip(char *p) { while (blank((unsigned char)*p)) ++p; return p; }
static void trim(char *p) { ptrdiff_t n=zlen(p); while (n && blank((unsigned char)p[n-1])) p[--n]=0; }
static int cancelled(void)
{
    if (ttcheck()) { int key=ttgetch(); if (key==3 || key==27) return 1; nungetc(key); }
    return 0;
}
static int append(struct string *s, const char *text, ptrdiff_t n)
{
    ptrdiff_t capacity;
    if (n<0 || n>VP_LINE_LIMIT-s->length) return 0;
    if (s->length+n+1>s->capacity) {
        capacity=s->capacity ? s->capacity*2 : 256;
        if (capacity<s->length+n+1) capacity=s->length+n+1;
        if (capacity>VP_LINE_LIMIT+1) capacity=VP_LINE_LIMIT+1;
        s->data=(char *)joe_realloc(s->data,capacity); s->capacity=capacity;
    }
    if (n) memcpy(s->data+s->length,text,n);
    s->length+=n; s->data[s->length]=0; return 1;
}

int vp_has_pipe(const char *p)
{
    int quote=0;
    for (;*p;++p) {
        if (*p=='\\' && p[1]) { ++p; continue; }
        if (quote) { if (*p==quote) quote=0; }
        else if (*p=='\'' || *p=='"') quote=*p;
        else if (*p=='|') return 1;
    }
    return 0;
}

/* Remove one outer command argument quote. Keep awk/regex backslashes intact
 * except escaped double quotes in a double-quoted outer argument. */
static char *argument(char *p)
{
    ptrdiff_t n;
    int q;
    p=skip(p); trim(p); n=zlen(p);
    if (*p!='\'' && *p!='"') return p;
    q=*p++;
    if (n<2 || p[n-2]!=q) return NULL;
    p[n-2]=0;
    if (q=='"') {
        char *a=p, *b=p;
        while (*a) {
            if (*a=='\\' && (a[1]=='"' || a[1]=='\\')) ++a;
            *b++=*a++;
        }
        *b=0;
    }
    return p;
}

/* Small option lexer, including -F',' and -F "\t". */
static int word(char **input, char *out, int size)
{
    char *p=skip(*input);
    int n=0, quote=0;
    while (*p && (quote || !blank((unsigned char)*p))) {
        int c=(unsigned char)*p++;
        if (quote && c==quote) { quote=0; continue; }
        if (!quote && (c=='\'' || c=='"')) { quote=c; continue; }
        if (c=='\\' && *p && (*p==quote || *p=='\\' || *p==' ')) c=(unsigned char)*p++;
        if (n>=size-1) return 0;
        out[n++]=(char)c;
    }
    if (quote) return 0;
    out[n]=0; *input=skip(p); return n>0;
}

static void atom_free(struct atom *a) { if (a->text) joe_free(a->text); }
void vp_free(struct vim_pipeline *p)
{
    int i,j;
    if (!p) return;
    for (i=0;i<p->count;++i) {
        struct stage *s=p->stages+i;
        atom_free(&s->left); atom_free(&s->right);
        for (j=0;j<s->count;++j) atom_free(s->items+j);
        if (s->regex) joe_regfree(s->regex);
    }
    if (p->scratch) { p->scratch->changed=0; brm(p->scratch); }
    joe_free(p);
}
const char *vp_error(struct vim_pipeline *p) { return p->error ? p->error : "Pipeline failed"; }
const char *vp_title(struct vim_pipeline *p)
{
    int k=p->stages[p->count-1].kind;
    return k==VP_MORE ? "More" : k==VP_LESS ? "Less" : k==VP_HEAD ? "Head" : p->count==1 ? "Awk" : "Pipeline";
}
int vp_to_file(struct vim_pipeline *p) { return p->to_file; }

static int regex_compile(struct vim_pipeline *p, struct stage *s, const char *text)
{
    struct string search={0};
    const char *at=text;
    int depth=0, bracket=0, first=0;
    s->regex=joe_regcomp(utf8_map,text,zlen(text),0,1,0);
    if (s->regex->err) { p->error=s->regex->err; return 0; }
    joe_regfree(s->regex); s->regex=NULL;
    /* A single NFA pass searches the whole record. Re-running an anchored
     * matcher at every character is quadratic for patterns like .*ERROR.
     * Prefix each top-level alternative, without introducing capture groups
     * or changing the meaning of a literal unmatched closing parenthesis. */
    append(&search,".*",2);
    while (*at) {
        char c=*at;
        if (c=='\\' && at[1]) { append(&search,at,2); at+=2; continue; }
        if (bracket) {
            if (first && c=='^' && first==1) { first=2; append(&search,at++,1); continue; }
            if (first) { first=0; if (c==']') { append(&search,at++,1); continue; } }
            if (c=='[' && at[1] && strchr(":=.",at[1])) {
                const char *end=at+2;
                while (*end && !(end[0]==at[1] && end[1]==']')) ++end;
                if (*end) { append(&search,at,end+2-at); at=end+2; continue; }
            }
            if (c==']') bracket=0;
        } else if (c=='[') { bracket=1; first=1; }
        else if (c=='(') ++depth;
        else if (c==')' && depth) --depth;
        append(&search,at++,1);
        if (!bracket && !depth && c=='|') append(&search,".*",2);
    }
    s->regex=joe_regcomp(utf8_map,search.data,search.length,0,1,0);
    joe_free(search.data);
    if (s->regex->err) { p->error=s->regex->err; return 0; }
    return 1;
}
static int boundary(int c) { return !c || blank(c) || strchr(",;{}<>=!~$\"",c)!=NULL; }
static int atom_parse(struct vim_pipeline *p, char **input, struct atom *a)
{
    char *s=skip(*input), *end;
    struct string text={0};
    if (*s=='$') {
        unsigned long n=0;
        ++s;
        if (!strncmp(s,"NF",2) && boundary((unsigned char)s[2])) { a->type=A_FIELD; a->field=-1; s+=2; }
        else {
            if (!isdigit((unsigned char)*s)) goto bad;
            while (isdigit((unsigned char)*s)) { n=n*10+*s++-'0'; if (n>VP_FIELDS) goto bad; }
            a->type=A_FIELD; a->field=(int)n;
        }
    } else if (!strncmp(s,"NR",2) && boundary((unsigned char)s[2])) { a->type=A_NR; s+=2; }
    else if (!strncmp(s,"NF",2) && boundary((unsigned char)s[2])) { a->type=A_NF; s+=2; }
    else if (*s=='"') {
        ++s;
        while (*s && *s!='"') {
            char c=*s++;
            if (c=='\\') {
                c=*s++;
                if (!c || (c!='t' && c!='"' && c!='\\')) goto bad;
                if (c=='t') c='\t';
            }
            if (!append(&text,&c,1)) goto bad;
        }
        if (*s!='"') goto bad;
        ++s; if (!append(&text,"",0)) goto bad;
        a->type=A_STRING; a->text=text.data; text.data=NULL;
    } else if (isdigit((unsigned char)*s) || *s=='-' || *s=='+' || *s=='.') {
        a->number=strtod(s,&end); if (end==s) goto bad;
        a->type=A_NUMBER; a->text=(char *)joe_malloc(end-s+1);
        memcpy(a->text,s,end-s); a->text[end-s]=0; s=end;
    } else goto bad;
    *input=skip(s); return 1;
bad:
    if (text.data) joe_free(text.data);
    p->error="Awk: expected $field, NR, NF, number or double-quoted string"; return 0;
}
static int regex_parse(struct vim_pipeline *p, struct stage *stage, char **input)
{
    char *s=skip(*input);
    struct string text={0};
    if (*s++!='/') goto bad;
    while (*s && *s!='/') {
        char c=*s++;
        if (c=='\\' && *s=='/') c=*s++;
        else if (c=='\\' && *s) {
            if (!append(&text,&c,1)) goto bad;
            c=*s++;
        }
        if (!append(&text,&c,1)) goto bad;
    }
    if (*s!='/') goto bad;
    if (!append(&text,"",0)) goto bad;
    if (!regex_compile(p,stage,text.data)) { joe_free(text.data); return 0; }
    joe_free(text.data); *input=skip(s+1); return 1;
bad:
    if (text.data) joe_free(text.data);
    p->error="Awk: expected a closed /regular expression/"; return 0;
}
static int awk_parse(struct vim_pipeline *p, struct stage *s, char *arg)
{
    int op=0;
    arg=argument(arg);
    if (!arg || !*arg) { p->error="Use :awk [-F separator] 'condition {print fields}'"; return 0; }
    arg=skip(arg);
    if (*arg=='/') { s->condition=C_REGEX; if (!regex_parse(p,s,&arg)) return 0; }
    else if (*arg!='{') {
        if (!atom_parse(p,&arg,&s->left)) return 0;
        if (!strncmp(arg,"!~",2)) { op=C_NOMATCH; arg+=2; }
        else if (*arg=='~') { op=C_MATCH; ++arg; }
        else if (!strncmp(arg,"==",2)) { op=C_EQ; arg+=2; }
        else if (!strncmp(arg,"!=",2)) { op=C_NE; arg+=2; }
        else if (!strncmp(arg,"<=",2)) { op=C_LE; arg+=2; }
        else if (!strncmp(arg,">=",2)) { op=C_GE; arg+=2; }
        else if (*arg=='<') { op=C_LT; ++arg; }
        else if (*arg=='>') { op=C_GT; ++arg; }
        else { p->error="Awk: expected == != < <= > >= ~ or !~"; return 0; }
        s->condition=op;
        if (op==C_MATCH || op==C_NOMATCH) { if (!regex_parse(p,s,&arg)) return 0; }
        else if (!atom_parse(p,&arg,&s->right)) return 0;
    }
    arg=skip(arg);
    if (!*arg && s->condition) return 1;
    if (*arg++!='{') goto bad;
    arg=skip(arg);
    if (strncmp(arg,"print",5) || !boundary((unsigned char)arg[5])) goto bad;
    arg=skip(arg+5); s->print=1;
    if (*arg=='}' || *arg==';') { s->items[0].type=A_FIELD; s->count=1; }
    else {
        int space=0;
        while (*arg && *arg!='}' && *arg!=';') {
            struct atom *a;
            if (s->count && !space && (*arg=='+' || *arg=='-')) {
                p->error="Awk: arithmetic expressions are not supported"; return 0;
            }
            if (s->count==VP_ITEMS) { p->error="Awk: at most 64 print items"; return 0; }
            a=s->items+s->count++; a->space=space;
            if (!atom_parse(p,&arg,a)) return 0;
            space=0;
            if (*arg==',') { ++arg; space=1; arg=skip(arg); if (!*arg || *arg=='}' || *arg==';') goto bad; }
        }
    }
    if (*arg==';') arg=skip(arg+1);
    if (*arg!='}' || *skip(arg+1)) goto bad;
    return 1;
bad:
    p->error="Awk: only one {print ...} action is supported (no assignments or loops)"; return 0;
}
static int stage_parse(struct vim_pipeline *p, char *text)
{
    struct stage *s;
    char name[32], token[64], *arg=text;
    if (p->count==VP_STAGES) { p->error="Pipeline: at most 16 commands"; return 0; }
    s=p->stages+p->count++;
    if (!word(&arg,name,sizeof(name))) goto bad;
    if (!strcmp(name,"grep")) s->kind=VP_GREP;
    else if (!strcmp(name,"awk")) s->kind=VP_AWK;
    else if (!strcmp(name,"head")) { s->kind=VP_HEAD; s->limit=10; }
    else if (!strcmp(name,"more")) s->kind=VP_MORE;
    else if (!strcmp(name,"less")) s->kind=VP_LESS;
    else { p->error="Pipeline: only grep, awk, head, more and less are built in"; return 0; }
    while (*arg=='-') {
        char *start=arg;
        if (!word(&arg,token,sizeof(token))) goto bad;
        if (!strcmp(token,"--")) break;
        if (!strcmp(token,"-f")) { p->to_file=1; continue; }
        if (s->kind==VP_AWK && !strncmp(token,"-F",2)) {
            const char *sep=token+2, *read;
            ptrdiff_t left;
            if (!*sep) { if (!word(&arg,token,sizeof(token))) goto bad; sep=token; }
            if (!strcmp(sep,"\\t")) sep="\t";
            if (!strcmp(sep," ")) { s->separator[0]=0; continue; }
            left=zlen(sep); read=sep;
            if (!left || left>4 || utf8_decode_fwrd(&read,&left)<0 || left) {
                p->error="Awk: -F requires one character (or \\t)"; return 0;
            }
            strcpy(s->separator,sep); continue;
        }
        if (s->kind==VP_HEAD && !strncmp(token,"-n",2)) {
            const char *number=token+2;
            JOE_LONG_LONG n=0;
            if (!*number) { if (!word(&arg,token,sizeof(token))) goto bad; number=token; }
            if (!*number) goto bad;
            while (*number>='0' && *number<='9') { n=n*10+*number++-'0'; if (n>1000000000L) goto bad; }
            if (*number) goto bad;
            s->limit=n; continue;
        }
        /* A negative-number awk condition is a program, not an option. */
        if (s->kind==VP_AWK && (isdigit((unsigned char)start[1]) || start[1]=='.')) { arg=start; break; }
        goto bad;
    }
    if (s->kind==VP_GREP) {
        arg=argument(arg);
        if (!arg || !*arg) goto bad;
        return regex_compile(p,s,arg);
    }
    if (s->kind==VP_AWK) return awk_parse(p,s,arg);
    if (*skip(arg)) goto bad;
    return 1;
bad:
    p->error="Use grep [-f] pattern | awk [-F separator] 'program' | head [-n N] | more/less"; return 0;
}

struct vim_pipeline *vp_compile(const char *command, const char **error)
{
    struct vim_pipeline *p;
    char *copy, *start, *s;
    int quote=0, regex=0, braces=0, awk=0;
    if (zlen(command)>65536) { *error="Pipeline command exceeds 64 KiB"; return NULL; }
    p=(struct vim_pipeline *)joe_malloc(sizeof(*p));
    copy=zdup(command); start=s=copy;
    memset(p,0,sizeof(*p));
    for (;;) {
        int c=(unsigned char)*s;
        if (!quote && !regex && !braces && (!c || c=='|')) {
            *s=0;
            if (!stage_parse(p,start)) break;
            if (!c) { joe_free(copy); *error=NULL; return p; }
            start=s+1; awk=0;
        } else if (c=='\\' && s[1]) { ++s; }
        else if (quote) { if (c==quote) quote=0; }
        else if (regex) { if (c=='/') regex=0; }
        else {
            char *a=skip(start);
            awk=!strncmp(a,"awk",3) && blank((unsigned char)a[3]);
            if (c=='\'' || c=='"') quote=c;
            else if (awk && c=='/') regex=1;
            else if (awk && c=='{') ++braces;
            else if (awk && c=='}') { if (!braces) { p->error="Awk: unmatched brace"; break; } --braces; }
        }
        if (!c) { p->error="Pipeline: unterminated quote, regex or action"; break; }
        ++s;
    }
    /* Compiler regex errors live in the compiled regex, so copy to a stable
     * diagnostic before releasing all partially parsed stages. */
    { static char diagnostic[256];
      const char *why=vp_error(p); ptrdiff_t n=zlen(why); if (n>255) n=255;
      memcpy(diagnostic,why,n); diagnostic[n]=0; *error=diagnostic; }
    joe_free(copy); vp_free(p); return NULL;
}

static int fields(struct vim_pipeline *p, const char *separator)
{
    const char *s=p->record, *a;
    ptrdiff_t width=zlen(separator);
    p->nf=0;
    if (!*s) return 1;
    for (;;) {
        if (!width) { while (blank((unsigned char)*s)) ++s; if (!*s) break; }
        a=s;
        if (width) { while (*s && strncmp(s,separator,width)) ++s; }
        else while (*s && !blank((unsigned char)*s)) ++s;
        if (p->nf==VP_FIELDS) { p->error="Awk: record exceeds 4096 fields"; return 0; }
        p->fields[p->nf].text=a; p->fields[p->nf++].length=s-a;
        if (!*s) break;
        if (width) { s+=width; if (!*s) {
            if (p->nf==VP_FIELDS) { p->error="Awk: record exceeds 4096 fields"; return 0; }
            p->fields[p->nf].text=s; p->fields[p->nf++].length=0; break;
        } }
    }
    return 1;
}
static struct field atom_value(struct vim_pipeline *p, struct stage *s, struct atom *a, char *number)
{
    struct field result;
    result.text=""; result.length=0;
    if (a->type==A_FIELD) {
        if (!a->field) { result.text=p->record; result.length=zlen(p->record); }
        else {
            int field=a->field<0 ? p->nf : a->field;
            if (field>0 && field<=p->nf) result=p->fields[field-1];
        }
    } else if (a->type==A_NR || a->type==A_NF) {
        if (a->type==A_NR) sprintf(number,"%" JOE_INT64_FORMAT "d",s->seen);
        else sprintf(number,"%d",p->nf);
        result.text=number; result.length=zlen(number);
    } else if (a->type==A_NUMBER) {
        if (a->number>=-1e15 && a->number<=1e15 && floor(a->number)==a->number) sprintf(number,"%.0f",a->number);
        else sprintf(number,"%.6g",a->number);
        result.text=number; result.length=zlen(number);
    } else { result.text=a->text; result.length=zlen(a->text); }
    return result;
}
static int match(struct vim_pipeline *p, struct regcomp *regex, const char *text, ptrdiff_t length)
{
    P *scan;
    int found;
    B *b=p->scratch;
    if (!b) {
        b=p->scratch=bmk(NULL); b->internal=1; b->o.charmap=utf8_map;
        undorm(b->undo); b->undo=NULL;
    }
    bdel(b->bof,b->eof); binsm(b->bof,text,length);
    scan=pdup(b->bof,"pipeline_regex");
    if (cancelled()) { p->error="Pipeline cancelled"; found=-1; }
    else {
        found=!joe_regexec(regex,scan,0,NULL,0);
        if (cancelled()) { p->error="Pipeline cancelled"; found=-1; }
    }
    prm(scan); return found;
}
static int numeric_string(char *p)
{
    int digits=0;
    p=skip(p); if (*p=='+' || *p=='-') ++p;
    while (isdigit((unsigned char)*p)) { ++p; ++digits; }
    if (*p=='.') { ++p; while (isdigit((unsigned char)*p)) { ++p; ++digits; } }
    if (!digits) return 0;
    if (*p=='e' || *p=='E') {
        ++p; if (*p=='+' || *p=='-') ++p;
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) ++p;
    }
    return !*skip(p);
}
static int condition(struct vim_pipeline *p, struct stage *s)
{
    char left_number[64], right_number[64];
    struct field a,b;
    int c;
    if (!s->condition) return 1;
    if (s->condition==C_REGEX) return match(p,s->regex,p->record,zlen(p->record));
    a=atom_value(p,s,&s->left,left_number);
    if (s->condition==C_MATCH || s->condition==C_NOMATCH) {
        c=match(p,s->regex,a.text,a.length);
        return c<0 ? c : s->condition==C_NOMATCH ? !c : c;
    }
    b=atom_value(p,s,&s->right,right_number);
    if (s->left.type!=A_STRING && s->right.type!=A_STRING) {
        char *x=(char *)joe_malloc(a.length+1), *y=(char *)joe_malloc(b.length+1);
        double u,v;
        memcpy(x,a.text,a.length); x[a.length]=0; memcpy(y,b.text,b.length); y[b.length]=0;
        u=strtod(x,NULL); v=strtod(y,NULL);
        if (numeric_string(x) && numeric_string(y)) {
            c=u<v ? -1 : u>v ? 1 : 0;
            joe_free(x); joe_free(y); goto compare;
        }
        joe_free(x); joe_free(y);
    }
    {
        ptrdiff_t n=a.length<b.length ? a.length : b.length;
        c=memcmp(a.text,b.text,n);
        if (!c) c=a.length<b.length ? -1 : a.length>b.length ? 1 : 0;
    }
compare:
    return s->condition==C_EQ ? c==0 : s->condition==C_NE ? c!=0 : s->condition==C_LT ? c<0 :
           s->condition==C_LE ? c<=0 : s->condition==C_GT ? c>0 : c>=0;
}
static int record(JOE_LONG_LONG row, JOE_LONG_LONG start, JOE_LONG_LONG end, const char *text, void *context)
{
    struct vim_pipeline *p=(struct vim_pipeline *)context;
    char *owned=NULL;
    int i, result=0, stop=0;
    (void)end;
    p->record=text;
    for (i=0;i<p->count;++i) {
        struct stage *s=p->stages+i;
        ++s->seen;
        if (s->kind==VP_HEAD) {
            if (s->seen>s->limit) { stop=1; goto done; }
            if (s->seen==s->limit) stop=1;
        } else if (s->kind==VP_GREP) {
            result=match(p,s->regex,p->record,zlen(p->record));
            if (result<0) goto done;
            if (!result) goto done;
        } else if (s->kind==VP_AWK) {
            if (!fields(p,s->separator)) { result=-1; goto done; }
            result=condition(p,s); if (result<0) goto done;
            if (!result) goto done;
            if (s->print) {
                struct string output={0};
                int j;
                for (j=0;j<s->count;++j) {
                    char number[64];
                    struct field a=atom_value(p,s,s->items+j,number);
                    if ((s->items[j].space && !append(&output," ",1)) || !append(&output,a.text,a.length)) {
                        if (output.data) joe_free(output.data);
                        p->error="Pipeline: generated record exceeds 1 MiB"; result=-1; goto done;
                    }
                }
                if (!append(&output,"",0)) { result=-1; goto done; }
                if (owned) joe_free(owned);
                owned=output.data; p->record=owned;
            }
        }
    }
    result=p->emit(row,start,p->record,p->context);
    if (result<0) p->error=result==-4 ? "Pipeline output exceeds 64 MiB; narrow the query" : "Pipeline output failed";
done:
    if (owned) joe_free(owned);
    if (result<0) return result;
    return stop || (i==p->count && result>0) ? 1 : 0;
}

int vp_run(struct vim_pipeline *p, BW *source,
           int (*emit)(JOE_LONG_LONG,JOE_LONG_LONG,const char *,void *), void *context)
{
    P *scan;
    struct string line={0};
    JOE_LONG_LONG row=1, start=0;
    int result=0, i;
    long checked=0;
    p->emit=emit; p->context=context;
    for (i=0;i<p->count;++i) if (p->stages[i].kind==VP_HEAD && !p->stages[i].limit) return 0;
    if (lf_active(source->b)) {
        result=lf_each_line(source,VP_LINE_LIMIT,record,p);
        if (result<0 && !p->error) p->error=result==-2 ? "Pipeline cancelled" : result==-4 ? "Pipeline: source record exceeds 1 MiB" : result==-5 ? "Pipeline: NUL bytes require binary mode" : "Pipeline: source changed or read error";
        return result;
    }
    scan=pdup(source->b->bof,"pipeline_source");
    while (!piseof(scan)) {
        int c=pgetc(scan);
        if (!start && scan->byte<=3 && c==0xfeff) continue;
        if (!c) { p->error="Pipeline: NUL bytes require binary mode"; result=-1; break; }
        if (c=='\n') {
            if (line.length && line.data[line.length-1]=='\r') --line.length;
            append(&line,"",0);
            result=record(row++,start,(JOE_LONG_LONG)scan->byte,line.data,p);
            if (result) break;
            start=scan->byte; line.length=0;
        } else {
            char bytes[8]; ptrdiff_t n=utf8_encode(bytes,c);
            if (!append(&line,bytes,n)) { p->error="Pipeline: source record exceeds 1 MiB"; result=-1; break; }
        }
        if (!(++checked%1024) && cancelled()) { p->error="Pipeline cancelled"; result=-2; break; }
    }
    if (!result && (JOE_LONG_LONG)scan->byte>start && line.length) {
        if (line.data[line.length-1]=='\r') --line.length;
        append(&line,"",0); result=record(row,start,(JOE_LONG_LONG)scan->byte,line.data,p);
    }
    if (line.data) joe_free(line.data);
    prm(scan); return result;
}
