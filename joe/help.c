/*
 *	Help system
 *	Copyright
 *		(C) 1992 Joseph H. Allen
 *		(C) 2001 Marek 'Marx' Grac
 *
 *	This file is part of JOE (Joe's Own Editor)
 */
#include "types.h"

struct help {
	char	*text;		/* help text with attributes */
	int	lines;		/* number of lines */
	struct help	*prev;		/* previous help screen */
	struct help	*next;		/* nex help screen */
	char	*name;		/* context name for context sensitive help */
};

#define NOT_ENOUGH_MEMORY -11

int bg_help;	/* Background color for help */

struct help *help_actual = NULL;	/* actual help screen */
struct help *help_ptr = NULL;		/* build pointer */

struct help_span {
	const char *start;
	ptrdiff_t len;
};

static struct help text_help;
static struct help *saved_help;
static Screen *text_screen;
static const char *text_title, *text_source;
static struct help_span *text_spans;
static ptrdiff_t text_count, text_top, text_rows, text_width, text_height;
static int saved_utf8, saved_visible;

static void help_text_render(Screen *t);

/* 
 * Process help file
 * Returns new line number
 */

int help_init(JFILE *fd,char *bf,int line)
{
	char *buf = vsmk(128);		/* input buffer */

	struct help *tmp;

	if (bf[0] == '{') {			/* start of help screen */
		tmp = (struct help *) joe_malloc(SIZEOF(struct help));

		tmp->text = vsmk(128);
		obj_perm(tmp->text);
		tmp->lines = 0;
		tmp->name = vsncpy(NULL, 0, sz(bf + 1));
		obj_perm(tmp->name);

		while ((jfgets(&buf, fd)) && (buf[0] != '}')) {
			++line;
			tmp->text = vscat(tmp->text, sv(buf));
			tmp->text = vsadd(tmp->text, '\n');
			++tmp->lines;
		}
		tmp->prev = help_ptr;
		tmp->next = NULL;
		if (help_ptr) {
			help_ptr->next = tmp;
		} else {
			help_actual = tmp;
		}
		help_ptr = tmp;
		if (buf[0] == '}') {		/* set new help screen as actual one */
			++line;
		} else {
			logerror_1(joe_gettext(_("\n%d: EOF before end of help text\n")), line);
		}
	}
	return line;
}

/*
 * Find context help - find help entry with the same name
 */

static struct help *find_context_help(const char *name)
{
	struct help *tmp = help_actual;
	if (!tmp) return NULL;

	while (tmp->prev != NULL)	/* find the first help entry */
		tmp = tmp->prev;

	while (tmp != NULL && zcmp(tmp->name, name) != 0)
		tmp = tmp->next;

	return tmp;
}

int help_is_utf8;

/*
 * Display help text
 */
void help_display(Screen *t)
{
	const char *str;
	int y, x, c, z;
	int atr = BG_COLOR(bg_help);
	if (text_screen == t && (text_width != t->w || text_height != t->h))
		help_text_render(t);

	if (help_actual) {
		str = help_actual->text;
	} else {
		str = NULL;
	}

	for (y = skiptop; y != t->wind; ++y) {
		if (t->t->updtab[y]) {
			const char *start = str, *eol;
			ptrdiff_t width=0;
			ptrdiff_t nspans=0;
			ptrdiff_t spanwidth;
			ptrdiff_t spancount=0;
			ptrdiff_t spanextra;
			ptrdiff_t len;

			eol = zchr(str, '\n'); 

			/* First pass: count no. springs \| and determine minimum width */
			while(*str && *str!='\n') {
				if (*str == '\\') {
					++str;
					switch(*str) {
						case 'i':
						case 'I':
						case 'u':
						case 'U':
						case 'd':
						case 'D':
						case 'b':
						case 'B':
						case 'f':
						case 'F':
						case 's':
						case 'S':
						case 'z':
						case 'Z':
							++str;
							break;
						case '|':
							++str;
							++nspans;
							break;
						case 0:
							break;
						default:
							++str;
							++width;
					}
				} else {
					len = eol - str;
					if (help_is_utf8)
						c = utf8_decode_fwrd(&str, &len);
					else {
						c = *(const unsigned char *)str++;
						--len;
					}
					width += joe_wcwidth((help_is_utf8 ? 1 : !!locale_map->type), c);
				}
			}
			str = start;
			/* Now calculate span width */
			if (width >= t->w || nspans==0) {
				spanwidth = 0;
				spanextra = nspans;
			} else {
				spanwidth = (t->w - width)/nspans;
				spanextra = nspans - (t->w - width - nspans*spanwidth);
			}
			/* Second pass: display text */
			for (x = 0; x != t->w; ++x) {
				if (*str == '\n' || !*str) {
					if (eraeol(t->t, x, y, BG_COLOR(bg_help))) {
						return;
					} else {
						break;
					}
				} else {
					if (*str == '\\') {
						switch (*++str) {
						case '|':
							++str;
							for (z=0;z!=spanwidth;++z)
								outatr((help_is_utf8 ? utf8_map : locale_map),t->t,t->t->scrn+x+y*t->w+z,t->t->attr+x+y*t->w+z,x+z,y,' ',atr);
							if (spancount++ >= spanextra) {
								outatr((help_is_utf8 ? utf8_map : locale_map),t->t,t->t->scrn+x+y*t->w+z,t->t->attr+x+y*t->w+z,x+z,y,' ',atr);
								++z;
							}
							x += z-1;
							continue;
						case 'i':
						case 'I':
							atr ^= INVERSE;
							/* Take status line colors for inverse */
							atr = (atr & ~(BG_MASK | FG_MASK)) | ((atr & INVERSE ? bg_stalin : bg_menu) & (BG_MASK | FG_MASK));
							++str;
							--x;
							continue;
						case 'u':
						case 'U':
							atr ^= UNDERLINE;
							++str;
							--x;
							continue;
						case 'd':
						case 'D':
							atr ^= DIM;
							++str;
							--x;
							continue;
						case 'b':
						case 'B':
							atr ^= BOLD;
							++str;
							--x;
							continue;
						case 'l':
						case 'L':
							atr ^= ITALIC;
							++str;
							--x;
							continue;
						case 'f':
						case 'F':
							atr ^= BLINK;
							++str;
							--x;
							continue;
						case 'z':
						case 'Z':
							atr ^= DOUBLE_UNDERLINE;
							++str;
							--x;
							continue;
						case 's':
						case 'S':
							atr ^= CROSSED_OUT;
							++str;
							--x;
							continue;
						case 0:	
							--x;
							continue;
						}
					}
					len = eol - str;
					if (help_is_utf8)
						c = utf8_decode_fwrd(&str, &len);
					else {
						c = *(const unsigned char *)str++;
						--len;
					}

					outatr((help_is_utf8 ? utf8_map : locale_map),
					       t->t, t->t->scrn + x + y * t->w, 
				       	       t->t->attr + x + y * t->w, x, y,
					       c, atr);
					x += (joe_wcwidth((help_is_utf8 ? 1 : !!locale_map->type), c) - 1);
				}
			}
			atr = BG_COLOR(bg_help);
			t->t->updtab[y] = 0;
			outatr_complete(t->t);
		}

		while (*str && *str != '\n')
			++str;
		if (*str == '\n')
			++str;
	}
}

/*
 * Show help screen 
 */
int help_on(Screen *t)
{
	if (help_actual) {
		t->wind = help_actual->lines + skiptop;
		if ((t->h - t->wind) < FITMIN) {
			t->wind = t->h - FITMIN;
		}
		if (t->wind <= skiptop) {
			t->wind = skiptop;
			return -1;
		}
		wfit(t);
		msetI(t->t->updtab + skiptop, 1, t->wind - skiptop);
		return 0;
	} else {
		return -1;
	}
}

/*
 * Hide help screen
 */
static void help_off(Screen *t)
{
	t->wind = skiptop;
	wfit(t);
}

int help_text_active(Screen *t)
{
	return text_screen == t;
}

/* Wrap by display cells, so UTF-8 descriptions remain readable in narrow
 * windows. Store source spans instead of duplicating the complete manual. */
static void help_text_wrap(Screen *t)
{
	const char *p = text_source, *start = p, *next, *end = p + zlen(p);
	ptrdiff_t width = 0, remaining, cells;
	int c;
	if (text_spans) joe_free(text_spans);
	text_spans = (struct help_span *)joe_malloc((zlen(text_source) + 1) * SIZEOF(struct help_span));
	text_count = 0;
	while (*p) {
		if (*p == '\n') {
			text_spans[text_count].start = start;
			text_spans[text_count++].len = p - start;
			start = ++p; width = 0; continue;
		}
		next = p; remaining = end - p;
		c = help_is_utf8 ? utf8_decode_fwrd(&next, &remaining) : (unsigned char)*next++;
		cells = joe_wcwidth(help_is_utf8 ? 1 : !!locale_map->type, c);
		if (cells < 1) cells = 1;
		if (width && width + cells > (t->w > 1 ? t->w - 1 : 1)) {
			text_spans[text_count].start = start;
			text_spans[text_count++].len = p - start;
			start = p; width = 0;
		}
		width += cells; p = next;
	}
	if (p != start || !text_count) {
		text_spans[text_count].start = start;
		text_spans[text_count++].len = p - start;
	}
}

static void help_text_render(Screen *t)
{
	ptrdiff_t available = t->h - FITMIN - skiptop, i, j, last;
	char *out;
	if (text_width != t->w) help_text_wrap(t);
	text_width = t->w; text_height = t->h;
	text_rows = available > 2 ? available - 2 : 1;
	if (text_rows > text_count) text_rows = text_count;
	last = (text_count - 1) / text_rows * text_rows;
	text_top = text_top / text_rows * text_rows;
	if (text_top > last) text_top = last;
	if (text_help.text) joe_free(text_help.text);
	text_help.text = (char *)joe_malloc(2 * zlen(text_source) + 2 * text_count + zlen(text_title) + 256);
	out = text_help.text;
	text_help.lines = 0;
	if (available > 1) {
		strcpy(out, text_title); out += zlen(out); *out++ = '\n';
		++text_help.lines;
	}
	for (i = text_top; i < text_count && i < text_top + text_rows; ++i) {
		for (j = 0; j < text_spans[i].len; ++j) {
			if (text_spans[i].start[j] == '\\') *out++ = '\\';
			*out++ = text_spans[i].start[j];
		}
		*out++ = '\n'; ++text_help.lines;
	}
	if (available > 2) {
		joe_snprintf_2(out, 128, "Page %ld/%ld | PgDn/PgUp | q/Esc\n",
		               (long)(text_top / text_rows + 1), (long)(last / text_rows + 1));
		out += zlen(out); ++text_help.lines;
	}
	*out = 0;
	help_on(t);
}

int help_text_open(W *w, const char *title, const char *text, int utf8)
{
	Screen *t = w->t;
	if (text_screen) help_text_close(text_screen);
	if (t->h - FITMIN - skiptop < 1) return -1;
	saved_help = help_actual; saved_utf8 = help_is_utf8;
	saved_visible = t->wind != skiptop;
	text_screen = t; text_title = title; text_source = text;
	text_top = text_width = text_height = 0;
	help_actual = &text_help; help_is_utf8 = utf8;
	help_text_render(t);
	return 0;
}

int help_text_page(Screen *t, int direction)
{
	if (text_screen != t) return -1;
	if (direction > 0 && text_top + text_rows < text_count) text_top += text_rows;
	if (direction < 0) text_top = text_top >= text_rows ? text_top - text_rows : 0;
	help_text_render(t);
	return 0;
}

void help_text_close(Screen *t)
{
	if (text_screen != t) return;
	text_screen = NULL;
	help_actual = saved_help; help_is_utf8 = saved_utf8;
	joe_free(text_spans); text_spans = NULL;
	joe_free(text_help.text); text_help.text = NULL;
	if (saved_visible) help_on(t); else help_off(t);
	updall();
}

/*
 * Show/hide current help screen
 */
int u_help(W *w, int k)
{
	struct help *new_help;
	if (help_text_active(w->t)) { help_text_close(w->t); return 0; }

	if (w->huh && (new_help = find_context_help(w->huh)) != NULL) {
		if (help_actual != new_help) {
			if (w->t->wind != skiptop)
				help_off(w->t);
			help_actual = new_help;		/* prepare context help */
		}
	}
	if (w->t->wind == skiptop) {
		return help_on(w->t);			/* help screen is hidden, so show the actual one */
	} else {
		help_off(w->t);				/* hide actual help screen */
		return 0;
	}
}

/*
 * Show next help screen (if it is possible)
 */
int u_help_next(W *w, int k)
{
	if (help_text_active(w->t)) return help_text_page(w->t, 1);
	if (help_actual && help_actual->next) {		/* is there any next help screen? */
		if (w->t->wind != skiptop) {
			help_off(w->t);			/* if help screen was visible, then hide it */
		}
		help_actual = help_actual->next;	/* change to next help screen */
		return help_on(w->t);			/* show actual help screen */
	} else {
		return -1;
	}
}

/*
 * Show previous help screen (if it is possible)
 */
int u_help_prev(W *w, int k)
{
	if (help_text_active(w->t)) return help_text_page(w->t, -1);
	if (help_actual && help_actual->prev) {		/* is there any previous help screen? */
		if (w->t->wind != skiptop)
			help_off(w->t);			/* if help screen was visible, then hide it */
		help_actual = help_actual->prev;	/* change to previous help screen */
		return help_on(w->t);			/* show actual help screen */
	} else {
		return -1;
	}
}
