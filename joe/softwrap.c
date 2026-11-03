/* Display-only wrapping. C89, bounded row caches, no buffer-sized index. */
#include "types.h"

#define SW_ROWS 256
struct sw_cache {
    B *buffer;
    off_t line, bol, first;
    int count, ended, width, tab, crlf;
    struct charmap *map;
    struct bwsoft_row rows[SW_ROWS];
};
struct softwrap {
    struct sw_cache cache[2]; /* Pin the cursor's logical line while drawing. */
    off_t top_row;
    int enabled, width, cursor_x, cursor_y;
};

int bwsoft(BW *w)
{
    return w->o.softwrap && !w->o.hex && !w->o.ansi && !w->o.picture &&
           !w->b->pid && !w->prompt && w->parent->watom==&watomtw;
}
int bwsoft_width(BW *w)
{
    off_t width=w->o.rmargin+1;
    if (width>w->w) width=w->w;
    if (width<2) width=2;
    return (int)width;
}
static struct softwrap *state(BW *w)
{
    if (!w->soft) {
        w->soft=(struct softwrap *)joe_malloc(sizeof(struct softwrap));
        memset(w->soft,0,sizeof(struct softwrap));
    }
    return w->soft;
}
void bwsoft_invalidate(BW *w)
{
    if (w->soft) {
        w->soft->cache[0].buffer=w->soft->cache[1].buffer=NULL;
    }
}
void bwsoft_free(BW *w)
{
    if (w->soft) joe_free(w->soft);
    w->soft=NULL;
}
/* Find the next break without splitting a wide character. Tabs can span rows;
 * byte/basecol still point at the tab while col advances through its cells. */
static int advance(BW *w, P *line, struct bwsoft_row *at, struct bwsoft_row *next)
{
    P *p=pdup(line,"softwrap advance");
    off_t col=at->basecol, index=at->index, limit=at->col+bwsoft_width(w);
    int result=0;
    memset(next,0,sizeof(*next));
    pgoto(p,at->byte);
    while (!piseol(p) && !piseof(p)) {
        int c=brch(p), width;
        if (c=='\t') width=(int)(w->o.tab-col%w->o.tab);
        else { width=joe_wcwidth(w->b->o.charmap->type,c); if (width<0) width=1; }
        if (width && col>=limit) {
            next->byte=p->byte; next->col=next->basecol=col; next->index=index; result=1; break;
        }
        if (width && col+width>limit) {
            next->byte=p->byte; next->basecol=col; next->index=index;
            next->col=c=='\t' ? limit : col;
            /* Width is at least two; a wide glyph always makes progress. */
            result=1; break;
        }
        pgetc(p); col+=width; ++index;
    }
    next->syn=NULL;
    prm(p); return result;
}
static struct sw_cache *cache(BW *w, P *line)
{
    struct softwrap *s=state(w);
    struct sw_cache *c=&s->cache[line->line==w->cursor->line ? 0 : 1];
    P *bol=pdup(line,"softwrap bol");
    p_goto_bol(bol);
    if (c->buffer!=w->b || c->line!=bol->line || c->bol!=bol->byte ||
        c->width!=bwsoft_width(w) || c->tab!=w->o.tab ||
        c->map!=w->b->o.charmap || c->crlf!=w->b->o.crlf) {
        c->buffer=w->b; c->line=bol->line; c->bol=bol->byte;
        c->width=bwsoft_width(w); c->tab=(int)w->o.tab;
        c->map=w->b->o.charmap; c->crlf=w->b->o.crlf;
        c->first=0; c->count=1; c->ended=0;
        memset(c->rows,0,sizeof(c->rows)); c->rows[0].byte=bol->byte;
    }
    prm(bol); return c;
}
static off_t getrow(BW *w, P *line, off_t number, struct bwsoft_row *row)
{
    struct sw_cache *c=cache(w,line);
    if (number<c->first) {
        c->first=0; c->count=1; c->ended=0;
        memset(&c->rows[0],0,sizeof(c->rows[0])); c->rows[0].byte=c->bol;
    }
    while (number>=c->first+c->count && !c->ended) {
        struct bwsoft_row next;
        if (!advance(w,line,&c->rows[c->count-1],&next)) c->ended=1;
        else {
            if (c->count==SW_ROWS) {
                memmove(c->rows,c->rows+1,(SW_ROWS-1)*sizeof(c->rows[0]));
                ++c->first; --c->count;
            }
            c->rows[c->count++]=next;
        }
    }
    if (number>=c->first+c->count) number=c->first+c->count-1;
    *row=c->rows[(int)(number-c->first)]; return number;
}
static off_t cursor_row(BW *w, P *p, struct bwsoft_row *row)
{
    struct sw_cache *c=cache(w,p);
    off_t number=c->first, col=piscol(p);
    struct bwsoft_row next;
    if (c->rows[0].byte>p->byte || (c->rows[0].byte==p->byte && c->rows[0].col>col)) number=0;
    getrow(w,p,number,row);
    for (;;) {
        off_t found=getrow(w,p,number+1,&next);
        if (found==number || next.byte>p->byte || (next.byte==p->byte && next.col>col)) break;
        ++number; *row=next;
    }
    return number;
}
void bwsoft_top(BW *w, P *line, off_t *number)
{
    struct bwsoft_row row;
    pset(line,w->top); p_goto_bol(line);
    *number=getrow(w,line,state(w)->top_row,&row);
}
int bwsoft_step(BW *w, P *line, off_t *number, int direction)
{
    struct bwsoft_row row;
    if (direction>0) {
        off_t next=getrow(w,line,*number+1,&row);
        if (next>*number) { *number=next; return 1; }
        if (!pnextl(line)) return 0;
        *number=0; return 1;
    }
    if (*number) { --*number; return 1; }
    if (!line->line) return 0;
    pprevl(line); p_goto_bol(line);
    *number=getrow(w,line,MAXOFF,&row); return 1;
}
void bwsoft_get(BW *w, P *line, off_t number, struct bwsoft_row *row)
{
    struct bwsoft_row next;
    getrow(w,line,number,row);
    row->endcol=advance(w,line,row,&next) ? next.col : row->col+bwsoft_width(w);
}
static int compare(P *a, off_t ar, P *b, off_t br)
{
    if (a->line!=b->line) return a->line<b->line ? -1 : 1;
    return ar==br ? 0 : ar<br ? -1 : 1;
}
static int visible(BW *w, P *target, off_t target_row)
{
    P *p=pdup(w->top,"softwrap visible");
    off_t row;
    int y, found=-1;
    bwsoft_top(w,p,&row);
    for (y=0;y<w->h;++y) {
        if (!compare(p,row,target,target_row)) { found=y; break; }
        if (!bwsoft_step(w,p,&row,1)) break;
    }
    prm(p); return found;
}
static void set_top(BW *w, P *line, off_t number)
{
    pset(w->top,line); p_goto_bol(w->top); state(w)->top_row=number;
}
int bwsoft_follow(BW *w)
{
    struct softwrap *s;
    struct bwsoft_row row, top;
    off_t number;
    int y, steps, width;
    P *line;
    if (!bwsoft(w)) {
        if (w->soft && w->soft->enabled) {
            w->soft->enabled=0; w->soft->top_row=0; w->offset=0;
            msetI(w->t->t->updtab+w->y,1,w->h);
        }
        return 0;
    }
    s=state(w); width=bwsoft_width(w);
    if (!s->enabled || s->width!=width) {
        s->enabled=1; s->width=width;
        msetI(w->t->t->updtab+w->y,1,w->h);
    }
    w->offset=0; p_goto_bol(w->top);
    number=cursor_row(w,w->cursor,&row);
    s->top_row=getrow(w,w->top,s->top_row,&top);
    y=visible(w,w->cursor,number);
    if (y<0) {
        line=pdup(w->cursor,"softwrap follow"); p_goto_bol(line);
        steps=opt_mid ? w->h/2 : compare(w->cursor,number,w->top,s->top_row)>0 ? w->h-1 : 0;
        while (steps-- && bwsoft_step(w,line,&number,-1)) {}
        set_top(w,line,number); prm(line);
        number=cursor_row(w,w->cursor,&row); y=visible(w,w->cursor,number);
        msetI(w->t->t->updtab+w->y,1,w->h);
    }
    s->cursor_y=y<0 ? 0 : y;
    s->cursor_x=(int)(piscol(w->cursor)-row.col);
    if (s->cursor_x>=width) s->cursor_x=width-1;
    if (s->cursor_x<0) s->cursor_x=0;
    if (w->curlin!=w->cursor->line && w->o.hiline) msetI(w->t->t->updtab+w->y,1,w->h);
    w->curlin=w->cursor->line;
    return 1;
}
void bwsoft_cursor(BW *w, int *x, int *y)
{
    struct softwrap *s=state(w); *x=s->cursor_x; *y=s->cursor_y;
}
static void place(BW *w, P *line, off_t number, off_t column)
{
    struct bwsoft_row row, next;
    P *p=w->cursor;
    getrow(w,line,number,&row);
    pgoto(p,row.byte); pcol(p,row.col+column);
    if (advance(w,line,&row,&next) && (p->byte>next.byte || (p->byte==next.byte && piscol(p)>=next.col))) {
        pgoto(p,next.byte); if (p->byte>row.byte) prgetc(p);
    }
    p->xcol=piscol(p);
}
int bwsoft_move(BW *w, long amount)
{
    struct bwsoft_row row;
    off_t number, column;
    int moved=0, direction=amount<0 ? -1 : 1;
    P *line;
    if (!bwsoft(w)) return 0;
    number=cursor_row(w,w->cursor,&row); column=piscol(w->cursor)-row.col;
    line=pdup(w->cursor,"softwrap move"); p_goto_bol(line);
    while (amount) {
        if (!bwsoft_step(w,line,&number,direction)) break;
        moved=1; amount-=direction;
    }
    if (moved) place(w,line,number,column);
    prm(line); return moved;
}
int bwsoft_mouse(BW *w, int y, int x)
{
    P *line=pdup(w->top,"softwrap mouse");
    off_t row;
    if (y<0) y=0; if (y>=w->h) y=(int)w->h-1;
    if (x<0) x=0; if (x>=bwsoft_width(w)) x=bwsoft_width(w)-1;
    bwsoft_top(w,line,&row);
    while (y-- && bwsoft_step(w,line,&row,1)) {}
    place(w,line,row,x); prm(line); return 0;
}
void bwsoft_scroll(BW *w, int amount, int fixed)
{
    struct softwrap *s=state(w);
    struct bwsoft_row cursor;
    P *line=pdup(w->top,"softwrap scroll");
    off_t row, number;
    int direction=amount<0 ? -1 : 1, count=amount<0 ? -amount : amount;
    bwsoft_top(w,line,&row);
    while (count-- && bwsoft_step(w,line,&row,direction)) {}
    set_top(w,line,row);
    if (fixed) bwsoft_move(w,amount);
    number=cursor_row(w,w->cursor,&cursor);
    if (visible(w,w->cursor,number)<0) {
        int y=compare(w->cursor,number,line,s->top_row)<0 ? 0 : (int)w->h-1;
        bwsoft_mouse(w,y,(int)(piscol(w->cursor)-cursor.col));
    }
    prm(line); msetI(w->t->t->updtab+w->y,1,w->h);
}
