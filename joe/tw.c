/* 
 *	Text editing windows
 *	Copyright
 *		(C) 1992 Joseph H. Allen
 *
 *	This file is part of JOE (Joe's Own Editor)
 */
#include "types.h"
#include "vim.h"

int staen = 0;
int staupd = 0;
int keepup = 0;
int bg_stalin;

/* A status-path selection is independent of buffer marks and Vim modes. */
static W *path_window;
static char *path_value;
static ptrdiff_t path_anchor, path_focus;
static int path_dragging, path_whole;

char *tw_full_path(B *b)
{
    char *name, *full, *result;
    if (!b || !b->name || !b->name[0] || b->name[0]=='!') return zdup("");
    name=canonical(dequotevs(vsdupz(b->name)),CANFLAG_NORESTART);
    if (ISDIRSEP(name[0])
#ifdef JOEWIN
        || (name[0] && name[1]==':')
#endif
       ) full=name;
    else full=vsfmt(NULL,0,"%s/%s",pwd(),name);
    result=zdup(full); return result;
}
static char *path_display(const char *text)
{
    char *s=zdup(text);
    ptrdiff_t i;
    for (i=0;s[i];++i) if ((unsigned char)s[i]<32 || s[i]==127) s[i]='?';
    return s;
}
void tw_path_clear(void)
{
    if (!path_value) return;
    if (path_value) joe_free(path_value);
    path_value=NULL; path_window=NULL; path_dragging=path_whole=0;
    path_anchor=path_focus=0; staupd=1;
}
static BW *path_at(ptrdiff_t x, ptrdiff_t y)
{
    W *w;
    BW *bw;
    TW *tw;
    if (!maint || vim_grep_active() || help_text_active(maint)) return NULL;
    w=watpos(maint,x,y);
    if (!w || w->watom!=&watomtw || !w->object) return NULL;
    bw=(BW *)w->object; tw=(TW *)bw->object;
    if (!tw->staon || y!=w->y || tw->path_start<0 || x<w->x+tw->path_start || x>=w->x+tw->path_end) return NULL;
    return bw;
}
int tw_path_hit(ptrdiff_t x, ptrdiff_t y) { return path_at(x,y)!=NULL; }
static int path_current(BW *bw)
{
    char *full;
    int equal;
    if (!path_value || !bw || path_window!=bw->parent) return 0;
    full=tw_full_path(bw->b); equal=!zcmp(full,path_value); joe_free(full);
    return equal;
}
/* Map display cells to complete locale characters, including wide names. */
static ptrdiff_t path_byte(ptrdiff_t column)
{
    const char *at=path_value;
    ptrdiff_t left=zlen(path_value), offset=0, cells=0;
    while (left>0) {
        const char *before=at;
        int c, width;
        if (locale_map->type) c=utf8_decode_fwrd(&at,&left);
        else { c=to_uni(locale_map,(unsigned char)*at++); --left; }
        width=c<32 || c==127 ? 1 : joe_wcwidth(1,c);
        if (width<0) width=1;
        if (cells+width>column) break;
        cells+=width; offset+=at-before;
    }
    return offset;
}
char *tw_path_selection(void)
{
    W *w;
    int found=0;
    ptrdiff_t a,z;
    char *result;
    if (!path_value || !maint || !maint->topwin) return NULL;
    w=maint->topwin;
    do {
        if (w==path_window && w->watom==&watomtw && path_current((BW *)w->object)) { found=1; break; }
        w=w->link.next;
    } while (w!=maint->topwin);
    if (!found) { tw_path_clear(); return NULL; }
    a=path_anchor<path_focus ? path_anchor : path_focus;
    z=path_anchor>path_focus ? path_anchor : path_focus;
    a=path_whole ? 0 : path_byte(a); z=path_whole ? zlen(path_value) : path_byte(z);
    if (z<=a) return NULL;
    result=(char *)joe_malloc(z-a+1); memcpy(result,path_value+a,z-a); result[z-a]=0; return result;
}
int tw_path_mouse(int action, ptrdiff_t x, ptrdiff_t y, int clicks)
{
    BW *bw;
    TW *tw;
    if (action==1) {
        bw=path_at(x,y);
        tw_path_clear();
        if (!bw) return 0;
        tw=(TW *)bw->object; path_window=bw->parent; path_value=tw_full_path(bw->b);
        path_anchor=path_focus=x-bw->parent->x-tw->path_start;
        path_dragging=1;
        if (clicks>=2) { path_whole=1; path_anchor=0; path_focus=txtwidth(locale_map,path_value,zlen(path_value)); }
    } else {
        W *w;
        if (!path_dragging || !maint || !maint->topwin) return 0;
        w=maint->topwin;
        do { if (w==path_window) break; w=w->link.next; } while (w!=maint->topwin);
        if (w!=path_window || w->watom!=&watomtw || !path_current((BW *)w->object)) { tw_path_clear(); return 1; }
        bw=(BW *)w->object; tw=(TW *)bw->object;
        if (!path_whole) {
            path_focus=x-w->x-tw->path_start;
            if (path_focus<0) path_focus=0;
            if (path_focus>tw->path_end-tw->path_start) path_focus=tw->path_end-tw->path_start;
        }
        if (action==3) {
            char *text=tw_path_selection();
            path_dragging=0;
            if (text) { mouse_copy_text(text); joe_free(text); }
        }
    }
    staupd=1; return 1;
}

/* Move text window */

static void movetw(W *w, ptrdiff_t x, ptrdiff_t y)
{
	BW *bw = (BW *)w->object;
	TW *tw = (TW *)bw->object;

	if ((y || !staen) && w->h > 1) {
		if (!tw->staon) {	/* Scroll down and shrink */
			nscrldn(bw->parent->t->t, y, bw->parent->nh + y, 1);
		}
		bwmove(bw, x + bw->lincols, y + 1);
		tw->staon = 1;
	} else {
		if (tw->staon) {	/* Scroll up and grow */
			nscrlup(bw->parent->t->t, y, bw->parent->nh + y, 1);
		}
		bwmove(bw, x + bw->lincols, y);
		tw->staon = 0;
	}
}

/* Resize text window */

static void resizetw(W *w, ptrdiff_t wi, ptrdiff_t he)
{
	BW *bw = (BW *)w->object;
	if ((bw->parent->ny || !staen) && he > 1)
		bwresz(bw, wi - bw->lincols, he - 1);
	else
		bwresz(bw, wi - bw->lincols, he);
}

/* Get current context */

/* Use context.jsf to determine context.  The advantage of this vs.  the old
 * method is that the line attribute cache will help find context line
 * quickly in large files.
 */

static struct high_syntax *context_syntax;

static const int *get_context(BW *bw)
{
	static int buf1[SAVED_SIZE*2]; /* Double size because we replace \ with \\ */
	const int *src;
	P *p;
	struct lattr_db *db;
	HIGHLIGHT_STATE st;
	clear_state(&st);
	p = pdup(bw->cursor, "get_context");
	p_goto_bol(p);
	if (!context_syntax)
		context_syntax = load_syntax("context");
	if (context_syntax) {
		db = find_lattr_db(bw->b, context_syntax);
		if (db) {
			st = lattr_get(db, context_syntax, p, p->line + 1);
			/* Handles last line better */
			/* st = parse(context_syntax, p, st, p->b->o.charmap); */
		}
	}
	prm(p);
	src = st.saved_s;
	buf1[0] = 0;
	if (src) {
		ptrdiff_t i, j, spc;
		/* replace tabs to spaces and remove adjoining spaces */
		for (i=0,j=0,spc=0; src[i] && i < SAVED_SIZE-1; i++) {
			if (src[i]=='\t' || src[i]==' ') {
				if (spc) continue;
				spc = 1;
			}
			else spc = 0;
			if (src[i]=='\t')
				buf1[j++] = ' ';
			else if (src[i]=='\\') {
				buf1[j++] = '\\';
				buf1[j++] = '\\';
			} else if (src[i] != '\r') {
				buf1[j++] = src[i];
			}
		}
		buf1[j]= '\0';
	}
	return buf1;
}

/* Find first line (going backwards) which has 0 indentation level
 * and is not a comment, blank, or block structuring line.  This is
 * likely to be the line with the function name.
 *
 * There are actually two possibilities:
 *
 * We want the first line- 
 *
 * int
 * foo(int x,int y) {
 *
 *   }
 *
 * We want the last line-
 *
 * program foo(input,output);
 * var a, b, c : real;
 * begin
 *
 */

#if 0
static char *get_context(BW *bw)
{
	P *p = pdup(bw->cursor, "get_context");
	P *q = pdup(p, "get_context");
	char buf[6];
	char *buf1;
	int i, spc, c;

	buf1 = vsmk(128);
	/* Find first line with 0 indentation which is not a comment line */
	do {
		p_goto_bol(p);
		if (!pisindent(p) && !pisblank(p)) {
			/* next: */
			/* Somewhat inefficient, but better than hitting a long line with brlinevs */
			pset(q, p);
			for (i = 0; i < (sizeof(buf) - 1) && NO_MORE_DATA != (c = pgetb(q)); i++) {
				buf[i] = c;
			}
			buf[i] = 0;
			/* Ignore comment and block structuring lines */
			if (!(buf[0]=='{' ||
			    (buf[0]=='/' && buf[1]=='*') ||
			    buf[0]=='\f' ||
			    (buf[0]=='/' && buf[1]=='/') ||
			    buf[0]=='#' ||
			    (buf[0]=='b' && buf[1]=='e' && buf[2]=='g' && buf[3]=='i' && buf[4]=='n') ||
			    (buf[0]=='B' && buf[1]=='E' && buf[2]=='G' && buf[3]=='I' && buf[4]=='N') ||
			    (buf[0]=='-' && buf[1]=='-') ||
			    buf[0]==';')) {
 				/* replace tabs to spaces and remove adjoining spaces */
				pset(q, p);
 				buf1 = vstrunc(buf1, 0);
 				for (i=0,spc=0; i < 1024 && !piseol(q); i++) {
					c = pgetb(q);

 					if (c=='\t' || c==' ') {
 						if (spc) continue;
 						spc = 1;
 					}
 					else spc = 0;
 					if (c=='\t')
 						buf1 = vsadd(buf1, ' ');
					else if (c=='\\') {
						buf1 = vsadd(buf1, '\\');
						buf1 = vsadd(buf1, '\\');
					} else
						buf1 = vsadd(buf1, c);
 				}
				/* Uncomment to get the last line instead of the first line (see above)
			    	if (pprevl(p)) {
			    		p_goto_bol(p);
			    		if (!pisindent(p) && !pisblank(p))
			    			goto next;
			    	}
			    	*/
			    	break;
			}
			
		}
	} while (!buf1[0] && pprevl(p));

	prm(p);
	prm(q);

	return buf1;
}
#endif

char *duplicate_backslashes(const char *s, ptrdiff_t len)
{
	char *m;
	ptrdiff_t x, count;
	for (x = count = 0; x != len; ++x)
		if (s[x] == '\\')
			++count;
	m = vsmk(len + count);
	for (x = 0; x != len; ++x) {
		m = vsadd(m, s[x]);
		if (s[x] == '\\')
			m = vsadd(m, '\\');
	}
	return m;
}

char *stagen(char *stalin, BW *bw, const char *s, char fill)
{
	char buf[80];
	int x;
	int field;
	int left=s==bw->o.lmsg;
	W *w = bw->parent;
	TW *tw=(TW *)bw->object;
	time_t n=time(NULL);
	struct tm *cas;
	cas=localtime(&n);

	stalin = vstrunc(stalin, 0);
	if (left) tw->path_start=tw->path_end=-1;
	obj_perm(stalin);
	if (s == bw->o.lmsg && !bw->b->pid) {
		stalin = vsncpy(sv(stalin), sz("["));
		stalin = vsncpy(sv(stalin), sz(vim_mode_name()));
		stalin = vsncpy(sv(stalin), sz("] "));
	}
	while (*s) {
		if (*s == '%' && s[1]) {
			field = 0;
			++s;
			while (*s >= '0' && *s <= '9' && s[1]) {
				field = field * 10 + *s - '0';
				++s;
			}
			switch (*s) {
			case 'v': /* Version of JOE */
				{
					joe_snprintf_1(buf, SIZEOF(buf), "%s", VERSION);
					stalin = vsncpy(sv(stalin), sz(buf));
				}
				break;

			case 'x': /* Context (but only if autoindent is enabled) */
				{
					if (bw->o.title) {
						const int *ts = get_context(bw);
						if (ts) {
							/* We need to translate between file's character set to
							   locale */
							char *t = my_iconv1(NULL, locale_map, ts);
							stalin = vscat(stalin, sv(t));
						}
					}
				}
				break;

			case 'y':
				{
					if (bw->o.syntax) {
						joe_snprintf_1(buf, SIZEOF(buf), "(%s)", bw->o.syntax->name);
						stalin = vsncpy(sv(stalin), sz(buf));
					}
				}
				break;
			case 't':
				{
					time_t curtime = time(NULL);
					int l;
					char *d = ctime(&curtime);

					l = (d[11] - '0') * 10 + d[12] - '0';
					if (l > 12)
						l -= 12;
					joe_snprintf_1(buf, SIZEOF(buf), "%2.2d", l);
					if (buf[0] == '0')
						buf[0] = fill;
					stalin = vsncpy(sv(stalin), buf, 2);
					stalin = vsncpy(sv(stalin), d + 13, 3);
				}
				break;
			    case 'd':
				{
					if (s[1]) switch (*++s) {
						case 'd' : joe_snprintf_1(buf, SIZEOF(buf), "%02d",cas->tm_mday); break;
						case 'm' : joe_snprintf_1(buf, SIZEOF(buf), "%02d",cas->tm_mon + 1); break;
						case 'y' : joe_snprintf_1(buf, SIZEOF(buf), "%02d",cas->tm_year % 100); break;
						case 'Y' : joe_snprintf_1(buf, SIZEOF(buf), "%04d",cas->tm_year + 1900); break;
						case 'w' : joe_snprintf_1(buf, SIZEOF(buf), "%d",cas->tm_wday); break;
						case 'D' : joe_snprintf_1(buf, SIZEOF(buf), "%03d",cas->tm_yday); break;
						default : buf[0]='d'; buf[1]=*s; buf[2]=0;
					}
					else { buf[0]='d'; buf[1]=0;}
						stalin=vsncpy(sv(stalin),sz(buf));
				}
				break;

				case 'E':
				{
					int l;
					char *ch;
					buf[0]=0;
					for(l=0;s[l+1] && s[l+1] != '%'; l++) buf[l]=s[l+1];
					if (s[l+1]=='%' && buf[0]) {
						buf[l]=0;
						s+=l+1;
						ch=getenv(buf);
						if (ch) stalin=vsncpy(sv(stalin),sz(ch));
					} 
				}
				break;

			case 'Z':
				{
					const char *ch;
					int l;
					buf[0]=0;
					for(l=0;s[l+1] && s[l+1] != '%'; l++) buf[l]=s[l+1];
					if (s[l+1]=='%' && buf[0]) {
						buf[l]=0;
						s+=l+1;
						ch=get_status(bw, buf);
						if (ch) stalin=vsncpy(sv(stalin),sz(ch));
					} 
				}
				break;

			case 'u':
				{
					time_t curtime = time(NULL);
					char *d = ctime(&curtime);

					stalin = vsncpy(sv(stalin), d + 11, 5);
				}
				break;
			case 'T':
				if (bw->o.overtype)
					stalin = vsadd(stalin, 'O');
				else
					stalin = vsadd(stalin, 'I');
				break;
			case 'W':
				if (bw->o.wordwrap)
					stalin = vsadd(stalin, 'W');
				else
					stalin = vsadd(stalin, fill);
				break;
			case 'I':
				if (bw->o.autoindent)
					stalin = vsadd(stalin, 'A');
				else
					stalin = vsadd(stalin, fill);
				break;
			case 'X':
				if (square)
					stalin = vsadd(stalin, 'X');
				else
					stalin = vsadd(stalin, fill);
				break;
			case 'n':
				{
				if (bw->b->name) {
					char *full=tw_full_path(bw->b);
					char *tmp=path_display(*full ? full : bw->b->name);
					char *tmp1 = duplicate_backslashes(tmp,zlen(tmp));
					if (left && *full) tw->path_start=fmtlen(stalin);
					stalin = vsncpy(sv(stalin), sv(tmp1));
					if (left && *full) tw->path_end=fmtlen(stalin);
					joe_free(tmp); joe_free(full);
				} else {
					stalin = vsncpy(sv(stalin), sz(joe_gettext(_("Unnamed"))));
				}
				}
				break;
			case 'm':
				if (bw->b->changed)
					stalin = vsadd(stalin, '*');
				break;
			case 'R':
				if (bw->b->rdonly)
					stalin = vsncpy(sv(stalin), sz(joe_gettext(_("(Read only)"))));
				break;
			case '*':
				if (bw->b->changed)
					stalin = vsadd(stalin, '*');
				else
					stalin = vsadd(stalin, fill);
				break;
			case 'r':
                if (lf_row_label(bw,buf,sizeof(buf))) { stalin = vsncpy(sv(stalin),sz(buf)); break; }
				if (field)
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%-4" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)(bw->cursor->line + 1));
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%-4ld", (long)(bw->cursor->line + 1));
#endif
				else
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)(bw->cursor->line + 1));
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%ld", (long)(bw->cursor->line + 1));
#endif
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'o':
				if (field)
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%-4" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)bw->cursor->byte);
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%-4ld", (long)bw->cursor->byte);
#endif
				else
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)bw->cursor->byte);
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%ld", (long)bw->cursor->byte);
#endif
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'O':
				if (field)
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%-4" JOE_INT64_FORMAT "X", (unsigned JOE_LONG_LONG)bw->cursor->byte);
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%-4lX", (unsigned long)bw->cursor->byte);
#endif
				else
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%" JOE_INT64_FORMAT "X", (unsigned JOE_LONG_LONG)bw->cursor->byte);
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%lX", (unsigned long)bw->cursor->byte);
#endif
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'a':
				if (!piseof(bw->cursor))
					joe_snprintf_1(buf, SIZEOF(buf), "%3d", brch(bw->cursor));
				else
					joe_snprintf_0(buf, SIZEOF(buf), "   ");
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'A':
				if (field)
					if (!piseof(bw->cursor))
						joe_snprintf_1(buf, SIZEOF(buf), "%2.2X", brch(bw->cursor));
					else
						joe_snprintf_0(buf, SIZEOF(buf), "  ");
				else
					if (!piseof(bw->cursor))
						joe_snprintf_1(buf, SIZEOF(buf), "%x", brch(bw->cursor));
					else
						joe_snprintf_0(buf, SIZEOF(buf), "");
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'c':
                if (lf_column_label(bw,buf,sizeof(buf))) { stalin=vsncpy(sv(stalin),sz(buf)); break; }
				if (field)
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%-3" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)(piscol(bw->cursor) + 1));
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%-3ld", (long)(piscol(bw->cursor) + 1));
#endif
				else
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)(piscol(bw->cursor) + 1));
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%ld", (long)(piscol(bw->cursor) + 1));
#endif
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'p':
				if (bw->b->eof->byte >= 1024*1024)
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%3" JOE_INT64_FORMAT "d", ((JOE_LONG_LONG)bw->cursor->byte >> 10) * 100 / ((JOE_LONG_LONG)bw->b->eof->byte >> 10));
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%3ld", ((long)bw->cursor->byte >> 10) * 100 / ((long)bw->b->eof->byte >> 10));
#endif
				else if (bw->b->eof->byte)
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%3" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)bw->cursor->byte * 100 / (JOE_LONG_LONG)bw->b->eof->byte);
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%3ld", (long)bw->cursor->byte * 100 / (long)bw->b->eof->byte);
#endif
				else
					joe_snprintf_0(buf, SIZEOF(buf), "100");
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'l':
				if (field)
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%-4" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)(bw->b->eof->line + 1));
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%-4ld", (long)(bw->b->eof->line + 1));
#endif
				else
#ifdef HAVE_LONG_LONG
					joe_snprintf_1(buf, SIZEOF(buf), "%" JOE_INT64_FORMAT "d", (JOE_LONG_LONG)(bw->b->eof->line + 1));
#else
					joe_snprintf_1(buf, SIZEOF(buf), "%ld", (long)(bw->b->eof->line + 1));
#endif
				for (x = 0; buf[x]; ++x)
					if (buf[x] == ' ')
						buf[x] = fill;
				stalin = vsncpy(sv(stalin), sz(buf));
				break;
			case 'k':
				{
					ptrdiff_t i;
					char *mycpos = buf;

					buf[0] = 0;
					if (w->kbd->x && w->kbd->seq[0])
						for (i = 0; i != w->kbd->x; ++i) {
							char c = w->kbd->seq[i] & 127;

							if (c < 32) {
								mycpos[0] = '^';
								mycpos[1] = (char)(c + '@');
								mycpos += 2;
							} else if (c == 127) {
								mycpos[0] = '^';
								mycpos[1] = '?';
								mycpos += 2;
							} else {
								mycpos[0] = c;
								mycpos += 1;
							}
						}
					*mycpos++ = fill;
					while (mycpos - buf < 4)
						*mycpos++ = fill;
					stalin = vsncpy(sv(stalin), buf, mycpos - buf);
				}
				break;
			case 'S':
				if (bw->b->pid)
					stalin = vsncpy(sv(stalin), sz(joe_gettext(_("*SHELL*"))));
				break;
			case 'M':
				if (recmac) {
					joe_snprintf_1(buf, SIZEOF(buf), joe_gettext(_("(Macro %d recording...)")), recmac->n);
					stalin = vsncpy(sv(stalin), sz(buf));
				}
				break;
			case 'e':
				stalin = vsncpy(sv(stalin), sz(bw->b->o.charmap->name));
				break;
			case 'b':
				stalin = vsncpy(sv(stalin), sz(locale_map->name));
				break;
			case 'w':
				if (!piseof(bw->cursor)) {
					joe_snprintf_1(buf, SIZEOF(buf), "%d", joe_wcwidth(bw->o.charmap->type, brch(bw->cursor)));
					stalin = vsncpy(sv(stalin), sz(buf));
				}
				break;
			default:
				stalin = vsadd(stalin, *s);
			}
		} else
			stalin = vsadd(stalin, *s);
		++s;
	}
	return stalin;
}

static void disptw(W *w, int flg)
{
	BW *bw = (BW *)w->object;
	TW *tw = (TW *)bw->object;
	int newcols = calclincols(bw);
	int linchg = 0;

	if (bw->lincols != newcols) {
		bw->lincols = newcols;
		resizetw(w, w->w, w->h);
		movetw(w, w->x, w->y);
		bwfllw(w);
		linchg = 1;
	}

	if (bw->o.hex) {
		w->cury = TO_DIFF_OK((bw->cursor->byte-bw->top->byte)/16 + bw->y - w->y);
		w->curx = TO_DIFF_OK((bw->cursor->byte-bw->top->byte)%16 + 60 + (lf_active(bw->b) ? 8 : 0) - bw->offset);
	} else if (bwsoft(bw)) {
		int x,y; bwsoft_cursor(bw,&x,&y);
		w->curx=x+newcols; w->cury=y+bw->y-w->y;
	} else {
		w->cury = TO_DIFF_OK(bw->cursor->line - bw->top->line + bw->y - w->y);
		w->curx = TO_DIFF_OK(bw->cursor->xcol - bw->offset + newcols);
	}

	if ((staupd || keepup || bw->cursor->line != tw->prevline || bw->b->changed != tw->changed || bw->b != tw->prev_b) && (w->y || !staen) && w->h > 1) {
		char fill;

		tw->prevline = bw->cursor->line;
		tw->changed = bw->b->changed;
		tw->prev_b = bw->b;
		if (bw->o.rmsg[0])
			fill = bw->o.rmsg[0];
		else
			fill = ' ';
		tw->stalin = stagen(tw->stalin, bw, bw->o.lmsg, fill);
		tw->staright = stagen(tw->staright, bw, bw->o.rmsg, fill);
        { const char *macro_label=vim_macro_status(); if (macro_label) tw->staright=vsncpy(sv(tw->staright),sz(macro_label)); }
        { const char *label = lf_status(bw); if (label) tw->staright = vsncpy(sv(tw->staright),sz(label)); }
#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)
		{
			const char *typography = mijoe_macos_gui_typography();
			if (typography) tw->staright = vsncpy(sv(tw->staright), sz(typography));
		}
#endif
		/* Keep the modified/read-only flags visible even when an absolute
		 * path is longer than the status line. Only clip the path field. */
		if (tw->path_start>=0 && fmtlen(tw->staright)<w->w) {
			ptrdiff_t suffix=fmtlen(tw->stalin)-tw->path_end;
			ptrdiff_t budget=w->w-fmtlen(tw->staright)-tw->path_start-suffix;
			if (budget<0) budget=0;
			if (tw->path_end-tw->path_start>budget) {
				char *tail=vsdupz(tw->stalin+fmtpos(tw->stalin,tw->path_end));
				tw->stalin=vstrunc(tw->stalin,fmtpos(tw->stalin,tw->path_start+budget));
				tw->path_end=fmtlen(tw->stalin);
				tw->stalin=vsncpy(sv(tw->stalin),sv(tail));
			}
		}
		if (fmtlen(tw->staright) < w->w) {
			ptrdiff_t x = fmtpos(tw->stalin, w->w - fmtlen(tw->staright));

			if (x > vslen(tw->stalin))
				tw->stalin = vsfill(sv(tw->stalin), fill, x - vslen(tw->stalin));
			tw->stalin = vsncpy(tw->stalin, fmtpos(tw->stalin, w->w - fmtlen(tw->staright)), sv(tw->staright));
		}
		tw->stalin = vstrunc(tw->stalin, fmtpos(tw->stalin, w->w));
		if (tw->path_end>w->w-fmtlen(tw->staright)) tw->path_end=w->w-fmtlen(tw->staright);
		if (tw->path_end<tw->path_start) tw->path_end=tw->path_start;
		genfmt(w->t->t, w->x, w->y, 0, tw->stalin, bg_stalin, 0, 0);
		/* Highlight literal asterisks in the status line, including %*. */
		{
			SCRN *screen = w->t->t;
			int column;
			for (column = 0; column < w->w; ++column) {
				int position = w->y * screen->co + w->x + column;
				if (screen->scrn[position][0] == '*')
					outatr(locale_map, screen, screen->scrn + position, screen->attr + position,
					       w->x + column, w->y, '*', bg_cursor);
			}
		}
		if (path_current(bw)) {
			ptrdiff_t a=path_anchor<path_focus ? path_anchor : path_focus;
			ptrdiff_t z=path_anchor>path_focus ? path_anchor : path_focus;
			char *safe=path_display(path_value);
			ptrdiff_t start=path_whole ? 0 : txtwidth(locale_map,safe,path_byte(a));
			ptrdiff_t end=path_whole ? tw->path_end-tw->path_start : txtwidth(locale_map,safe,path_byte(z));
			ptrdiff_t at;
			if (end>tw->path_end-tw->path_start) end=tw->path_end-tw->path_start;
			at=w->y*w->t->t->co+w->x+tw->path_start+start;
			if (end>start && tw->path_start>=0)
				genfield(w->t->t,w->t->t->scrn+at,w->t->t->attr+at,w->x+tw->path_start+start,w->y,start,safe,zlen(safe),selectatr,end-start,0,NULL);
			joe_free(safe);
		}
		w->t->t->updtab[w->y] = 0;
	}

	if (flg) {
		if (bw->o.hex)
			bwgenh(bw);
		else
			bwgen(bw, bw->o.linums, linchg);
	}
}

/* Split current window */

static void iztw(TW *tw, ptrdiff_t y)
{
	tw->stalin = NULL;
	tw->staright = NULL;
	tw->changed = -1;
	tw->prevline = -1;
	tw->staon = (!staen || y);
	tw->path_start=tw->path_end=-1;
	tw->prev_b = 0;
}

int usplitw(W *w, int k)
{
	BW *bw;
	ptrdiff_t newh = getgrouph(w);
	W *neww;
	TW *newtw;
	BW *newbw;
	WIND_BW(bw, w);
    if (bw->b->large) { msgnw(w,"Lazy files: use an independent window instead of splitting this buffer."); return -1; }

	dostaupd = 1;
	if (newh / 2 < FITHEIGHT)
		return -1;
	neww = wcreate(w->t, w->watom, findbotw(w), NULL, w, newh / 2 + (newh & 1), NULL);
	if (!neww)
		return -1;
//	wfit(neww->t);
	neww->object = (void *) (newbw = bwmk(neww, bw->b, 0, NULL));
	++bw->b->count;
	newbw->offset = bw->offset;
	newbw->object = (void *) (newtw = (TW *) joe_malloc(SIZEOF(TW)));
	iztw(newtw, neww->y);
	pset(newbw->top, bw->top);
	pset(newbw->cursor, bw->cursor);
	newbw->cursor->xcol = bw->cursor->xcol;
	neww->t->curwin = neww;
	wfit(neww->t);
	return 0;
}

int uduptw(W *w, int k)
{
	BW *bw;
	ptrdiff_t newh = getgrouph(w);
	W *neww;
	TW *newtw;
	BW *newbw;
	WIND_BW(bw, w);
    if (bw->b->large) { msgnw(w,"Lazy files: use an independent window instead of splitting this buffer."); return -1; }

	dostaupd = 1;
	neww = wcreate(w->t, w->watom, findbotw(w), NULL, NULL, newh, NULL);
	if (!neww)
		return -1;
	if (demotegroup(w))
		neww->t->topwin = neww;
	neww->object = (void *) (newbw = bwmk(neww, bw->b, 0, NULL));
	++bw->b->count;
	newbw->offset = bw->offset;
	newbw->object = (void *) (newtw = (TW *) joe_malloc(SIZEOF(TW)));
	iztw(newtw, neww->y);
	pset(newbw->top, bw->top);
	pset(newbw->cursor, bw->cursor);
	newbw->cursor->xcol = bw->cursor->xcol;
	neww->t->curwin = neww;
	wfit(w->t);
	return 0;
}

static void instw(W *w, B *b, off_t l, off_t n, int flg)
{
	BW *bw = (BW *)w->object;
	if (b == bw->b)
		bwins(bw, l, n, flg);
}

static void deltw(W *w, B *b, off_t l, off_t n, int flg)
{
	BW *bw = (BW *)w->object;
	if (b == bw->b)
		bwdel(bw, l, n, flg);
}

WATOM watomtw = {
	"main",
	disptw,
	bwfllw,
	NULL,
	rtntw,
	utypew,
	resizetw,
	movetw,
	instw,
	deltw,
	TYPETW
};

int abortit(W *w, int k)
{
	BW *bw;
	TW *tw;
	B *b;
	
	WIND_BW(bw, w);
	if (bw->parent->watom != &watomtw)
		return wabort(bw->parent);
	if (bw->b->pid && bw->b->count==1)
		return ukillpid(bw->parent, 0);
	w = bw->parent;
	tw = (TW *) bw->object;
	/* If only one main window on the screen... */
	if (countmain(w->t) == 1)
		/* Replace it with an orphaned buffer if there are any */
		if ((b = borphan()) != NULL) {
			void *object = bw->object;
			/* FIXME: Shouldn't we wabort() and wcreate here to kill
			   any prompt windows? */

			bwrm(bw);
			w->object = (void *) (bw = bwmk(w, b, 0, NULL));
			wredraw(bw->parent);
			bw->object = object;
			bw = (BW *)w->object;
			bw->cursor->xcol = piscol(bw->cursor);
			return 0;
		}
	bwrm(bw);
	obj_free(tw->stalin);
	joe_free(tw);
	w->object = NULL;
	wabort(w);	/* Eliminate this window and it's children */
#ifdef JOEWIN
	notify_selection();
#endif
	return 0;
}

static B *wpop(BW *bw)
{
	B *b;
	struct bstack *e = bw->parent->bstack;
	b = e->b;
	if (b->oldcur) prm(b->oldcur);
	if (b->oldtop) prm(b->oldtop);
	b->oldcur = e->cursor;
	b->oldtop = e->top;
	bw->parent->bstack = e->next;
	free(e);
	--b->count;
#ifdef JOEWIN
	notify_selection();
	notify_changed_buffer(b);
#endif
	return b;
}

/* Pop, or do nothing if no window on stack */
int upopabort(W *w, int k)
{
	BW *bw;
	WIND_BW(bw, w);
	if (bw->parent->bstack) {
		int rtn;
		B *b = wpop(bw);
		w = bw->parent;
		rtn = get_buffer_in_window(bw, b);
		bw = (BW *)w->object;
		bw->cursor->xcol = piscol(bw->cursor);
		return rtn;
	} else {
		return 0;
	}
}

/* k is last character types which lead to uabort.  If k is -1, it means uabort
   was called internally, and not by the user: which means uabort will not send
   Ctrl-C to process */
int uabort(W *w, int k)
{
	BW *bw;
	
	if (w->watom != &watomtw)
		return wabort(w);
	WIND_BW(bw, w);
	if (markv(0) && markb->b == bw->b) {
		prm(markk);
		markk = 0;
		updall();
		return 0;
	}
	if (bw->parent->bstack) {
		int rtn;
		B *b = wpop(bw);
		w = bw->parent;
		rtn = get_buffer_in_window(bw, b);
		bw = (BW *)w->object;
		bw->cursor->xcol = piscol(bw->cursor);
		return rtn;
	}
	if (bw->b->pid && bw->b->count==1)
		return ukillpid(bw->parent, 0);
	if (bw->b->changed && bw->b->count == 1 && !bw->b->scratch) {
		int c = query(w, sz(joe_gettext(_("Lose changes to this file (y,n,%{abort})? "))), 0);
		if (!yncheck(yes_key, c))
			return -1;
	}
	if (bw->b->count == 1)
		genexmsg(bw, 0, NULL);
	return abortit(w, NO_MORE_DATA);
}

int ucancel(W *w, int k)
{
	if (w->watom != &watomtw) {
		wabort(w);
		return 0;
	} else
		return uabort(w, k);
}

/* Same as above, but only calls genexmsg if nobody else has */

int uabort1(W *w, int k)
{
	BW *bw;
	
	if (w->watom != &watomtw)
		return wabort(w);
	
	bw = (BW *)w->object;
	if (bw->b->pid && bw->b->count==1)
		return ukillpid(bw->parent, 0);
	if (bw->b->changed && bw->b->count == 1 && !bw->b->scratch) {
		int c = query(w, sz(joe_gettext(_("Lose changes to this file (y,n,%{abort})? "))), 0);
		if (!yncheck(yes_key, c))
			return -1;
	}
	if (!exmsg) genexmsg(bw, 0, NULL);
	return abortit(w, NO_MORE_DATA);
}

/* Abort buffer without prompting: just fail if this is last window on buffer */

int uabortbuf(W *w, int k)
{
	BW *bw;
	B *b;

	WIND_BW(bw, w);

	if (bw->b->pid && bw->b->count==1)
		return ukillpid(w, 0);

	if (okrepl(bw))
		return -1;

	if ((b = borphan()) != NULL) {
		void *object = bw->object;

		bwrm(bw);
		w->object = (void *) (bw = bwmk(w, b, 0, NULL));
		wredraw(bw->parent);
		bw->object = object;
		return 0;
	}

	if (bw->b->count == 1)
		genexmsg(bw, 0, NULL);
	return abortit(w, NO_MORE_DATA);
}

/* Kill current window (orphans buffer) */

int utw0(W *w, int k)
{
	BW *bw;
	w = w->main;
	bw = (BW *)w->object;

	if (w->bstack)
		return uabort(w, -1);
	if (countmain(w->t) == 1)
		return -1;
	if (bw->b->count == 1)
		orphit(bw);
	return uabort(w, -1);
}

/* Kill all other windows (orphans buffers) */

int utw1(W *w, int k)
{
	W *starting = w;
	W *mainw = starting->main;
	Screen *t = mainw->t;
	int myyn;

	do {
		myyn = 0;
	      loop:
		do {
			wnext(t);
		} while (t->curwin->main == mainw && t->curwin != starting);
		if (t->curwin->main != mainw) {
			utw0(t->curwin->main, 0);
			myyn = 1;
			goto loop;
		}
	} while (myyn);
	return 0;
}

void setline(B *b, off_t line)
{
	W *w = maint->curwin;

	do {
		if (w->watom->what == TYPETW) {
			BW *bw = (BW *)w->object;

			if (bw->b == b) {
				off_t oline = bw->top->line;

				/* pline(bw->top, line); */
				pline(bw->cursor, line);
				if (!bw->b->err)
					bw->b->err = pdup(bw->cursor, "setline");
				pline(bw->b->err, line);
				if (w->y >= 0 && bw->top->line > oline && bw->top->line - oline < bw->h)
					nscrlup(w->t->t, bw->y, bw->y + bw->h, (int) (bw->top->line - oline));
				else if (w->y >= 0 && bw->top->line < oline && oline - bw->top->line < bw->h)
					nscrldn(w->t->t, bw->y, bw->y + bw->h, (int) (oline - bw->top->line));
				msetI(bw->t->t->updtab + bw->y, 1, bw->h);
			}
		}
	} while ((w = w->link.next) != maint->curwin);
	/* In case error buffer was orphaned */
	if (errbuf == b && b->oldcur) {
		pline(b->oldcur, line);
		if (!b->err)
			b->err = pdup(b->oldcur, ("setline1"));
		pline(b->err, line);
	}
}

/* Create a text window.  It becomes the last window on the screen */

BW *wmktw(Screen *t, B *b)
{
	W *w;
	BW *bw;
	TW *tw;

	w = wcreate(t, &watomtw, NULL, NULL, NULL, t->h, NULL);
	wfit(w->t);
	w->object = (void *) (bw = bwmk(w, b, 0, NULL));
	bw->object = (void *) (tw = (TW *) joe_malloc(SIZEOF(TW)));
	iztw(tw, w->y);
#ifdef JOEWIN
	notify_new_buffer(b);
#endif
	return bw;
}
