/* Shared, bounded command-bar history. No editor keystrokes or macro payloads
 * are recorded unless they submit an actual command line through Enter. */
#include "types.h"
#define HISTORY_LIMIT 200
#define HISTORY_TEXT_LIMIT 32768
#define HISTORY_HEADER "# MiJoe command history v1"
struct history_entry { unsigned long id; int origin; char *text; };
struct history_list { struct history_entry entries[HISTORY_LIMIT]; int count; };
static const char *last_error;
const char *mj_cmdhistory_error(void) { return last_error ? last_error : "Cannot read command history; original file kept"; }
static void clear(struct history_list *list)
{
    int i;
    for(i=0;i<list->count;++i)joe_free(list->entries[i].text);
    list->count=0;
}
static int valid_utf8(const char *s)
{
    ptrdiff_t left=zlen(s);
    while(left>0)if(utf8_decode_fwrd(&s,&left)<0)return 0;
    return 1;
}
static int load(struct history_list *list)
{
    char *path=mj_profile_path(".history",0), *line=NULL, *text=NULL;
    FILE *f=path ? fopen(path,"rb") : NULL;
    struct stat st;
    int failed=0;
    if(!f)return !path || errno!=ENOENT ? -1 : 0;
    if(fstat(fileno(f),&st) || st.st_size>8L*1024*1024)failed=1;
    if(!failed && (!vsgets(&line,f) || strncmp(line,HISTORY_HEADER,zlen(HISTORY_HEADER)) ||
        (line[zlen(HISTORY_HEADER)] && line[zlen(HISTORY_HEADER)]!='\n' && line[zlen(HISTORY_HEADER)]!='\r')))failed=1;
    while(!failed && vsgets(&line,f)) {
        const char *p=line;
        char *end;
        unsigned long id;
        long origin;
        ptrdiff_t length;
        if(*p=='\n' || *p=='\r' || !*p)continue;
        if(list->count==HISTORY_LIMIT || *p<'0' || *p>'9'){failed=1;break;}
        errno=0;id=strtoul(p,&end,10);p=end;
        if(errno || !id || *p++!='\t'){failed=1;break;}
        errno=0;origin=strtol(p,&end,10);p=end;
        if(errno || origin<0 || origin>2 || *p++!='\t'){failed=1;break;}
        length=parse_string(&p,&text);
        if(length<=0 || length!=zlen(text)){failed=1;break;}
        while(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n')++p;
        if(*p || !text[0] || !text[1] || !strchr(":/?",text[0]) || zlen(text)>HISTORY_TEXT_LIMIT || !valid_utf8(text) ||
           (list->count && id>=list->entries[list->count-1].id)){failed=1;break;}
        list->entries[list->count].id=id;list->entries[list->count].origin=(int)origin;
        list->entries[list->count++].text=zdup(text);
    }
    if(ferror(f))failed=1;
    fclose(f);if(line)obj_free(line);
    return failed ? -1 : 0;
}
#ifdef JOEWIN
static HANDLE history_lock=INVALID_HANDLE_VALUE;
static int lock_history(void)
{
    char *path=mj_profile_path(".history.lock",1);
    wchar_t *wide;
    int length,attempt;
    if(!path)return -1;
    length=MultiByteToWideChar(CP_UTF8,0,path,-1,NULL,0);if(!length)return -1;
    wide=(wchar_t *)joe_malloc(length*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8,0,path,-1,wide,length);
    for(attempt=0;attempt<40;++attempt) {
        history_lock=CreateFileW(wide,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(history_lock!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_SHARING_VIOLATION)break;
        Sleep(5);
    }
    joe_free(wide);return history_lock==INVALID_HANDLE_VALUE ? -1 : 0;
}
static void unlock_history(void) {CloseHandle(history_lock);history_lock=INVALID_HANDLE_VALUE;}
#else
static int history_lock=-1;
static int lock_history(void)
{
    char *path=mj_profile_path(".history.lock",1);
    struct flock lock;
    int attempt;
    if(!path)return -1;
    history_lock=open(path,O_CREAT|O_RDWR,0600);if(history_lock<0)return -1;
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    for(attempt=0;attempt<40;++attempt) {
        if(!fcntl(history_lock,F_SETLK,&lock))return 0;
        if(errno!=EACCES && errno!=EAGAIN && errno!=EINTR)break;
        usleep(5000);
    }
    close(history_lock);history_lock=-1;return -1;
}
static void unlock_history(void) {close(history_lock);history_lock=-1;}
#endif
static int save(struct history_list *list)
{
    char *path=mj_profile_path(".history",1), *temporary=NULL;
    FILE *f;
    int i,fd=-1,failed=0;
    if(!path)return -1;
    for(i=0;i<8;++i) {
        temporary=vsfmt(temporary,0,"%s.%ld.%d.tmp",path,(long)getpid(),i);
        fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd>=0 || errno!=EEXIST)break;
    }
    if(fd<0){if(temporary)obj_free(temporary);return -1;}
    f=fdopen(fd,"wb");
    if(!f){close(fd);unlink(temporary);obj_free(temporary);return -1;}
    fprintf(f,HISTORY_HEADER "\n");
    for(i=0;i<list->count;++i) {
        fprintf(f,"%lu\t%d\t",list->entries[i].id,list->entries[i].origin);
        emit_string(f,sz(list->entries[i].text));fputc('\n',f);
    }
    if(ferror(f))failed=1;
    if(fclose(f))failed=1;
    if(!failed && mj_profile_replace(temporary,path))failed=1;
    if(failed)unlink(temporary);
    obj_free(temporary);return failed ? -1 : 0;
}
static int history_query(int prefix,const char *command)
{
    const char *p=command;
    if(prefix!=':')return 0;
    while(*p==':' || *p==' ' || *p=='\t')++p;
    if(strncmp(p,"history",7))return 0;
    p+=7;while(*p==' ' || *p=='\t')++p;
    return !*p;
}
int mj_cmdhistory_add(int prefix,const char *command,int origin)
{
    struct history_list list;
    struct history_entry entry;
    char *encoded,*full;
    const char *p=command;
    int result;
    while(*p==' ' || *p=='\t')++p;
    if(!*p || history_query(prefix,command))return 0;
    encoded=my_iconv(NULL,utf8_map,command,locale_map);
    full=zdup(vsfmt(NULL,0,"%c%s",prefix,encoded));obj_free(encoded);
    if((prefix!=':' && prefix!='/' && prefix!='?') || origin<0 || origin>2 || zlen(full)>HISTORY_TEXT_LIMIT || !valid_utf8(full)) {
        joe_free(full);last_error="Invalid command history entry";return -1;
    }
    if(lock_history()){joe_free(full);last_error="Cannot lock command history; command still executed";return -1;}
    memset(&list,0,sizeof(list));
    if(load(&list)){clear(&list);unlock_history();joe_free(full);last_error="Command history file is invalid or unreadable; original file kept";return -1;}
    entry.id=list.count ? list.entries[0].id+1 : 1;
    if(!entry.id){clear(&list);unlock_history();joe_free(full);last_error="Command history sequence exhausted";return -1;}
    entry.origin=origin;entry.text=full;
    if(list.count==HISTORY_LIMIT)joe_free(list.entries[--list.count].text);
    memmove(list.entries+1,list.entries,list.count*sizeof(entry));list.entries[0]=entry;++list.count;
    result=save(&list);clear(&list);unlock_history();
    last_error=result ? "Cannot save some command-line inputs; command still executed" : NULL;
    return result;
}
char *mj_cmdhistory_text(void)
{
    struct history_list list;
    char *out;
    int i;
    memset(&list,0,sizeof(list));
    if(load(&list)){clear(&list);last_error="Command history file is invalid or unreadable; original file kept";return NULL;}
    out=vsfmt(NULL,0,"Command-bar history: %d / 200 | newest first\nINPUT=typed  REC=macro recording  PLAY=macro playback\n\n",list.count);obj_perm(out);
    if(last_error)out=vsfmt(out,vslen(out),"Warning: %s\n\n",last_error);
    if(!list.count)out=vsncpy(sv(out),sz("No submitted command-line input yet.\n"));
    for(i=0;i<list.count;++i) {
        const char *p=list.entries[i].text;
        ptrdiff_t left=zlen(p);
        out=vsfmt(out,vslen(out),"%6lu  %-5s  ",list.entries[i].id,list.entries[i].origin==2 ? "PLAY" : list.entries[i].origin==1 ? "REC" : "INPUT");
        while(left>0) {
            const char *start=p;
            int c=utf8_decode_fwrd(&p,&left);
            if(c<32 || (c>=127 && c<160))out=vsfmt(out,vslen(out),"<U+%04X>",c);
            else out=vsncpy(sv(out),start,p-start);
        }
        out=vsadd(out,'\n');
    }
    clear(&list);return out;
}
