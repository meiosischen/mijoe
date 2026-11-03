/* Shared, atomic, bounded recent-file history and a display-row modal picker. */
#include "types.h"
#include "vim.h"
#define RECENT_LIMIT 200
struct recent_entry { char *path; int flags; };
struct recent_list { struct recent_entry entries[RECENT_LIMIT]; int count; };
struct recent_row { char *text; int entry; };
static int suppress_mouse_up;
static struct {
    BW *bw;
    struct recent_list list;
    struct recent_row *rows;
    int count, selected, top, width, height, x, y;
    char error[160];
} view;

static void clear(struct recent_list *list)
{
    int i;
    for (i=0;i<list->count;++i) joe_free(list->entries[i].path);
    list->count=0;
}
static int same(const char *a,const char *b)
{
#ifdef JOEWIN
    while (*a && *b) {
        int x=(unsigned char)*a++, y=(unsigned char)*b++;
        if (x=='\\') x='/'; if (y=='\\') y='/';
        if (x>='A' && x<='Z') x+='a'-'A';
        if (y>='A' && y<='Z') y+='a'-'A';
        if (x!=y) return 0;
    }
    return !*a && !*b;
#else
    return !zcmp(a,b);
#endif
}
static int load(struct recent_list *list)
{
    char *path=mj_profile_path(".recent",0), *line=NULL, *name=NULL;
    FILE *f=path ? fopen(path,"rb") : NULL;
    struct stat status;
    int failed;
    clear(list);
    if (!f) return !path || errno!=ENOENT ? -1 : 0;
    if(fstat(fileno(f),&status) || status.st_size>16L*1024*1024) {fclose(f);return -1;}
    if(vsgets(&line,f) && strncmp(line,"# MiJoe recent v1:",18)) {fclose(f);return -1;}
    while (list->count<RECENT_LIMIT && vsgets(&line,f)) {
        const char *p=line;
        int flags, i;
        long value;
        char *end;
        if (!(*p>='0' && *p<='9')) continue;
        value=strtol(p,&end,10);p=end;
        if (*p!='\t' || value<0 || value>15) continue;
        flags=(int)value;
        ++p;
        if (parse_string(&p,&name)<=0 || !name[0] || zlen(name)>32768) continue;
        for (i=0;i<list->count;++i) if (same(name,list->entries[i].path)) break;
        if (i<list->count) continue;
        list->entries[list->count].path=zdup(name);
        list->entries[list->count++].flags=flags;
    }
    failed=ferror(f);fclose(f);return failed ? -1 : 0;
}
#ifdef JOEWIN
static HANDLE history_lock=INVALID_HANDLE_VALUE;
static int lock_history(void)
{
    char *path=mj_profile_path(".recent.lock",1);
    wchar_t *wide;
    int length,attempt;
    if (!path) return -1;
    length=MultiByteToWideChar(CP_UTF8,0,path,-1,NULL,0);
    wide=(wchar_t *)joe_malloc(length*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8,0,path,-1,wide,length);
    for(attempt=0;attempt<40;++attempt) {
        history_lock=CreateFileW(wide,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(history_lock!=INVALID_HANDLE_VALUE || GetLastError()!=ERROR_SHARING_VIOLATION)break;
        Sleep(5);
    }
    joe_free(wide); return history_lock==INVALID_HANDLE_VALUE ? -1 : 0;
}
static void unlock_history(void) { CloseHandle(history_lock); history_lock=INVALID_HANDLE_VALUE; }
#else
static int history_lock=-1;
static int lock_history(void)
{
    char *path=mj_profile_path(".recent.lock",1);
    struct flock lock;
    int attempt;
    if (!path) return -1;
    history_lock=open(path,O_CREAT|O_RDWR,0600);
    if (history_lock<0) return -1;
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    for(attempt=0;attempt<40;++attempt) {
        if(!fcntl(history_lock,F_SETLK,&lock))return 0;
        if(errno!=EACCES && errno!=EAGAIN && errno!=EINTR)break;
        usleep(5000);
    }
    close(history_lock);history_lock=-1;return -1;
}
static void unlock_history(void) { close(history_lock); history_lock=-1; }
#endif
static int save(struct recent_list *list)
{
    char *path=mj_profile_path(".recent",1), *temporary;
    FILE *f;
    int i,failed=0;
    mode_t old;
    if (!path) return -1;
    temporary=vsfmt(NULL,0,"%s.%ld.tmp",path,(long)getpid());
    old=umask(0077);f=fopen(temporary,"wb");umask(old);
    if (!f) return -1;
    fprintf(f,"# MiJoe recent v1: flags (RO=1 HEX=2 LAZY=4 SOFT=8), quoted UTF-8 absolute path\n");
    for (i=0;i<list->count;++i) {
        fprintf(f,"%d\t",list->entries[i].flags);
        emit_string(f,sz(list->entries[i].path)); fputc('\n',f);
    }
    if (ferror(f)) failed=1;
    if (fclose(f)) failed=1;
    if (!failed && mj_profile_replace(temporary,path)) failed=1;
    if (failed) unlink(temporary);
    return failed ? -1 : 0;
}
void mj_recent_initialize(void)
{
    char *path=mj_profile_path(".recent",1);
    struct stat status;
    struct recent_list empty;
    if(!path || !stat(path,&status) || errno!=ENOENT || lock_history())return;
    if(stat(path,&status) && errno==ENOENT) {memset(&empty,0,sizeof(empty));save(&empty);}
    unlock_history();
}
void mj_recent_record(BW *bw,int opened)
{
    struct recent_list list;
    struct recent_entry entry;
    struct stat status;
    char *path,*utf8;
    int i;
    if (!bw || bw->b->internal || bw->b->scratch || bw->b->pid || lf_pending(bw->b)) return;
    path=tw_full_path(bw->b);
    if (!*path || stat(path,&status) || (status.st_mode&S_IFMT)!=S_IFREG) { joe_free(path); return; }
    utf8=my_iconv(NULL,utf8_map,path,locale_map); joe_free(path);
    if (lock_history()) { obj_free(utf8); return; }
    memset(&list,0,sizeof(list));if(load(&list)){clear(&list);obj_free(utf8);unlock_history();return;}
    for (i=0;i<list.count;++i) if (same(utf8,list.entries[i].path)) break;
    entry.path=zdup(utf8);obj_free(utf8);
    entry.flags=(bw->b->rdonly ? MJ_RECENT_RO : 0) | (bw->o.hex ? MJ_RECENT_HEX : 0) |
                (lf_active(bw->b) ? MJ_RECENT_LAZY : 0) | (bw->o.softwrap ? MJ_RECENT_SOFT : 0);
    if (i<list.count) {
        joe_free(list.entries[i].path);
        if (!opened) { list.entries[i]=entry; save(&list);clear(&list);unlock_history();return; }
        memmove(list.entries+i,list.entries+i+1,(list.count-i-1)*sizeof(entry)); --list.count;
    } else if (!opened) { joe_free(entry.path); clear(&list);unlock_history();return; }
    if (list.count==RECENT_LIMIT) { joe_free(list.entries[--list.count].path); }
    memmove(list.entries+1,list.entries,list.count*sizeof(entry));list.entries[0]=entry;++list.count;
    save(&list); clear(&list);unlock_history();
}
static int remove_path(const char *path)
{
    struct recent_list list;
    int i;
    int result;
    if (lock_history()) return -1;
    memset(&list,0,sizeof(list));if(load(&list)){clear(&list);unlock_history();return -1;}
    for (i=0;i<list.count;++i) if (same(path,list.entries[i].path)) {
        joe_free(list.entries[i].path);
        memmove(list.entries+i,list.entries+i+1,(list.count-i-1)*sizeof(list.entries[0]));--list.count;break;
    }
    result=save(&list); clear(&list);unlock_history();return result;
}
static void free_rows(void)
{
    int i;
    for (i=0;i<view.count;++i) joe_free(view.rows[i].text);
    if (view.rows) joe_free(view.rows);
    view.rows=NULL;view.count=0;
}
static void close_view(void)
{
    free_rows();clear(&view.list);memset(&view,0,sizeof(view));
    ttputs("\033[?25h");nredraw(maint->t);staupd=dostaupd=1;
}
int mj_recent_active(void) { return view.bw!=NULL; }
int mj_recent_count(void) { return view.list.count; }
int mj_recent_selected(void) { return view.selected; }
int mj_recent_top(void) { return view.top; }
int mj_recent_rows(void) { return view.count; }
static void add_row(char *text,int entry)
{
    if (!(view.count%64)) view.rows=(struct recent_row *)joe_realloc(view.rows,(view.count+64)*sizeof(*view.rows));
    view.rows[view.count].text=zdup(text);view.rows[view.count++].entry=entry;
}
static void ensure_selection(void);
static void layout(Screen *screen)
{
    int width=(int)screen->w*3/4,height=(int)screen->h*2/3,i;
    if (width<24) width=(int)screen->w;
    if (height<6) height=(int)screen->h;
    if (width==view.width && height==view.height) return;
    free_rows();view.width=width;view.height=height;view.x=((int)screen->w-width)/2;view.y=((int)screen->h-height)/2;
    for (i=0;i<view.list.count;++i) {
        char label[80],*text,*line=NULL;
        const char *p;
        int col=0,available=width-3;
        ptrdiff_t left;
        struct recent_entry *entry=view.list.entries+i;
        sprintf(label,"%d [%s %s%s] ",i+1,entry->flags & MJ_RECENT_RO ? "RO" : "RW",entry->flags & MJ_RECENT_HEX ? "HEX" : "TEXT",entry->flags & MJ_RECENT_LAZY ? " LAZY" : "");
        text=vsfmt(NULL,0,"%s%s",label,entry->path);p=text;left=zlen(text);
        while (left>0) {
            const char *before=p;
            int c=utf8_decode_fwrd(&p,&left),cells=joe_wcwidth(1,c),control=c<32 || c==127;
            if (c<32 || c==127) { c='?'; cells=1; }
            if (cells<0) cells=1;
            if (col+cells>available && col) { add_row(line ? line : "",i); line=vstrunc(line,0);col=0; }
            if (control) line=vsadd(line,'?');
            else line=vsncpy(sv(line),before,p-before);
            col+=cells;
        }
        add_row(line ? line : "",i);
    }
    if (view.top>view.count-(height-3)) view.top=view.count-(height-3);
    if (view.top<0) view.top=0;
    ensure_selection();
}
static void ensure_selection(void)
{
    int first=-1,last=-1,i,visible=view.height>3 ? view.height-3 : 1;
    for (i=0;i<view.count;++i) if(view.rows[i].entry==view.selected){if(first<0)first=i;last=i;}
    if(first<0)return;
    if(first<view.top)view.top=first;
    else if(first>=view.top+visible)view.top=first;
    else if(last>=view.top+visible && last-first+1<=visible)view.top=last-visible+1;
    if(view.top>view.count-visible)view.top=view.count-visible;
    if(view.top<0)view.top=0;
}
int mj_recent_open(BW *bw)
{
    if (mj_recent_active()) close_view();
    mj_recent_record(bw,0);view.bw=bw;
    if(load(&view.list))strcpy(view.error,"Could not read ~/.mj/.recent (permissions or invalid format).");
    layout(bw->t);
    ttputs("\033[?25l");return 0;
}
int mj_recent_key(int key)
{
    int next=view.selected,visible;
    if (!view.bw) return 0;
    layout(view.bw->t);visible=view.height>3 ? view.height-3 : 1;
    if (key==27 || key==3 || key=='q') { close_view();return 1; }
    if (key==VIM_PGDN || key==VIM_PGUP) {
        view.top+=key==VIM_PGDN ? visible : -visible;
        if(view.top>view.count-visible)view.top=view.count-visible;
        if(view.top<0)view.top=0;
        if(view.count)view.selected=view.rows[view.top].entry;
        return 1;
    }
    if (key==VIM_DOWN || key==14 || key=='j' || key==KEY_MWDOWN) ++next;
    else if(key==VIM_UP || key==16 || key=='k' || key==KEY_MWUP) --next;
    else if(key==VIM_HOME || key=='g')next=0;
    else if(key==VIM_END || key=='G')next=view.list.count-1;
    else if(key==13 || key==10) {
        int result;
        struct recent_entry entry;
        if(!view.list.count)return 1;
        entry=view.list.entries[view.selected];entry.path=zdup(entry.path);
        result=vim_recent_open(view.bw,entry.path,entry.flags);
        if (maint->curwin->watom==&watomtw) view.bw=(BW *)maint->curwin->object;
        if(!result) { joe_free(entry.path);close_view();return 1; }
        if(result==-2) {
            int failed=remove_path(entry.path);
            load(&view.list);view.width=0;layout(view.bw->t);
            if(view.selected>=view.list.count)view.selected=view.list.count-1;
            if(view.selected<0)view.selected=0;
            strcpy(view.error,failed ? "File is missing; could not update recent history." : "File no longer exists; removed from recent files.");ensure_selection();
        } else strcpy(view.error,"Could not open file; check permissions or file type.");
        joe_free(entry.path);return 1;
    } else return 1;
    if(next<0)next=0;if(next>=view.list.count)next=view.list.count-1;
    if(next>=0)view.selected=next;
    view.error[0]=0;ensure_selection();return 1;
}
int mj_recent_input(va_list args) { return mj_recent_key(va_arg(args,int)); }
int mj_recent_mouse(int event,int x,int y)
{
    int index;
    if(event==3 && suppress_mouse_up){suppress_mouse_up=0;return 1;}
    if(event==1)suppress_mouse_up=0;
    if(!view.bw)return 0;
    layout(view.bw->t);
    if(event==1 || event==4) {
        if(x>view.x && x<view.x+view.width-1 && y>view.y && y<view.y+view.height-2) {
            index=view.top+y-view.y-1;
            if(index>=0 && index<view.count) {view.selected=view.rows[index].entry;if(event==4){mj_recent_key(13);if(!view.bw)suppress_mouse_up=1;}}
        }
    }
    return 1;
}
int mj_recent_render(Screen *screen)
{
    SCRN *t=screen->t;
    int y,visible,thumb,start;
    char label[128];
    if(!view.bw)return 0;
    layout(screen);ttputs("\033[?25l");if(view.width<4 || view.height<4)return 1;
    visible=view.height-3;thumb=view.count>visible ? visible*visible/view.count : visible;
    if(thumb<1)thumb=1;
    start=view.count>visible ? view.top*(visible-thumb)/(view.count-visible) : 0;
    for(y=0;y<view.height;++y) {
        int at=(view.y+y)*screen->w+view.x,index=view.top+y-1,atr=bg_menu;
        const char *text="";char *converted=NULL;
        if(y==0){sprintf(label,"Recent %d/%d | ~/.mj/.recent",view.list.count ? view.selected+1 : 0,view.list.count);text=label;atr=(bg_menu & ~FG_MASK)|FG_BCYAN|BOLD;}
        else if(y==view.height-1)text="Up/Down ^P/^N | PgUp/PgDn | Enter | Esc";
        else if(y==view.height-2)text=view.error[0] ? view.error : "Modes restored: RO/RW, TEXT/HEX, LAZY";
        else if(!view.count && y==1)text="No recent files.";
        else if(index<view.count){text=view.rows[index].text;if(view.rows[index].entry==view.selected)atr=selectatr;}
        if(!locale_map->type){converted=my_iconv(NULL,locale_map,text,utf8_map);text=converted;}
        genfield(t,t->scrn+at+1,t->attr+at+1,view.x+1,view.y+y,0,text,zlen(text),atr,view.width-2,0,NULL);
        if(converted)obj_free(converted);
        outatr(locale_map,t,t->scrn+at,t->attr+at,view.x,view.y+y,!y || y==view.height-1 ? '+' : '|',bg_menu);
        outatr(locale_map,t,t->scrn+at+view.width-1,t->attr+at+view.width-1,view.x+view.width-1,view.y+y,!y || y==view.height-1 ? '+' : y>0 && y<=visible && view.count>visible ? (y-1>=start && y-1<start+thumb ? '#' : ':') : '|',bg_menu);
    }
    return 1; /* Keep the text caret hidden while the picker is open. */
}
