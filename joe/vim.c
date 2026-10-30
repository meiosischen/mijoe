/*
 * Basic Vim interaction on the shared JOE editing engine.
 * No platform API calls belong here: tty.c supplies timed key lookahead.
 */
#include "types.h"
#include "vim.h"
#include "vimhelp.h"

#define VM_NORMAL 0
#define VM_INSERT 1
#define VM_VISUAL 2
#define VM_LINE 3
#define VM_COMMAND 4
#define VM_BLOCK 5
#define COUNT_LIMIT 1000000L
#define COMMAND_SIZE 8192
#define VIM_LITERAL 0x4000000

int vim_enabled = 1;
static int mode;
static unsigned long next_number, active_number;
static long count, op_count;
static int pending, g_pending, find_pending, object_pending;
static int motion_target, last_find_kind, last_find_target;
static P *anchor;
static int block_eol;
static P *block_insert_start;
static off_t block_last_line, block_insert_col;
static int block_insert_append, block_insert_eol, block_prepared;
struct vim_register { B *text; struct charmap *map; int linewise; };
static struct vim_register registers[38];
static int selected_register='"', register_pending, command_register_pending, insert_register_pending;
static char last_ex[COMMAND_SIZE];
static int *sequence, *last_change;
static ptrdiff_t sequence_len, sequence_cap, last_len;
static int recording_change, replaying;
static int paste, paste_insert, paste_match;
static int command_kind;
static char command[COMMAND_SIZE], history[COMMAND_SIZE];
static ptrdiff_t command_len;
static char *search_pattern;
static int search_back;
static int search_highlight;
static struct regcomp *highlight_regex;
static struct charmap *highlight_map;
static char *highlight_pattern;

void vim_highlight_begin(struct vim_highlight *h, BW *bw, P *line)
{
    char *pattern;
    h->scan = NULL; h->start = h->end = -1;
    if (!vim_enabled || !search_highlight || !search_pattern || !*search_pattern ||
        bw->b->internal || bw->b->scratch || bw->o.hex) return;
    if (!highlight_pattern || zcmp(highlight_pattern, search_pattern) || highlight_map != bw->b->o.charmap) {
        if (highlight_regex) joe_regfree(highlight_regex);
        if (highlight_pattern) joe_free(highlight_pattern);
        highlight_pattern = zdup(search_pattern);
        highlight_map = bw->b->o.charmap;
        pattern = my_iconv(NULL, highlight_map, search_pattern, locale_map);
        highlight_regex = joe_regcomp(highlight_map, pattern, zlen(pattern), 0, 1, 0);
        obj_free(pattern);
    }
    if (!highlight_regex || highlight_regex->err) return;
    h->regex = highlight_regex; h->line = line->line;
    h->scan = pdup(line, "vim_highlight");
    p_goto_bol(h->scan);
}

int vim_highlight_test(struct vim_highlight *h, off_t byte)
{
    if (!h->scan || h->line < 0) return 0;
    while (byte >= h->end && h->scan->byte <= byte) {
        off_t candidate;
        int matched;
        if (piseof(h->scan) || h->scan->line != h->line || brc(h->scan) == '\n') { h->line = -1; return 0; }
        candidate = h->scan->byte;
        matched = (!h->regex->prefix_len || brc(h->scan) == (unsigned char)h->regex->prefix[0]) &&
                  !joe_regexec(h->regex, h->scan, 0, NULL, 0);
        if (matched && h->scan->byte > candidate) {
            h->start = candidate; h->end = h->scan->byte;
        } else {
            pgoto(h->scan, candidate);
            if (piseof(h->scan)) { h->line = -1; return 0; }
            pgetc(h->scan);
        }
    }
    return byte >= h->start && byte < h->end;
}

void vim_highlight_end(struct vim_highlight *h)
{
    if (h->scan) prm(h->scan);
}
static int queued_keys[40], queued_count, queued_index;

static void finish_block_insert(BW *bw);

static int visual_mode(void)
{
	return mode == VM_VISUAL || mode == VM_LINE || mode == VM_BLOCK;
}

int vim_getch(void)
{
	if (queued_index < queued_count) return queued_keys[queued_index++];
	queued_index = queued_count = 0;
	return ttgetch();
}

static BW *current_bw(void)
{
	if (leave || !maint || !maint->curwin || !(maint->curwin->watom->what & TYPETW))
		return NULL;
	return (BW *)maint->curwin->object;
}

unsigned long vim_buffer_number(B *b)
{
	if (!b->vim_number)
		b->vim_number = ++next_number;
	return b->vim_number;
}

const char *vim_mode_name(void)
{
	if (!vim_enabled) return "JOE";
	switch (mode) {
	case VM_INSERT: return "I";
	case VM_VISUAL:
	case VM_LINE:
	case VM_BLOCK: return "V";
	case VM_COMMAND: return "C";
	default: return "N";
	}
}

static void message(const char *s)
{
	BW *bw = current_bw();
	if (bw) msgnw(bw->parent, s);
}

static void clear_pending(void)
{
	count = op_count = 0;
	pending = g_pending = find_pending = object_pending = 0;
    selected_register='"'; register_pending=insert_register_pending=0;
	staupd = 1;
}

static void clear_visual(void)
{
	if (anchor) prm(anchor);
	anchor = NULL;
	if (markb) prm(markb);
	if (markk) prm(markk);
	markb = markk = NULL;
	marking = nowmarking = square = 0;
	block_eol = 0;
	updall();
}

void vim_cancel_selection(void)
{
    clear_visual();
    if (visual_mode()) mode = VM_NORMAL;
}

static void normal_cursor(BW *bw)
{
    if (bw->o.hex) { bw->cursor->xcol=piscol(bw->cursor); return; }
	/* Picture mode owns a display column, including positions past EOL. */
	if (bw->o.picture && !visual_mode()) return;
	if (mode != VM_INSERT && mode != VM_BLOCK && piseol(bw->cursor) && !pisbol(bw->cursor))
		prgetc(bw->cursor);
	bw->cursor->xcol = piscol(bw->cursor);
}

static void start_unit(void)
{
	undomark();
	umclear();
}

static void finish_unit(void)
{
	if (block_insert_start) finish_block_insert(current_bw());
	undomark();
	umclear();
	if (recording_change && !replaying && sequence_len) {
		last_change = (int *)joe_realloc(last_change, sequence_len * SIZEOF(int));
		memcpy(last_change, sequence, (size_t)(sequence_len * SIZEOF(int)));
		last_len = sequence_len;
	}
	recording_change = 0;
}

static void record_key(int key)
{
	if (replaying) return;
	if (sequence_len == sequence_cap) {
		sequence_cap = sequence_cap ? sequence_cap * 2 : 64;
		sequence = (int *)joe_realloc(sequence, sequence_cap * SIZEOF(int));
	}
	sequence[sequence_len++] = key;
}

/* Feed complete JOE prefix sequences to the configured keymap.  Terminal
 * escapes, find arguments, Ex input and bracketed paste still belong to Vim. */
int vim_command_active(void) { return mode == VM_COMMAND; }
const char *vim_command_text(void) { return mode==VM_COMMAND ? command : ""; }

int vim_native_key(int key)
{
	BW *bw = current_bw();
	KBD *kbd;
	if (!bw || paste || mode == VM_COMMAND || find_pending || object_pending || vim_grep_active()) return 0;
	kbd = bw->parent->kbd;
	if (key != 11 && key != 20 && kbd->curmap == kbd->topmap) return 0;
	if (help_text_active(bw->t)) help_text_close(bw->t);
	finish_unit(); clear_pending();
	sequence_len = 0;
	if (mode == VM_INSERT) { record_key('i'); start_unit(); }
	return 1;
}

static int writable(BW *bw)
{
	return !bw->o.hex && modify_logic(bw, bw->b);
}

/* The two word classes match Vim's default keyword/punctuation distinction. */
static int word_class(P *p)
{
	int c = brch(p);
	if (c == NO_MORE_DATA || joe_isspace(p->b->o.charmap, c)) return 0;
	return joe_isalnum_(p->b->o.charmap, c) ? 1 : 2;
}

/* Text objects use half-open ranges, independently of the operator/selection.
 * Horizontal white space is an iw object; line breaks separate objects but do
 * not consume a count. WORD joins keyword and punctuation characters. */
static int object_class(P *p, int big)
{
	int c=brch(p), cls=word_class(p);
	if (big) return cls ? 1 : 0;
	/* Vim separates East Asian scripts from adjacent Latin keyword text. */
	if ((c>=0x3300 && c<=0x9fff) || (c>=0xf900 && c<=0xfaff) ||
	    (c>=0x20000 && c<=0x2ffff)) return 3;
	if (c>=0x3040 && c<=0x309f) return 4;
	if (c>=0x30a0 && c<=0x30ff) return 5;
	if (c>=0xac00 && c<=0xd7a3) return 6;
	return cls;
}

static void object_run(P *p, int big, int backward)
{
	P *q = pdup(p, "vim_object_run");
	int cls = object_class(p, big);
	if (backward) {
		while (!pisbol(p)) {
			pset(q, p); prgetc(q);
			if (object_class(q, big) != cls) break;
			pset(p, q);
		}
	} else {
		while (!piseof(p) && !piseol(p) && object_class(p, big) == cls) pgetc(p);
	}
	prm(q);
}

static int object_next(P *p, int backward)
{
	if (backward) {
		if (pisbof(p)) return 0;
		prgetc(p);
		if (piseol(p) && !pisbol(p)) prgetc(p);
	} else {
		if (piseof(p)) return 0;
		if (piseol(p)) pgetc(p);
		if (piseof(p)) return 0;
	}
	return 1;
}

static int word_object(BW *bw, P *a, P *z, int big, int around, long n)
{
	int extending = mode == VM_VISUAL && anchor && anchor->byte != bw->cursor->byte;
	int backward = extending && bw->cursor->byte < anchor->byte;
	P *edge = backward ? a : z;
	P *q = pdup(bw->cursor, "vim_word_object");
	int cls, first = 1, ok = 1, started_word=object_class(q,big)!=0;
	if (piseof(q) && !pisbol(q)) prgetc(q);
	if (!extending) object_run(a, big, 1);
	else {
		if (backward) pset(z, anchor);
		else pset(a, anchor);
		if (backward && !piseof(z)) pgetc(z);
		/* An existing selection extends beyond its active end when that
		 * end already lies on the boundary of the current object. */
		if (backward) {
			P *prev = pdup(q, "vim_object_prev");
			if (!pisbof(prev)) {
				prgetc(prev);
				if (piseol(prev) || object_class(prev,big) != object_class(q,big)) pset(q,prev);
			}
			prm(prev);
		} else {
			P *next = pdup(q, "vim_object_next");
			if (!piseof(next)) {
				pgetc(next);
				if (piseol(next) || object_class(next,big) != object_class(q,big)) pset(q,next);
			}
			prm(next);
		}
	}
	while (n-- > 0) {
		if ((!first && !object_next(q,backward)) || piseof(q)) { ok=0; break; }
		if (!backward && !around && piseol(q) && pisbol(q)) {
			/* Vim counts an empty line as a word boundary. Coming from a
			 * previous word its inclusive end reaches the next character. */
			pgetc(q);
			if (!first && !piseof(q)) pgetc(q);
			first=0; continue;
		}
		if (piseol(q) && !object_next(q,backward)) { ok=0; break; }
		cls = object_class(q,big);
		if (around && !cls) {
			do {
				object_run(q,big,backward);
				if (!object_next(q,backward)) { ok=0; break; }
			} while (!object_class(q,big));
			if (!ok) break;
			cls = object_class(q,big);
			object_run(q,big,backward);
		} else {
			object_run(q,big,backward);
			if (around) {
				P *space = pdup(q,"vim_object_space");
				if (backward && !pisbof(space)) prgetc(space);
				if (!piseol(space) && !object_class(space,big)) {
					object_run(space,big,backward); pset(q,space);
				}
				prm(space);
			}
		}
		first=0;
	}
	if (ok) {
		pset(edge,q);
		if (!backward && z->byte == a->byte && !piseof(z)) pgetc(z);
		if (around && started_word && !extending && !pisbol(a)) {
			P *last=pdup(z,"vim_object_trailing");
			if (!pisbof(last)) prgetc(last);
			if (piseol(last) || object_class(last,big)) {
				pset(last,a); prgetc(last);
				if (!object_class(last,big)) { object_run(last,big,1); if (!pisbol(last)) pset(a,last); }
			}
			prm(last);
		}
	}
	if (!ok) {
		if (piseof(q) && pisbol(q) && !pisbof(q)) prgetc(q);
		pset(bw->cursor,q); normal_cursor(bw);
	}
	prm(q);
	return ok;
}

static void word_forward(P *p)
{
	int cls = word_class(p);
	if (cls) while (!piseof(p) && word_class(p) == cls) pgetc(p);
	while (!piseof(p) && !word_class(p)) pgetc(p);
}

static void word_backward(P *p)
{
	int cls;
	if (pisbof(p)) return;
	prgetc(p);
	while (!pisbof(p) && !word_class(p)) prgetc(p);
	cls = word_class(p);
	while (!pisbof(p)) {
		P *q = pdup(p, "vim_word_backward");
		prgetc(q);
		if (word_class(q) != cls) { prm(q); break; }
		pset(p, q);
		prm(q);
	}
}

static void word_end(P *p)
{
	int cls;
	if (!piseof(p)) pgetc(p);
	while (!piseof(p) && !word_class(p)) pgetc(p);
	cls = word_class(p);
	while (!piseof(p) && word_class(p) == cls) pgetc(p);
	if (!pisbof(p)) prgetc(p);
}

static void first_nonblank(P *p)
{
	p_goto_bol(p);
	while (!piseol(p) && joe_isblank(p->b->o.charmap, brch(p))) pgetc(p);
}

static int find_kind(int key)
{
	return key == 'f' || key == 'F' || key == 't' || key == 'T';
}

/* Search on a copy, so a missing Nth occurrence never partially moves or edits.
 * Repeated till motions skip an adjacent match that would leave us stationary. */
static int find_character(BW *bw, int kind, int target, long n, int repeating)
{
	P *p = pdup(bw->cursor, "vim_find_character");
	int backward = kind == 'F' || kind == 'T';
	int till = kind == 't' || kind == 'T';
	int first = 1;
	off_t original = p->byte;
	while (n) {
		if (backward) {
			if (pisbol(p)) break;
			prgetc(p);
		} else {
			if (piseol(p)) break;
			pgetc(p);
			if (piseol(p)) break;
		}
		if (brch(p) == target) {
			int stationary = 0;
			if (first && repeating && till && n == 1) {
				P *q = pdup(p, "vim_till_repeat");
				if (backward) pgetc(q); else prgetc(q);
				stationary = q->byte == original;
				prm(q);
			}
			first = 0;
			if (!stationary && !--n) {
				if (till) { if (backward) pgetc(p); else prgetc(p); }
				pset(bw->cursor, p);
				bw->cursor->xcol = piscol(bw->cursor);
				prm(p);
				return 1;
			}
		}
	}
	prm(p);
	message("Character not found on this line");
	return -1;
}

/* Matching is independent of JOE's filetype/highlighter configuration. */
static int match_escaped(P *p)
{
	P *q = pdup(p, "vim_match_escape");
	int escaped = 0;
	while (!pisbol(q) && prgetc(q) == '\\') escaped = !escaped;
	prm(q);
	return escaped;
}

/* Keep a line's quote mask while walking in either direction.  ASCII syntax
 * bytes cannot occur inside a UTF-8 continuation, so byte masks also work for
 * Unicode buffers.  Only C character constants, not arbitrary single-quoted
 * strings, are excluded. Odd uncontinued double quotes are ordinary text. */
struct match_line {
	off_t line, start;
	ptrdiff_t len;
	char *text, *mask;
};

static void match_line_load(struct match_line *m, P *p)
{
	P *q = pdup(p, "vim_match_line");
	ptrdiff_t i, j;
	int quotes = 0, inside = 0, continued = 0;
	if (m->text) joe_free(m->text);
	if (m->mask) joe_free(m->mask);
	p_goto_bol(q);
	m->line = q->line; m->start = q->byte;
	p_goto_eol(q);
	m->len = (ptrdiff_t)(q->byte - m->start);
	pgoto(q, m->start);
	m->text = brs(q, m->len);
	m->mask = (char *)joe_calloc(m->len + 1, 1);
	if (!pisbof(q)) {
		prgetc(q);
		if (!pisbof(q) && prgetc(q) == '\\') continued = 1;
	}
	prm(q);
	/* Mark short character constants before counting double quotes. */
	for (i = 0; i < m->len; ++i) {
		if (m->text[i] == '\\' && i + 1 < m->len) { ++i; continue; }
		if (m->text[i] != '\'') continue;
		j = i + 1;
		if (j < m->len && m->text[j] == '\\') ++j;
		if (++j < m->len && m->text[j] == '\'') {
			while (i <= j) m->mask[i++] = 2;
			--i;
		}
	}
	for (i = 0; i < m->len; ++i) {
		if (m->text[i] == '\\' && i + 1 < m->len) { ++i; continue; }
		if (m->text[i] == '"' && !m->mask[i]) ++quotes;
	}
	if ((quotes & 1) && !continued && (!m->len || m->text[m->len - 1] != '\\')) return;
	inside = continued && (quotes & 1);
	for (i = 0; i < m->len; ++i) {
		if (inside) m->mask[i] |= 1;
		if (m->text[i] == '\\' && i + 1 < m->len) {
			if (inside) m->mask[i + 1] |= 1;
			++i;
		} else if (m->text[i] == '"' && !(m->mask[i] & 2)) inside = !inside;
	}
}

static int match_bracket(P *p)
{
	struct match_line m;
	P *start = pdup(p, "vim_match_bracket");
	int c = brch(p), other, backward, escaped = match_escaped(p), found = 0;
	int start_quoted, quoted;
	off_t depth = 0;
	memset(&m, 0, sizeof(m));
	backward = c == ')' || c == ']' || c == '}';
	other = c == '(' ? ')' : c == ')' ? '(' : c == '[' ? ']' : c == ']' ? '[' : c == '{' ? '}' : '{';
	match_line_load(&m, p);
	start_quoted = m.mask[p->byte - m.start] & 1;
	for (;;) {
		if (backward) { if (pisbof(p)) break; prgetc(p); }
		else { if (piseof(p)) break; pgetc(p); }
		if (p->line != m.line) { match_line_load(&m, p); start_quoted = 0; }
		c = brch(p);
		quoted = m.mask[p->byte - m.start];
		if (c == '"' && !match_escaped(p)) start_quoted = 0;
		if ((quoted & 2) || ((quoted & 1) && !start_quoted)) continue;
		if ((c == brch(start) || c == other) && match_escaped(p) == escaped) {
			if (c != other) ++depth;
			else if (!depth) { found = 1; break; }
			else --depth;
		}
	}
	joe_free(m.text); joe_free(m.mask); prm(start);
	return found;
}

/* Return a directive kind, leaving p on its #. */
static int match_directive_kind(P *p)
{
	P *q;
	char word[16];
	int i = 0, c;
	first_nonblank(p);
	if (brch(p) != '#') return 0;
	q = pdup(p, "vim_match_directive_kind");
	pgetc(q);
	while (!piseol(q) && joe_isblank(q->b->o.charmap, brch(q))) pgetc(q);
	while ((c = brch(q)) >= 'a' && c <= 'z' && i < (int)sizeof(word) - 1) {
		word[i++] = (char)c; pgetc(q);
	}
	word[i] = 0;
	prm(q);
	if (!strcmp(word, "if") || !strcmp(word, "ifdef") || !strcmp(word, "ifndef")) return 1;
	if (!strcmp(word, "else") || !strcmp(word, "elif")) return 2;
	return !strcmp(word, "endif") ? 3 : 0;
}

static int match_directive(P *p, int kind)
{
	int backward = kind == 3;
	off_t depth = 0, line;
	for (;;) {
		line = p->line;
		if (backward) { if (!line) return 0; pline(p, line - 1); }
		else {
			pline(p, line + 1);
			if (p->line == line || piseof(p)) return 0;
		}
		kind = match_directive_kind(p);
		if (kind == (backward ? 3 : 1)) ++depth;
		else if (kind == (backward ? 1 : 3)) {
			if (!depth) return 1;
			--depth;
		} else if (kind == 2 && !backward && !depth) return 1;
	}
}

/* A C comment is special only when the cursor is on either delimiter. */
static int match_comment_direction(P *p)
{
	P *q = pdup(p, "vim_match_comment_direction");
	int c = brch(p), next, previous = 0, direction = 0;
	if (!pisbol(q)) previous = prgetc(q);
	pset(q, p); pgetc(q); next = brch(q);
	if (c == '/' && next == '*') { pgetc(p); direction = 1; }
	else if (c == '*' && previous == '/') direction = 1;
	else if (c == '/' && previous == '*') { prgetc(p); direction = -1; }
	else if (c == '*' && next == '/') direction = -1;
	prm(q);
	return direction;
}

static int match_comment(P *p, int direction)
{
	P *q = pdup(p, "vim_match_comment");
	int found = 0;
	for (;;) {
		if (direction < 0) { if (pisbof(p)) break; prgetc(p); }
		else { if (piseof(p)) break; pgetc(p); }
		if (brch(p) != (direction < 0 ? '/' : '*')) continue;
		pset(q, p); pgetc(q);
		if (brch(q) == (direction < 0 ? '*' : '/')) {
			if (direction > 0) pset(p, q);
			found = 1; break;
		}
	}
	prm(q);
	return found;
}

static int percent_motion(BW *bw, long n, int explicit_count, int *linewise)
{
	P *p, *directive;
	int c, kind, direction = 0, found = 0;
	if (explicit_count) {
		off_t lines = bw->b->eof->line;
		off_t target;
		if (n < 1 || n > 100) { message("Percentage must be between 1 and 100"); return -1; }
		if (!lines || !pisbol(bw->b->eof)) ++lines;
		/* Avoid overflowing the XP build's off_t when computing the ceiling. */
		target = (lines / 100) * n + ((lines % 100) * n + 99) / 100;
		pline(bw->cursor, target - 1);
		first_nonblank(bw->cursor);
		*linewise = 1;
		return 1;
	}
	p = pdup(bw->cursor, "vim_percent_scan");
	directive = pdup(p, "vim_percent_directive");
	kind = match_directive_kind(directive);
	if (kind && p->byte <= directive->byte) {
		pset(p, directive); found = match_directive(p, kind); *linewise = 1;
	} else if ((direction = match_comment_direction(p)) != 0) found = match_comment(p, direction);
	else {
		while (!piseol(p)) {
			c = brch(p);
			if (c > 0 && c < 128 && strchr("()[]{}", c)) break;
			pgetc(p);
		}
		if (!piseol(p)) found = match_bracket(p);
		else if (kind) { pset(p, directive); found = match_directive(p, kind); *linewise = 1; }
	}
	if (found) { pset(bw->cursor, p); bw->cursor->xcol = piscol(p); }
	else message("Matching item not found");
	prm(p); prm(directive);
	return found ? 1 : -1;
}

/* 1: moved, 0: unsupported, -1: supported but failed. Failed motions must
 * cancel a pending operator rather than editing at an old/partial endpoint. */
static int motion(BW *bw, int key, long n, int explicit_count, int *linewise, int *inclusive)
{
	P *p = bw->cursor;
	off_t col = p->xcol;
	*linewise = *inclusive = 0;
    if (lf_active(bw->b)) { int result = lf_motion(bw,key,n,explicit_count); if (result) return result; }
	/* Reuse JOE's virtual-column movement for ordinary picture editing.
	 * Operators and selections continue to use their text-range semantics. */
	if (bw->o.picture && !pending && !visual_mode()) {
		const char *command_name = NULL;
		int down = key == 'j' || key == VIM_DOWN;
		int extends = 0;
		switch (key) {
		case 'h': case VIM_LEFT: case VIM_BACK_CHAR: command_name = "ltarw"; break;
		case 'l': case VIM_RIGHT: case VIM_FORWARD_CHAR: command_name = "rtarw"; break;
		case 'j': case VIM_DOWN: command_name = "dnarw"; break;
		case 'k': case VIM_UP: command_name = "uparw"; break;
		}
		if (command_name) {
			while (n--) {
				/* JOE's picture-mode down arrow can append a newline.
				 * Enforce edit protection and group counted moves for undo. */
				if (down && p->line == bw->b->eof->line) {
					if (!writable(bw)) break;
					if (!extends && mode != VM_INSERT) undomark();
					extends = 1;
				}
				if (execmd(findcmd(command_name), 0)) break;
			}
			if (extends && mode != VM_INSERT) undomark();
			return 1;
		}
	}
	switch (key) {
	case 'f': case 'F': case 't': case 'T':
		*inclusive = key == 'f' || key == 't';
		return find_character(bw, key, motion_target, n, 0);
	case ';': case ',':
		{
			int kind = last_find_kind;
			if (!kind) { message("No previous character search"); return -1; }
			if (key == ',') {
				if (kind == 'f') kind = 'F'; else if (kind == 'F') kind = 'f';
				else if (kind == 't') kind = 'T'; else kind = 't';
			}
			*inclusive = kind == 'f' || kind == 't';
			return find_character(bw, kind, last_find_target, n, 1);
		}
	case '%':
		*inclusive = 1;
		return percent_motion(bw, n, explicit_count, linewise);
	case VIM_BACK_CHAR:
        while (n-- && !pisbof(p)) prgetc(p);
        break;
    case VIM_FORWARD_CHAR:
        while (n-- && !piseof(p)) { pgetc(p); if (mode!=VM_INSERT && piseol(p) && !piseof(p)) pgetc(p); }
        break;
	case 'h': case VIM_LEFT:
		while (n-- && !pisbol(p)) prgetc(p);
		break;
	case 'l': case VIM_RIGHT:
		while (n-- && !piseol(p)) pgetc(p);
		break;
	case 'j': case VIM_DOWN:
		pline(p, p->line + n); pcol(p, col); *linewise = 1; return 1;
	case 'k': case VIM_UP:
		pline(p, p->line > n ? p->line - n : 0); pcol(p, col); *linewise = 1; return 1;
	case '0': case VIM_HOME: p_goto_bol(p); break;
	case '^': first_nonblank(p); break;
	case '$': case VIM_END:
		if (n > 1) pline(p, p->line + n - 1);
		p_goto_eol(p);
		if (mode != VM_INSERT && !pisbol(p)) prgetc(p);
		*inclusive = 1; break;
	case 'w': while (n--) word_forward(p); break;
	case 'b': while (n--) word_backward(p); break;
	case 'e': while (n--) word_end(p); *inclusive = 1; break;
	case 'G':
		pline(p, explicit_count ? n - 1 : bw->b->eof->line);
		/* A trailing newline is not an additional Vim text line. */
		if (piseof(p) && pisbol(p) && !pisbof(p)) { prgetc(p); p_goto_bol(p); }
		first_nonblank(p); *linewise = 1; break;
	case 'g': pline(p, explicit_count ? n - 1 : 0); first_nonblank(p); *linewise = 1; break;
	case 21: case VIM_PGUP:
		n *= key == 21 ? (bw->h > 1 ? bw->h / 2 : 1) : bw->h;
		pline(p, p->line > n ? p->line - n : 0); pcol(p, col); return 1;
	case 4: case VIM_PGDN:
		n *= key == 4 ? (bw->h > 1 ? bw->h / 2 : 1) : bw->h;
		pline(p, p->line + n); pcol(p, col); return 1;
	default: return 0;
	}
	p->xcol = piscol(p);
	return 1;
}

/* Convert an inclusive line range into the engine's half-open byte range. */
static void line_range(P *a, P *z)
{
	p_goto_bol(a);
	p_goto_eol(z);
	if (!piseof(z)) pgetc(z);
}

static void inner_block(P *a, P *z)
{
	P *q;
	pgetc(a);
	if (piseol(a) && !piseof(a)) pgetc(a);
	q=pdup(z,"vim_inner_indent"); p_goto_bol(q);
	while (q->byte<z->byte && joe_isblank(q->b->o.charmap,brch(q))) pgetc(q);
	if (q->byte==z->byte && z->line>a->line) p_goto_bol(z);
	prm(q);
	if (a->byte>z->byte) pset(a,z);
}

/* Find the nearest enclosing pair; outside a block Vim first tries the next
 * opener. Work on temporary positions so malformed input never moves cursor. */
static int enclosing_block(P *a, P *z, int open, int close, int next)
{
	P *q=pdup(a,"vim_block_scan");
	struct match_line m;
	int depth=0, found=0, c, quoted, in_string;
	memset(&m,0,sizeof(m)); m.line=-1;
	match_line_load(&m,q); in_string=m.mask[q->byte-m.start]&1;
	if (brch(q)==close && !match_escaped(q) && !pisbof(q)) prgetc(q);
	for (;;) {
		if (q->line!=m.line) match_line_load(&m,q);
		c=brch(q); quoted=m.mask[q->byte-m.start];
		if ((c==open || c==close) && !(quoted&2) && (!(quoted&1) || in_string) && !match_escaped(q)) {
			if (c==close) ++depth;
			else if (depth) --depth;
			else { found=1; break; }
		}
		if (pisbof(q)) break;
		prgetc(q);
	}
	if (!found && next) {
		pset(q,a);
		while (!piseof(q)) {
			if (brch(q)==open && !match_escaped(q)) { found=1; break; }
			pgetc(q);
		}
	}
	if (found) {
		pset(a,q); depth=1; found=0;
		if (q->line!=m.line) match_line_load(&m,q);
		in_string=m.mask[q->byte-m.start]&1;
		while (!piseof(q)) {
			pgetc(q);
			if (q->line!=m.line) match_line_load(&m,q);
			c=brch(q); quoted=m.mask[q->byte-m.start];
			if ((c!=open && c!=close) || (quoted&2) || ((quoted&1) && !in_string) || match_escaped(q)) continue;
			if (c==open) ++depth;
			else if (!--depth) { pset(z,q); found=1; break; }
		}
	}
	if (m.text) joe_free(m.text);
	if (m.mask) joe_free(m.mask);
	prm(q); return found;
}

static int block_object(BW *bw, P *a, P *z, int key, int around, long n)
{
	int open, close, found=0, first=1;
	P *left=pdup(a,"vim_block_left"), *right=pdup(z,"vim_block_right");
	off_t low=0, high=0;
	int extended=mode==VM_VISUAL && anchor && anchor->byte!=bw->cursor->byte;
	if (key=='b' || key=='(' || key==')') { open='('; close=')'; }
	else if (key=='B' || key=='{' || key=='}') { open='{'; close='}'; }
	else if (key=='[' || key==']') { open='['; close=']'; }
	else { open='<'; close='>'; }
	if (extended) { low=markb->byte; high=markk->byte; }
	while (n>0) {
		found=enclosing_block(left,right,open,close,first);
		if (!found) break;
		pset(a,left); pset(z,right);
		if (around) pgetc(z); else inner_block(a,z);
		/* Repeating a selected block grows outwards, unless adding its
 * delimiters already grows the selection at this nesting level. */
		if (!(first && extended && a->byte>=low && z->byte<=high)) --n;
		if (!n) break;
		if (pisbof(left)) { found=0; break; }
		prgetc(left); first=0;
	}
	prm(left); prm(right); return found && !n;
}

/* Quote searches stay on one line. Pairing opening quotes deliberately does
 * not apply escapes; closing quotes use Vim's default backslash escape rule. */
static int quote_seek(P *p, int quote, int backward, int escapes, int step)
{
	if (step) {
		if (backward) { if (pisbol(p)) return 0; prgetc(p); }
		else { if (piseol(p)) return 0; pgetc(p); }
	}
	for (;;) {
		if (brch(p)==quote && (!escapes || !match_escaped(p))) return 1;
		if (backward) { if (pisbol(p)) return 0; prgetc(p); }
		else { if (piseol(p)) return 0; pgetc(p); }
	}
}

static int quote_object(BW *bw, P *a, P *z, int quote, int around, long n)
{
	P *q=pdup(bw->cursor,"vim_quote_scan");
	P *target=pdup(q,"vim_quote_target");
	int extended=mode==VM_VISUAL && anchor && anchor->byte!=q->byte;
	int forward=!extended || anchor->byte<q->byte;
	int inside=0, selected=0, found=0;
	if (visual_mode() && anchor && anchor->line!=q->line) goto done;
	if (extended) {
		pset(q,markb);
		if (!pisbol(q)) { prgetc(q); inside=brch(q)==quote && brch(markk)==quote; }
		pset(q,markb);
		while (q->byte<markk->byte) {
			if (brch(q)==quote) { selected=1; break; }
			pgetc(q);
		}
	}
	pset(q,bw->cursor);
	if (extended && brch(q)==quote) {
		if (forward) {
			if (!quote_seek(q,quote,0,0,1)) goto done;
			pset(a,q);
			if (!quote_seek(q,quote,0,1,1)) { pset(q,a); pset(a,bw->cursor); }
			pset(z,q);
		} else {
			if (!quote_seek(q,quote,1,0,1)) goto done;
			pset(z,q);
			if (!quote_seek(q,quote,1,1,1)) { pset(q,z); pset(z,bw->cursor); }
			pset(a,q);
		}
	} else if (extended || brch(q)==quote) {
		if (extended && !quote_seek(target,quote,!forward,0,0)) goto done;
		p_goto_bol(q);
		for (;;) {
			if (!quote_seek(q,quote,0,0,0) || q->byte>target->byte) goto done;
			pset(a,q);
			if (!quote_seek(q,quote,0,1,1)) goto done;
			pset(z,q);
			if (target->byte<=z->byte) break;
			pgetc(q);
		}
	} else {
		if (!quote_seek(q,quote,1,1,1)) {
			p_goto_bol(q);
			if (!quote_seek(q,quote,0,0,0)) goto done;
		}
		pset(a,q);
		if (!quote_seek(q,quote,0,1,1)) goto done;
		pset(z,q);
	}
	if (around) {
		pset(q,z); pgetc(q);
		if (!piseol(q) && joe_isblank(q->b->o.charmap,brch(q))) {
			while (!piseol(q) && joe_isblank(q->b->o.charmap,brch(q))) { pset(z,q); pgetc(q); }
		} else while (!pisbol(a)) {
			pset(q,a); prgetc(q);
			if (!joe_isblank(q->b->o.charmap,brch(q))) break;
			pset(a,q);
		}
	}
	if (!around && n<2 && (!extended || !inside)) pgetc(a);
	if (around || n>1 || (extended && inside)) pgetc(z);
	if (extended) {
		pset(q,anchor);
		if (forward) {
			if (!pisbol(q)) prgetc(q);
			if (selected || (!inside && (brch(anchor)==quote || brch(q)==quote))) pset(a,anchor);
		} else {
			if (!piseol(q)) pgetc(q);
			if (!inside && (selected || brch(anchor)==quote || brch(q)==quote)) { pset(z,anchor); pgetc(z); }
		}
	}
	found=1;
done:
	prm(q); prm(target); return found;
}

static void block_bounds(BW *bw, off_t *first, off_t *last, off_t *left, off_t *right)
{
	P *p = pdup(anchor, "vim_block_bounds");
	off_t ac = piscol(anchor), cc = piscol(bw->cursor), ae = ac, ce = cc;
	*first = anchor->line < bw->cursor->line ? anchor->line : bw->cursor->line;
	*last = anchor->line > bw->cursor->line ? anchor->line : bw->cursor->line;
	*left = ac < cc ? ac : cc;
	if (!piseol(p)) { pgetc(p); ae = piscol(p); }
	pset(p, bw->cursor);
	if (!piseol(p)) { pgetc(p); ce = piscol(p); }
	*right = ae > ce ? ae : ce;
	if (*right <= *left) *right = *left + 1;
	if (block_eol) {
		off_t line;
		*right = *left + 1;
		for (line = *first; line <= *last; ++line) {
			pline(p, line); p_goto_eol(p);
			if (piscol(p) > *right) *right = piscol(p);
		}
	}
	prm(p);
}

static void update_visual(BW *bw)
{
	P *a, *z;
	if (!anchor || anchor->b != bw->b) return;
	a = pdup(anchor, "vim_visual_a");
	z = pdup(bw->cursor, "vim_visual_z");
	if (mode == VM_BLOCK) {
		off_t first, last, left, right;
		block_bounds(bw, &first, &last, &left, &right);
		pline(a, first); pcol(a, left); a->xcol = left;
		pline(z, last); pcol(z, right); z->xcol = right;
	} else {
		if (a->byte > z->byte) { P *t = a; a = z; z = t; }
		if (mode == VM_LINE) line_range(a, z);
		else if (!piseof(z)) pgetc(z);
	}
	pdupown(a, &markb, "vim_visual_markb");
	pdupown(z, &markk, "vim_visual_markk");
	prm(a); prm(z);
	square = mode == VM_BLOCK;
	marking = nowmarking = 0;
	updall();
}

/* Position an insertion at a display column. Interior tabs become spaces;
 * a partial wide character is retained after padding to the insertion column. */
static int block_insert_position(P *p, off_t col, int pad)
{
	off_t actual;
	pcol(p, col);
	actual = piscol(p);
	if (actual >= col) return 1;
	if (piseol(p) && !pad) return 0;
	if (brch(p) == '\t') {
		P *q = pdup(p, "vim_block_split_tab");
		off_t end;
		pgetc(q); end = piscol(q);
		bdel(p, q); prm(q);
		pfill(p, end, ' ');
		pcol(p, col);
	} else pfill(p, col, ' ');
	return 1;
}

static void drop_block_insert(void)
{
	if (block_insert_start) prm(block_insert_start);
	block_insert_start = NULL;
	block_prepared = 0;
}

static void finish_block_insert(BW *bw)
{
	P *p;
	B *text;
	off_t line;
	if (!bw || !block_insert_start || block_insert_start->b != bw->b || !block_prepared ||
	    block_insert_start->line != bw->cursor->line || block_insert_start->byte >= bw->cursor->byte) {
		drop_block_insert(); return;
	}
	text = bcpy(block_insert_start, bw->cursor);
	p = pdup(bw->cursor, "vim_block_replicate");
	for (line = bw->cursor->line + 1; line <= block_last_line; ++line) {
		pline(p, line);
		if (block_insert_eol) p_goto_eol(p);
		else if (!block_insert_position(p, block_insert_col, block_insert_append)) continue;
		binsb(p, bcpy(text->bof, text->eof));
	}
	prm(p); brm(text);
	drop_block_insert();
	updall();
}

static int begin_block_insert(BW *bw, int append)
{
	off_t first, last, left, right;
	int eol = block_eol;
	if (!writable(bw)) { clear_pending(); return -1; }
	block_bounds(bw, &first, &last, &left, &right);
	start_unit();
	clear_visual();
	block_last_line = last;
	block_insert_col = append ? right : left;
	block_insert_append = append;
	block_insert_eol = append && eol;
	block_prepared = 0;
	pline(bw->cursor, first);
	if (block_insert_eol) p_goto_eol(bw->cursor);
	else pcol(bw->cursor, block_insert_col);
	pdupown(bw->cursor, &block_insert_start, "vim_block_insert_start");
	mode = VM_INSERT;
	clear_pending();
	return 0;
}

static void block_insert_key(BW *bw, int key)
{
	int c = key & ~VIM_LITERAL;
	if (!block_insert_start) return;
	if (c == 13 || c == 10 || key == VIM_LEFT || key == VIM_RIGHT || key == VIM_UP ||
	    key == VIM_DOWN || key == VIM_HOME || key == VIM_END || key == VIM_PGUP || key == VIM_PGDN ||
	    ((key == 8 || key == 127) && bw->cursor->byte <= block_insert_start->byte) || key == VIM_DEL) {
		drop_block_insert(); return;
	}
	if (!block_prepared && ((c >= 32 && c <= 0x10ffff) || c == 9 ||
	    ((key & VIM_LITERAL) && c >= 0 && c <= 0x10ffff) || key == VIM_CLIPBOARD)) {
		if (block_insert_eol) p_goto_eol(bw->cursor);
		else block_insert_position(bw->cursor, block_insert_col, block_insert_append);
		bw->cursor->xcol = piscol(bw->cursor);
		pset(block_insert_start, bw->cursor);
		block_prepared = 1;
	}
}

static int register_index(int name)
{
    if (name=='"') return 0;
    if (name>='0' && name<='9') return 1+name-'0';
    if (name=='-') return 11;
    if (name>='A' && name<='Z') name+='a'-'A';
    if (name>='a' && name<='z') return 12+name-'a';
    return -1;
}
static void register_assign(int index, B *source, struct charmap *map, int linewise, int append)
{
    struct vim_register *value=registers+index;
    if (append && value->text) {
        if (value->map==map) binsb(value->text->eof,bcpy(source->bof,source->eof));
        else {
            char *raw=brs(source->bof,source->eof->byte);
            char *converted=my_iconv(NULL,value->map,raw,map);
            joe_free(raw); binsm(value->text->eof,converted,zlen(converted)); obj_free(converted);
        }
        value->linewise|=linewise;
    } else {
        if (value->text) brm(value->text);
        value->text=bcpy(source->bof,source->eof); value->map=map; value->linewise=linewise;
    }
}
static void register_store(B *source, struct charmap *map, int linewise, int operation, int multiline)
{
    int index=register_index(selected_register), i;
    if (selected_register=='_') return;
    register_assign(0,source,map,linewise,0);
    if (operation=='y') {
        if (selected_register=='"') register_assign(1,source,map,linewise,0);
    } else if (linewise || multiline || selected_register!='"') {
        if (registers[10].text) brm(registers[10].text);
        for (i=10;i>2;--i) registers[i]=registers[i-1];
        memset(registers+2,0,sizeof(registers[2]));
        register_assign(2,source,map,linewise,0);
    } else register_assign(11,source,map,linewise,0);
    if (index>0) {
        register_assign(index,source,map,linewise,selected_register>='A' && selected_register<='Z');
        register_assign(0,registers[index].text,registers[index].map,registers[index].linewise,0);
    }
}
static void yank_range(P *a, P *z, int linewise, int operation)
{
    B *copy=bcpy(a,z);
    if (linewise && copy->eof->byte) {
        P *p=pdup(copy->eof,"vim_yank_tail");
        if (prgetc(p)!='\n') binsm(copy->eof,"\n",1);
        prm(p);
    }
    register_store(copy,a->b->o.charmap,linewise,operation,a->line!=z->line);
    brm(copy);
}
void vim_capture_selection(void)
{
    BW *bw=current_bw();
    int saved=selected_register;
    if (!vim_enabled || !bw || !markv(0) || markb->b!=bw->b) return;
    selected_register='"';
    if (square) {
        B *copy=pextrect(markb,markk->line-markb->line+1,markk->xcol);
        register_store(copy,bw->b->o.charmap,0,'y',1); brm(copy);
    } else yank_range(markb,markk,mode==VM_LINE,'y');
    selected_register=saved;
}
/* Returned text is an object-stack temporary in the requested encoding. */
static char *register_text(BW *bw, int name, struct charmap *map)
{
    int index=register_index(name);
    const char *special=NULL;
    struct charmap *source_map=locale_map;
    if (index>=0 && registers[index].text) {
        char *raw=brs(registers[index].text->bof,registers[index].text->eof->byte);
        char *text=my_iconv(NULL,map,raw,registers[index].map);
        joe_free(raw); return text;
    }
    if (name=='/') special=search_pattern;
    else if (name==':') special=last_ex;
    else if (name=='%') { special=bw->b->name; source_map=utf8_map; }
    else if (name=='_') special="";
    if (special) return my_iconv(NULL,map,special,source_map);
    message(index<0 ? "Register is not supported" : "Register is empty");
    return NULL;
}

#define MACRO_KEY_LIMIT 65536
struct vim_macro_key { int key, decoded; };
static struct vim_macro_key *macro_keys, *macro_capture;
static int macro_length, macro_position, macro_count, macro_capacity;
static int macro_recording, macro_playing, macro_start_mode, capture_start_mode;
static char *inspection_text;

int vim_macro_recording(void) { return macro_recording; }
int vim_macro_playing(void) { return macro_playing; }
int vim_macro_length(void) { return macro_recording ? macro_count : macro_length; }
const char *vim_macro_status(void)
{
    static char text[96];
    if (macro_recording) sprintf(text," REC %d | F5 stop ",macro_count);
    else if (macro_playing) sprintf(text," PLAY %d/%d ",macro_position,macro_length);
    else return NULL;
    return text;
}
static void macro_commit(void)
{
    macro_recording=0;
    if (macro_count) {
        if (macro_keys) joe_free(macro_keys);
        macro_keys=macro_capture; macro_length=macro_count; macro_start_mode=capture_start_mode;
        macro_capture=NULL; macro_capacity=0;
    }
    macro_count=0; staupd=dostaupd=1;
}
void vim_macro_capture(int key, int decoded)
{
    if (!macro_recording || macro_playing || key==VIM_F5 || key==VIM_F6 ||
        (key>=KEY_MDOWN && key<=KEY_MRDRAG)) return;
    if (macro_count==MACRO_KEY_LIMIT) { macro_recording=0; macro_count=0; message("Macro too long: recording discarded; previous macro kept"); staupd=dostaupd=1; return; }
    if (macro_count==macro_capacity) {
        macro_capacity=macro_capacity ? macro_capacity*2 : 64;
        macro_capture=(struct vim_macro_key *)joe_realloc(macro_capture,macro_capacity*sizeof(*macro_capture));
    }
    macro_capture[macro_count].key=key; macro_capture[macro_count++].decoded=decoded;
    staupd=dostaupd=1;
}
int vim_macro_next(int *key, int *decoded)
{
    if (!macro_playing) return 0;
    /* Ctrl+C is unambiguous; leave other typeahead queued until playback ends. */
    if (ttcheck() && havec==3) {
        ttgetch(); macro_playing=0; clear_pending();
        /* Interrupt playback without also invoking window abort. */
        staupd=dostaupd=1; *key=27; *decoded=1; return 1;
    }
    if (macro_position>=macro_length) { macro_playing=0; return 0; }
    *key=macro_keys[macro_position].key; *decoded=macro_keys[macro_position++].decoded;
    return 1;
}
void vim_macro_applied(void)
{
    if (macro_playing && macro_position>=macro_length) { macro_playing=0; staupd=dostaupd=1; }
}
void vim_macro_result(int result)
{
    if (result && macro_playing) {
        macro_playing=0; paste=paste_match=paste_insert=0;
        clear_pending(); staupd=dostaupd=1;
    }
}
static int macro_toggle(void)
{
    char text[128];
    if (macro_playing) { message("Cannot record during macro playback"); return -1; }
    if (macro_recording) {
        int length=macro_count;
        macro_commit();
        sprintf(text,length ? "Macro saved: %d keys; F6 plays, :macro shows keys" : "No keys recorded; previous macro kept",length);
        message(text); return 0;
    }
    if (mode==VM_COMMAND || pending || find_pending || object_pending || g_pending || register_pending || insert_register_pending || count || selected_register!='"') {
        message("Finish the pending command or press Esc before recording"); return -1;
    }
    if (help_text_active(maint)) help_text_close(maint);
    macro_count=0; capture_start_mode=mode; macro_recording=1;
    message("REC: F5 stops recording; :macro shows captured keys"); staupd=dostaupd=1;
    return 0;
}
static int macro_play(void)
{
    if (macro_recording) { message("Press F5 to stop recording before F6 playback"); return -1; }
    if (macro_playing) { message("Recursive macro playback is not allowed"); return -1; }
    if (!macro_length) { message("No recorded macro; F5 starts recording"); return -1; }
    if (mode!=macro_start_mode) { message("Switch to the mode in which recording started before F6"); return -1; }
    if (help_text_active(maint)) help_text_close(maint);
    clear_pending(); macro_position=0; macro_playing=1; staupd=dostaupd=1;
    message("Playing macro; Ctrl+C interrupts"); return 0;
}
static const char *macro_key_name(int key, char *text)
{
    const char *name=NULL;
    switch (key) {
    case 27: return "<Esc>"; case 13: case 10: return "<Enter>";
    case 9: return "<Tab>"; case 8: case 127: return "<Backspace>";
    case 32: return "<Space>";
    case VIM_LEFT: name="<Left>"; break; case VIM_RIGHT: name="<Right>"; break;
    case VIM_UP: name="<Up>"; break; case VIM_DOWN: name="<Down>"; break;
    case VIM_HOME: name="<Home>"; break; case VIM_END: name="<End>"; break;
    case VIM_PGUP: name="<PageUp>"; break; case VIM_PGDN: name="<PageDown>"; break;
    case VIM_DEL: name="<Delete>"; break;
    case VIM_PASTE: name="<PasteBegin>"; break; case VIM_PASTE_END: name="<PasteEnd>"; break;
    }
    if (name) return name;
    if (key>=0 && key<32) { sprintf(text,"<C-%c>",key+'@'); return text; }
    if (key>=32 && key<=0x10ffff) { int n=utf8_encode(text,key); text[n]=0; return text; }
    sprintf(text,"<Key-%x>",key); return text;
}
static int inspection_open(BW *bw, const char *title, char *text)
{
    if (help_text_active(bw->t)) help_text_close(bw->t);
    if (inspection_text) obj_free(inspection_text);
    inspection_text=text;
    return help_text_open(bw->parent,title,inspection_text,1);
}

char *vim_buffer_list_text(void)
{
    BW *bw=current_bw();
    B *b;
    char *list=vsdupz("Number  % current  + modified  R readonly  L lazy\n\n");
    obj_perm(list);
    for (b=bufs.link.next;b!=&bufs;b=b->link.next) if (!b->internal && !b->scratch) {
        char flags[96], escaped[16], *name;
        ptrdiff_t i;
        sprintf(flags,"%6lu  %c%c%c%c  ",vim_buffer_number(b),bw && b==bw->b ? '%' : ' ',b->changed ? '+' : ' ',b->rdonly ? 'R' : ' ',lf_active(b) ? 'L' : ' ');
        list=vsncpy(sv(list),sz(flags));
        name=b->name ? my_iconv(NULL,utf8_map,b->name,locale_map) : vsdupz("[No Name]");
        for (i=0;name[i];++i) {
            unsigned char c=(unsigned char)name[i];
            if (c<32 || c==127) { sprintf(escaped,"<0x%02x>",c); list=vsncpy(sv(list),sz(escaped)); }
            else list=vsadd(list,(char)c);
        }
        obj_free(name); list=vsadd(list,'\n');
    }
    return list;
}

#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)
static int show_buffer_list(BW *bw, const char *path)
{
    FILE *input=fopen(path,"rb");
    struct stat status;
    char *raw,*text;
    size_t length;
    if (!input) return -1;
    if (fstat(fileno(input),&status) || (status.st_mode&S_IFMT)!=S_IFREG || status.st_size<0 || status.st_size>16L*1024*1024) { fclose(input); return -1; }
    length=(size_t)status.st_size; raw=(char *)joe_malloc(length+1);
    if (fread(raw,1,length,input)!=length) { joe_free(raw); fclose(input); return -1; }
    fclose(input); raw[length]=0;
    text=vsdupz(raw); obj_perm(text); joe_free(raw);
    mode=VM_NORMAL;
    return inspection_open(bw,"MiJoe buffers - all app windows",text);
}
#endif
static int macro_show(BW *bw)
{
    char *text=vsdupz("F5: record/stop | F6: playback | Ctrl+C: interrupt\n\n");
    struct vim_macro_key *keys=macro_recording ? macro_capture : macro_keys;
    int length=macro_recording ? macro_count : macro_length, i;
    char label[160], token[48];
    obj_perm(text);
    sprintf(label,"State: %s | Keys: %d | Start mode: %s\n\n",macro_recording ? "RECORDING" : "saved",length,
            (macro_recording ? capture_start_mode : macro_start_mode)==VM_INSERT ? "Insert" :
            (macro_recording ? capture_start_mode : macro_start_mode)==VM_NORMAL ? "Normal" : "Visual");
    text=vsncpy(sv(text),sz(label));
    if (!length) text=vsncpy(sv(text),sz("No macro captured yet.\n"));
    for (i=0;i<length;++i) {
        const char *name=macro_key_name(keys[i].key,token);
        sprintf(label,"%5d  %s  ",i+1,keys[i].decoded ? "Vim" : "JOE");
        text=vsncpy(sv(text),sz(label)); text=vsncpy(sv(text),sz(name)); text=vsadd(text,'\n');
    }
    return inspection_open(bw,"MiJoe macro - recorded keys",text);
}
static int registers_show(BW *bw, const char *filter)
{
    const char *names="\"0123456789-abcdefghijklmnopqrstuvwxyz_:/%";
    const char *name;
    char *text=vsdupz("Name  Type  Bytes  Contents (control characters escaped)\n\n");
    int i;
    obj_perm(text);
    for (i=0;filter[i];++i) if (!strchr(names,filter[i]) && !(filter[i]>='A' && filter[i]<='Z')) {
        obj_free(text); message("Use :reg with names such as 0, a, \" or /"); return -1;
    }
    for (name=names;*name;++name) {
        int index=register_index(*name), present=0, truncated=0;
        char label[128], *value=NULL;
        const char *source=NULL;
        ptrdiff_t j, length;
        if (*filter && !strchr(filter,*name) && !(*name>='a' && *name<='z' && strchr(filter,*name-'a'+'A'))) continue;
        if (index>=0 && registers[index].text) {
            B *b=registers[index].text;
            off_t amount=b->eof->byte;
            char *raw;
            if (amount>16384) { amount=16384; truncated=1; }
            raw=brs(b->bof,amount); value=my_iconv(NULL,utf8_map,raw,registers[index].map); joe_free(raw); present=1;
            sprintf(label,"\"%c  %s  %" JOE_INT64_FORMAT "d  ",*name,registers[index].linewise ? "line" : "char",(JOE_LONG_LONG)b->eof->byte);
        } else {
            if (*name=='/') source=search_pattern;
            else if (*name==':') source=last_ex;
            else if (*name=='%') source=bw->b->name;
            else if (*name=='_') source="";
            if (source) { value=my_iconv(NULL,utf8_map,source,*name=='%' ? utf8_map : locale_map); present=*source!=0; }
            sprintf(label,"\"%c  text  %ld  ",*name,(long)(value ? zlen(value) : 0));
        }
        if (!present && !*filter) { if (value) obj_free(value); continue; }
        text=vsncpy(sv(text),sz(label));
        if (!value || !*value) text=vsncpy(sv(text),sz("[empty]"));
        length=value ? zlen(value) : 0;
        for (j=0;j<length;++j) {
            unsigned char c=(unsigned char)value[j];
            if (c=='\n') text=vsncpy(sv(text),sz("<NL>"));
            else if (c=='\r') text=vsncpy(sv(text),sz("<CR>"));
            else if (c=='\t') text=vsncpy(sv(text),sz("<Tab>"));
            else if (c<32 || c==127) { sprintf(label,"<0x%02x>",c); text=vsncpy(sv(text),sz(label)); }
            else text=vsadd(text,(char)c);
        }
        if (truncated) text=vsncpy(sv(text),sz(" ... [preview limited to 16 KiB]"));
        text=vsadd(text,'\n'); if (value) obj_free(value);
    }
    return inspection_open(bw,"MiJoe registers - :reg [names]",text);
}

static int operate(BW *bw, P *a, P *z, int op, int linewise)
{
	P *d;
	int tail, ending = 0;
	if (a->byte == z->byte && op != 'c') return 0;
	if (op != 'y' && !writable(bw)) return -1;
	yank_range(a, z, linewise, op);
	if (op == 'y') {
		pset(bw->cursor, a);
		return 0;
	}
	/* dd on the final unterminated line removes its preceding line break. */
	tail = linewise && piseof(z) && !pisbol(z) && !pisbof(a);
	if (linewise && op == 'c' && z->byte > a->byte) {
		P *last = pdup(z, "vim_change_ending");
		ending = prgetc(last) == '\n';
		prm(last);
	}
	d = pdup(a, "vim_delete_start");
	if (tail && op == 'd') prgetc(d);
	bdel(d, z);
	pset(bw->cursor, d);
	if (linewise && op == 'c' && (ending || !piseof(bw->cursor))) {
		binsc(bw->cursor, '\n');
	}
	prm(d);
	recording_change = 1;
	if (op == 'c') mode = VM_INSERT;
	else if (linewise) first_nonblank(bw->cursor);
	bw->cursor->xcol = piscol(bw->cursor);
	return 0;
}

static int put_register(BW *bw, int before, long n)
{
	P *start;
	char *converted = NULL;
	int add_newline = 0;
    int index=register_index(selected_register);
    B *reg=index>=0 ? registers[index].text : NULL;
    struct charmap *reg_map=index>=0 ? registers[index].map : NULL;
    int reg_line=index>=0 ? registers[index].linewise : 0;
	if (!reg) { message("Register is empty"); return -1; }
	if (!writable(bw)) return -1;
	if (reg_map != bw->b->o.charmap) {
		char *text = brs(reg->bof, reg->eof->byte);
		converted = my_iconv(NULL, bw->b->o.charmap, text, reg_map);
		joe_free(text);
	}
	if (reg_line) {
		if (before) p_goto_bol(bw->cursor);
		else {
			p_goto_eol(bw->cursor);
			if (!piseof(bw->cursor)) pgetc(bw->cursor);
			else if (!pisbol(bw->cursor) || !pisbof(bw->cursor)) add_newline = 1;
		}
	} else if (!before && !piseol(bw->cursor)) pgetc(bw->cursor);
	if (add_newline) { binsc(bw->cursor, '\n'); pgetc(bw->cursor); }
	start = pdup(bw->cursor, "vim_put");
	start->end = 0;
	while (n--) {
		off_t len = converted ? zlen(converted) : reg->eof->byte;
		if (converted) binsm(bw->cursor, converted, zlen(converted));
		else binsb(bw->cursor, bcpy(reg->bof, reg->eof));
		pfwrd(bw->cursor, len);
	}
	if (reg_line) { pset(bw->cursor, start); first_nonblank(bw->cursor); }
	else if (reg->eof->line>0) pset(bw->cursor,start);
	else if (!pisbof(bw->cursor)) prgetc(bw->cursor);
	bw->cursor->xcol = piscol(bw->cursor);
	prm(start);
	if (converted) obj_free(converted);
	recording_change = 1;
	return 0;
}

static void enter_insert(BW *bw, int key)
{
	if (!writable(bw)) return;
	start_unit();
	switch (key) {
	case 'a':
		if (!piseol(bw->cursor)) { pgetc(bw->cursor); bw->cursor->xcol = piscol(bw->cursor); }
		else if (bw->o.picture) ++bw->cursor->xcol;
		break;
	case 'I': first_nonblank(bw->cursor); bw->cursor->xcol = piscol(bw->cursor); break;
	case 'A': p_goto_eol(bw->cursor); bw->cursor->xcol = piscol(bw->cursor); break;
	case 'o':
		p_goto_eol(bw->cursor);
		bw->cursor->xcol = piscol(bw->cursor);
		if (!execmd(findcmd("rtn"), 13)) recording_change = 1;
		break;
	case 'O':
		p_goto_bol(bw->cursor);
		binsc(bw->cursor, '\n');
		bw->cursor->xcol = 0;
		recording_change = 1;
		break;
	}
	mode = VM_INSERT;
	clear_pending();
}

static int search(BW *bw, int backwards, long n)
{
	char *pattern;
	struct regcomp *compiled;
	P *p;
	int found = 0;
	if (!search_pattern || !*search_pattern) { message("No previous search"); return -1; }
    if (lf_active(bw->b)) {
        search_highlight = 1;
        return lf_search(bw,search_pattern,backwards,n);
    }
	pattern = my_iconv(NULL, bw->b->o.charmap, search_pattern, locale_map);
	compiled = joe_regcomp(bw->b->o.charmap, pattern, zlen(pattern), 0, 1, 0);
	obj_free(pattern);
	nredraw(maint->t);
	if (compiled->err) { message(compiled->err); joe_regfree(compiled); return -1; }
	search_highlight = 1;
	p = pdup(bw->cursor, "vim_search");
	while (n--) {
		off_t start = p->byte;
		found = 0;
		do {
			if (backwards) {
				if (pisbof(p)) p_goto_eof(p); else prgetc(p);
			} else {
				if (piseof(p)) p_goto_bof(p); else pgetc(p);
			}
			{
				off_t candidate = p->byte;
				int matched = !joe_regexec(compiled, p, 0, NULL, 0);
				/* The engine consumes p while matching; Vim stays at the start. */
				pgoto(p, candidate);
				if (matched) { found = 1; break; }
			}
		} while (p->byte != start);
		if (!found) break;
	}
	if (found) { pset(bw->cursor, p); bw->cursor->xcol = piscol(bw->cursor); }
	else message("Pattern not found");
	prm(p);
	joe_regfree(compiled);
	return found ? 0 : -1;
}

/* Vim *: prefer a keyword on/after the cursor, otherwise a nonblank WORD. */
static int search_word(BW *bw, long n)
{
    P *a = pdup(bw->cursor, "vim_star_start"), *z, *q;
    char *raw, *word, *pattern;
    ptrdiff_t i;
    int keyword = word_class(a) == 1;
    if (!keyword) {
        while (!piseol(a) && word_class(a) != 1) pgetc(a);
        keyword = !piseol(a);
        if (!keyword) {
            pset(a, bw->cursor);
            while (!piseol(a) && !word_class(a)) pgetc(a);
            if (piseol(a)) { prm(a); message("No word under cursor"); return -1; }
        }
    }
    while (!pisbol(a)) {
        q = pdup(a, "vim_star_previous");
        prgetc(q);
        if (keyword ? word_class(q) != 1 : word_class(q) != 2) { prm(q); break; }
        pset(a, q); prm(q);
    }
    z = pdup(a, "vim_star_end");
    while (!piseol(z) && (keyword ? word_class(z) == 1 : word_class(z) == 2)) pgetc(z);
    if (z->byte - a->byte >= COMMAND_SIZE - 4) {
        prm(a); prm(z); message("Word is too long to search"); return -1;
    }
    raw = brs(a, z->byte - a->byte);
    word = my_iconv(NULL, locale_map, raw, bw->b->o.charmap);
    /* Freeing a temporary object also frees newer objects: keep word independent. */
    obj_perm(word);
    joe_free(raw);
    pattern = vsdupz(keyword ? "\\<" : "");
    for (i = 0; word[i]; ++i) {
        if (strchr("\\.^$[]*+?(){}|", (unsigned char)word[i])) pattern = vsadd(pattern, '\\');
        pattern = vsadd(pattern, word[i]);
    }
    if (keyword) pattern = vsncpy(sv(pattern), sz("\\>"));
    obj_free(word);
    if (zlen(pattern) >= COMMAND_SIZE) {
        obj_free(pattern); prm(a); prm(z); message("Word is too long to search"); return -1;
    }
    if (search_pattern) joe_free(search_pattern);
    search_pattern = zdup(pattern); obj_free(pattern); search_back = 0;
    pset(bw->cursor, a);
    prm(a); prm(z);
    return search(bw, 0, n);
}

static B *numbered_buffer(unsigned long number)
{
	B *b;
	for (b = bufs.link.next; b != &bufs; b = b->link.next)
		if (!b->internal && !b->scratch && vim_buffer_number(b) == number) return b;
	return NULL;
}

static int switch_buffer(BW *bw, B *b)
{
	int rc;
	clear_visual();
	finish_unit();
	mode = VM_NORMAL;
	clear_pending();
	rc = get_buffer_in_window(bw, b);
	active_number = vim_buffer_number(b);
	return rc;
}

static int open_file(BW *bw, const char *name)
{
	B *b;
	int error, rc;
	b = bfind(name);
	error = berror;
	if (error && error != -1) {
		message(joe_gettext(msgs[-error]));
		brm(b);
		return -1;
	}
	rc = switch_buffer(bw, b);
	brm(b); /* bfind and get_buffer_in_window each acquired a reference. */
#ifdef JOEWIN
	notify_new_buffer(((BW *)maint->curwin->object)->b);
#endif
	return rc;
}

static int quit_window(BW *bw, int force_quit)
{
	B *b;
	if (!force_quit) {
		if (bw->b->changed && bw->b->count == 1) {
			message("No write since last change (use :q! to discard)"); return -1;
		}
		if (countmain(bw->t) == 1)
			for (b = bufs.link.next; b != &bufs; b = b->link.next)
				if (!b->internal && !b->scratch && b->changed) {
					message("An unsaved buffer remains (use :ls)"); return -1;
				}
	}
	clear_visual();
	if (countmain(bw->t) == 1) leave = 1;
	else abortit(bw->parent, -1);
	return 0;
}

/* One filename argument: preserve Windows backslashes and embedded spaces. */
static char *filename_arg(char *s)
{
	ptrdiff_t n;
	while (*s == ' ' || *s == '\t') ++s;
	n = zlen(s);
	if ((*s == '"' || *s == '\'') && (n < 2 || s[n-1] != *s)) return NULL;
	if (n >= 2 && ((*s == '"' && s[n-1] == '"') || (*s == '\'' && s[n-1] == '\''))) {
		s[n-1] = 0;
		++s;
	}
	return s;
}

static int color_menu(BW *bw)
{
	char **names = get_colors();
	ptrdiff_t i, selected = 0;
	if (!names || !valen(names)) { message("No color schemes found"); return -1; }
	for (i = 0; i < valen(names); ++i)
		if (scheme_name && !zcmp(names[i], scheme_name)) selected = i;
	message("Color scheme: arrows/PgUp/PgDn, Enter to apply, Ctrl+C to cancel");
	if (choose(bw->parent, bw->parent, names, &selected, 0) != 0) return -1;
	return set_color_scheme(bw->parent, names[selected]);
}

#define GREP_LIMIT 10000
struct grep_hit { JOE_LONG_LONG row, offset, start, end; char *text; };
static struct {
    BW *bw;
    struct grep_hit *hits;
    int count, selected, top, capped, left, widest;
    JOE_LONG_LONG origin, origin_top;
    off_t xcol, scroll;
} grep_view;

int vim_grep_active(void) { return grep_view.bw!=NULL; }
int vim_grep_count(void) { return grep_view.count; }
int vim_grep_selected(void) { return grep_view.selected; }
int vim_grep_column(void) { return grep_view.left; }

/* Keep row numbers fixed while every result shares one text viewport. */
static int grep_text_columns(int *gutter)
{
    int width, digits=1;
    JOE_LONG_LONG row;
    if (!grep_view.bw || !grep_view.count) { *gutter=0; return 0; }
    width=(int)grep_view.bw->t->w*3/4;
    if (width<16) width=(int)grep_view.bw->t->w;
    if (width<4) { *gutter=0; return 0; }
    row=grep_view.hits[grep_view.count-1].row;
    while (row>=10) { ++digits; row/=10; }
    *gutter=digits+2;
    if (*gutter>width-3) *gutter=width-3;
    return width-2-*gutter;
}

int vim_grep_max_column(void)
{
    int gutter, columns=grep_text_columns(&gutter);
    return columns && grep_view.widest>columns ? grep_view.widest-columns : 0;
}

static int grep_emit(JOE_LONG_LONG row, JOE_LONG_LONG offset, JOE_LONG_LONG start, JOE_LONG_LONG end, const char *text, void *context)
{
    struct grep_hit *hit;
    char *clean;
    ptrdiff_t i, length=zlen(text), columns;
    (void)context;
    if (length>1024) length=1024;
    clean=(char *)joe_malloc(length+1);
    for (i=0;i<length;++i) clean[i]=(unsigned char)text[i]<32 || text[i]==127 ? ' ' : text[i];
    clean[length]=0;
    if (locale_map->type) columns=txtwidth(locale_map,clean,length);
    else {
        char *display=my_iconv(NULL,locale_map,clean,utf8_map);
        columns=txtwidth(locale_map,display,zlen(display)); obj_free(display);
    }
    if (columns>grep_view.widest) grep_view.widest=(int)columns;
    if (!(grep_view.count%64)) grep_view.hits=(struct grep_hit *)joe_realloc(grep_view.hits,(grep_view.count+64)*sizeof(*hit));
    hit=grep_view.hits+grep_view.count++;
    hit->row=row; hit->offset=offset; hit->start=start; hit->end=end; hit->text=clean;
    if (grep_view.count==GREP_LIMIT) { grep_view.capped=1; return 1; }
    return 0;
}
static void grep_close(int cancel)
{
    BW *bw=grep_view.bw;
    int i;
    if (!bw) return;
    if (cancel) {
        if (lf_active(bw->b)) {
            if (!lf_goto_absolute(bw,grep_view.origin)) {
                JOE_LONG_LONG relative=grep_view.origin_top-lf_absolute(bw->b,0);
                if (relative>=0 && relative<=bw->b->eof->byte) pgoto(bw->top,(off_t)relative);
            }
        } else { pgoto(bw->cursor,(off_t)grep_view.origin); pgoto(bw->top,(off_t)grep_view.origin_top); }
        bw->cursor->xcol=grep_view.xcol; bw->offset=grep_view.scroll;
    }
    for (i=0;i<grep_view.count;++i) joe_free(grep_view.hits[i].text);
    if (grep_view.hits) joe_free(grep_view.hits);
    memset(&grep_view,0,sizeof(grep_view));
    ttputs("\033[?25h");
    nredraw(maint->t); staupd=dostaupd=1;
}
static void grep_preview(void)
{
    BW *bw=grep_view.bw;
    JOE_LONG_LONG offset=grep_view.hits[grep_view.selected].offset;
    if (lf_active(bw->b)) lf_goto_absolute(bw,offset);
    else pgoto(bw->cursor,(off_t)offset);
    bw->cursor->xcol=piscol(bw->cursor);
    nredraw(bw->t->t); staupd=dostaupd=1;
}
int vim_grep_key(int key)
{
    int step=1, next=grep_view.selected;
    if (!grep_view.bw) return 0;
    if (key==27 || key==3 || key=='q') { grep_close(1); return 1; }
    if (key==13 || key==10) { grep_close(0); return 1; }
    if (key==VIM_LEFT || key==VIM_RIGHT) {
        int limit=vim_grep_max_column(), left=grep_view.left;
        if (left>limit) left=limit;
        if (key==VIM_RIGHT && left<limit) ++left;
        if (key==VIM_LEFT && left>0) --left;
        if (left!=grep_view.left) { grep_view.left=left; staupd=dostaupd=1; }
        return 1;
    }
    if (key==VIM_PGDN || key==VIM_PGUP) step=maint->h*2/3-3;
    if (step<1) step=1;
    if (key==VIM_DOWN || key==14 || key=='j' || key==VIM_PGDN) next+=step;
    if (key==VIM_UP || key==16 || key=='k' || key==VIM_PGUP) next-=step;
    if (key==VIM_HOME || key=='g') next=0;
    if (key==VIM_END || key=='G') next=grep_view.count-1;
    if (next<0) next=0;
    if (next>=grep_view.count) next=grep_view.count-1;
    if (next!=grep_view.selected) { grep_view.selected=next; grep_preview(); }
    return 1;
}
int vim_grep_render(Screen *screen)
{
    SCRN *t=screen->t;
    int width, height, x, y, row, visible, gutter, columns, limit;
    char label[192];
    if (!grep_view.bw || grep_view.bw->t!=screen) return 0;
    width=(int)screen->w*3/4; height=(int)screen->h*2/3;
    if (width<16) width=(int)screen->w;
    if (height<5) height=(int)screen->h;
    if (width<4 || height<4) return 0;
    columns=grep_text_columns(&gutter); limit=vim_grep_max_column();
    if (grep_view.left>limit) grep_view.left=limit;
    x=((int)screen->w-width)/2; y=((int)screen->h-height)/2;
    visible=height-3;
    if (grep_view.selected<grep_view.top) grep_view.top=grep_view.selected;
    if (grep_view.selected>=grep_view.top+visible) grep_view.top=grep_view.selected-visible+1;
    for (row=0;row<height;++row) {
        int attribute=bg_menu;
        int (*cells)[COMPOSE]=t->scrn+(y+row)*screen->w+x;
        int *attrs=t->attr+(y+row)*screen->w+x;
        int index=grep_view.top+row-1;
        const char *body="";
        int result_row=row>0 && row<height-2 && index<grep_view.count;
        if (!row) {
            sprintf(label,"Grep %d/%d%s | X %d",grep_view.selected+1,grep_view.count,grep_view.capped ? " (limit)" : "",grep_view.left+1); body=label; attribute=(bg_menu & ~FG_MASK)|FG_BCYAN|BOLD;
        } else if (row==height-1) body="Up/Down ^P/^N | Left/Right: pan";
        else if (row==height-2) body="Enter: keep preview | Esc: restore";
        else if (result_row) {
            body=grep_view.hits[index].text;
            if (index==grep_view.selected) attribute=selectatr;
        }
        if (!locale_map->type) body=my_iconv(NULL,locale_map,body,utf8_map);
        if (result_row) {
            sprintf(label,"%*" JOE_INT64_FORMAT "d: ",gutter>2 ? gutter-2 : 1,grep_view.hits[index].row);
            genfield(t,cells+1,attrs+1,x+1,y+row,0,label,zlen(label),attribute,gutter,0,NULL);
            genfield(t,cells+1+gutter,attrs+1+gutter,x+1+gutter,y+row,grep_view.left,body,zlen(body),attribute,columns,0,NULL);
        } else genfield(t,cells+1,attrs+1,x+1,y+row,0,body,zlen(body),attribute,width-2,0,NULL);
        outatr(locale_map,t,cells,attrs,x,y+row,!row || row==height-1 ? '+' : '|',bg_menu);
        outatr(locale_map,t,cells+width-1,attrs+width-1,x+width-1,y+row,!row || row==height-1 ? '+' : '|',bg_menu);
    }
    cpos(t,x+1,y+1+grep_view.selected-grep_view.top);
    return 1;
}
#define GREP_TEXT_LIMIT (64L*1024*1024)

/* Generated text is an ordinary unnamed, editable buffer: it participates in
 * unsaved-buffer protection, and saving never overwrites its transfer file. */
static int import_text_result(BW *bw, const char *path, int new_window)
{
    FILE *input=fopen(path,"rb");
    B *b;
    struct stat status;
    int old_guess, error;
    if (!input) return -1;
    if (fstat(fileno(input),&status) || (status.st_mode&S_IFMT)!=S_IFREG || status.st_size<0 || status.st_size>GREP_TEXT_LIMIT) { fclose(input); return -1; }
    old_guess=guess_utf16; guess_utf16=0;
    b=bread(fileno(input),GREP_TEXT_LIMIT+1); error=berror || b->eof->byte>GREP_TEXT_LIMIT; guess_utf16=old_guess; fclose(input);
    if (error) { brm(b); return -1; }
    setopt(b,"");
    b->internal=b->scratch=b->rdonly=0; b->changed=1;
    b->o.charmap=utf8_map; b->o.map_name="utf-8"; b->o.readonly=b->o.hex=b->o.crlf=b->o.ansi=0;
    b->o.syntax=NULL; b->o.syntax_name=NULL; b->o.highlight=0;
    if (new_window) {
        BW *next=wmktw(bw->t,b);
        bw->t->curwin=next->parent; wshowone(next->parent);
    } else { error=get_buffer_in_window(bw,b); brm(b); }
    mode=VM_NORMAL; clear_pending(); staupd=dostaupd=1;
    return error ? -1 : 0;
}

static int grep_export(BW *bw)
{
    char *path=mktmp(NULL);
    FILE *output;
    JOE_LONG_LONG written=0;
    int i, result=0, handed=0;
    if (!path) { message("Could not create grep transfer file"); return -1; }
    output=fopen(path,"wb");
    if (!output) { unlink(path); obj_free(path); message("Could not write grep transfer file"); return -1; }
    for (i=0;i<grep_view.count && !result;++i) {
        struct grep_hit *hit=grep_view.hits+i;
        char label[64];
        int length=sprintf(label,"%" JOE_INT64_FORMAT "d: ",hit->row);
        if (written+length+1>GREP_TEXT_LIMIT) { result=-4; break; }
        if (fwrite(label,1,length,output)!=(size_t)length) { result=-1; break; }
        written+=length;
        if (lf_active(bw->b)) result=lf_export_text(bw,hit->start,hit->end,output,&written,GREP_TEXT_LIMIT-1);
        else {
            P *p=pdup(bw->cursor,"grep_export");
            char bytes[65536]; ptrdiff_t used=0;
            pgoto(p,(off_t)hit->start);
            while (p->byte<hit->end && !result) {
                char encoded[8]; off_t at=p->byte;
                int c=pgetc(p); ptrdiff_t amount;
                if (!at && c==0xfeff) continue;
                amount=utf8_encode(encoded,c);
                memcpy(bytes+used,encoded,amount); used+=amount;
                if (used>=(ptrdiff_t)sizeof(bytes)-8 || p->byte==hit->end) {
                    if (written+used>=GREP_TEXT_LIMIT) result=-4;
                    else if (fwrite(bytes,1,used,output)!=(size_t)used) result=-1;
                    else written+=used;
                    used=0;
                    if (!result && ttcheck()) { int key=ttgetch(); if (key==27 || key==3) result=-2; else nungetc(key); }
                }
            }
            prm(p);
        }
        if (!result) { if (fputc('\n',output)==EOF) result=-1; else ++written; }
    }
    if (!result && grep_view.capped) {
        const char *notice="[grep: results limited to 10000 matching lines]\n";
        if (written+zlen(notice)>GREP_TEXT_LIMIT) result=-4;
        else if (fputs(notice,output)==EOF) result=-1;
    }
    if (fclose(output)) result=-1;
    grep_close(0);
    if (!result) {
#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)
        handed=mijoe_macos_gui_open_text(path);
#endif
        if (!handed) {
#ifdef JOEWIN
            result=import_text_result(bw,path,1);
#else
            /* Console results belong to the current window's buffer list.
             * Native macOS already handed off to a new app window above. */
            result=import_text_result(bw,path,0);
#endif
        }
    }
    if (!handed) unlink(path);
    obj_free(path);
    if (result) message(result==-4 ? "Grep output exceeds 64 MiB; narrow the expression" : result==-2 ? "Grep export cancelled" : "Could not open grep text results");
    return result ? -1 : 0;
}

static int grep_open(BW *bw, const char *expression, int to_file)
{
    P *line, *scan, *end;
    struct regcomp *compiled;
    char *pattern;
    int result=0;
    long checked=0;
    if (!*expression || bw->o.hex) { message("Use :grep [-f] expression in a text file"); return -1; }
    clear_visual(); finish_unit(); clear_pending(); mode=VM_NORMAL;
    grep_view.bw=bw;
    grep_view.origin=lf_absolute(bw->b,bw->cursor->byte);
    grep_view.origin_top=lf_absolute(bw->b,bw->top->byte);
    grep_view.xcol=bw->cursor->xcol; grep_view.scroll=bw->offset;
    if (lf_active(bw->b)) {
        result=lf_grep(bw,expression,grep_emit,NULL);
        if (result<0) { grep_close(1); message(result==-3 ? "Lazy grep accepts literal text; regex requires normal opening" : result==-2 ? "Grep cancelled" : "Grep failed: source changed or read error"); return -1; }
    } else {
        pattern=my_iconv(NULL,bw->b->o.charmap,expression,locale_map);
        compiled=joe_regcomp(bw->b->o.charmap,pattern,zlen(pattern),0,1,0); obj_free(pattern);
        if (compiled->err) { message(compiled->err); joe_regfree(compiled); grep_close(1); return -1; }
        line=pdup(bw->b->bof,"grep_line"); scan=pdup(line,"grep_scan"); end=pdup(line,"grep_end");
        for (;;) {
            int found=0;
            off_t candidate=0;
            pset(end,line); p_goto_eol(end); pset(scan,line);
            while (scan->byte<=end->byte) {
                candidate=scan->byte;
                if ((!compiled->prefix_len || brc(scan)==(unsigned char)compiled->prefix[0]) && !joe_regexec(compiled,scan,0,NULL,0) && scan->byte<=end->byte) found=1;
                pgoto(scan,candidate);
                if (found || piseol(scan)) break;
                pgetc(scan);
                if (!(++checked%1024) && ttcheck()) {
                    int key=ttgetch();
                    if (key==27 || key==3) { result=-2; break; }
                    nungetc(key);
                }
            }
            if (result<0) break;
            if (found) {
                off_t amount=end->byte-line->byte;
                char *raw, *text;
                if (amount>1000) amount=1000;
                raw=brs(line,amount); text=my_iconv(NULL,utf8_map,raw,bw->b->o.charmap); joe_free(raw);
                result=grep_emit((JOE_LONG_LONG)line->line+1,(JOE_LONG_LONG)candidate,(JOE_LONG_LONG)line->byte,(JOE_LONG_LONG)end->byte,text,NULL); obj_free(text);
                if (result) break;
            }
            if (!(++checked%64) && ttcheck()) {
                int key=ttgetch();
                if (key==27 || key==3) { result=-2; break; }
                nungetc(key);
            }
            if (piseof(end)) break;
            pset(line,end); pgetc(line);
        }
        prm(line); prm(scan); prm(end); joe_regfree(compiled);
        if (result<0) { grep_close(1); message("Grep cancelled"); return -1; }
    }
    if (!grep_view.count) { grep_close(1); message("Grep: no matching lines"); return 0; }
    if (to_file) return grep_export(bw);
    if (search_pattern) joe_free(search_pattern);
    search_pattern=zdup(expression); search_highlight=1; search_back=0;
    ttputs("\033[?25l");
    grep_preview();
    return 0;
}

int vim_ex(W *w, const char *line)
{
	BW *bw = (BW *)w->object;
	char *copy = zdup(line), *cmd = copy, *arg, *end;
	int bang = 0, rc = 0;
	while (*cmd == ':' || *cmd == ' ' || *cmd == '\t') ++cmd;
	end = cmd;
	while (*end && *end != ' ' && *end != '\t' && *end != '!') ++end;
	if (*end == '!') { bang = 1; *end++ = 0; }
	else if (*end) *end++ = 0;
	arg = filename_arg(end);
	if (!arg) { message("Unterminated filename quote"); rc = -1; goto done; }
	/* The existing file engine's dequote buffer is 1024 bytes. */
	if (zlen(arg) >= 1000) { message("Filename argument is too long"); rc = -1; goto done; }
	if (!*cmd) goto done;
	if (!zcmp(cmd,"reg") || !zcmp(cmd,"registers")) {
        if (bang) { message("Use :reg [names]"); rc=-1; } else rc=registers_show(bw,arg);
    } else if (!zcmp(cmd,"macro") || !zcmp(cmd,"macros")) {
        if (bang || *arg) { message("Use :macro to view recorded keys"); rc=-1; } else rc=macro_show(bw);
    } else if (!zcmp(cmd,"grep")) {
        int to_file=!strncmp(arg,"-f",2) && (!arg[2] || arg[2]==' ' || arg[2]=='\t');
        if (to_file) { arg+=2; while (*arg==' ' || *arg=='\t') ++arg; }
        if (!strncmp(arg,"--",2) && (!arg[2] || arg[2]==' ' || arg[2]=='\t')) { arg+=2; while (*arg==' ' || *arg=='\t') ++arg; }
        arg=filename_arg(arg);
        if (bang || !arg) { message("Use :grep [-f] expression"); rc=-1; }
        else rc=grep_open(bw,arg,to_file);
    } else if (!zcmp(cmd, "help") || !zcmp(cmd, "h")) {
		if (*arg || bang) { message("Use :help to list supported commands"); rc = -1; }
		else rc = help_text_open(w, "MiJoe :help - supported commands", vim_help_text, 1);
	} else if (!zcmp(cmd, "colors") || !zcmp(cmd, "colorscheme")) {
		if (bang) { message("Trailing characters"); rc = -1; }
		else rc = *arg ? set_color_scheme(w, arg) : color_menu(bw);
	} else if (!zcmp(cmd, "w") || !zcmp(cmd, "write") || !zcmp(cmd, "saveas") || !zcmp(cmd, "sav") || !zcmp(cmd, "wq") || !zcmp(cmd, "x")) {
		int rename = !zcmp(cmd, "saveas") || !zcmp(cmd, "sav");
		int quitting = !zcmp(cmd, "wq") || !zcmp(cmd, "x");
		if (rename && !*arg) { message("A filename is required"); rc = -1; }
		else if (!zcmp(cmd, "x") && !bw->b->changed && !*arg) rc = quit_window(bw, bang);
		else {
			rc = vim_write_file(w, *arg ? arg : NULL, rename, bang);
			if (!rc && quitting) rc = quit_window((BW *)w->object, 0);
		}
	} else if (!zcmp(cmd, "q") || !zcmp(cmd, "quit")) {
		if (*arg) { message("Trailing characters"); rc = -1; }
		else rc = quit_window(bw, bang);
	} else if (!zcmp(cmd, "e") || !zcmp(cmd, "edit")) {
		if (!*arg || bang) { message("Use :e filename; forced reload is not implemented"); rc = -1; }
		else rc = open_file(bw, arg);
	} else if (!zcmp(cmd, "enew")) {
		if (*arg || bang) { message("Trailing characters"); rc = -1; }
		else rc = open_file(bw, NULL);
	} else if (!zcmp(cmd, "bn") || !zcmp(cmd, "bnext") || !zcmp(cmd, "bp") || !zcmp(cmd, "bprevious")) {
		B *b = bw->b;
		int backwards = cmd[1] == 'p';
		if (*arg || bang) { message("Trailing characters"); rc = -1; goto done; }
		do {
			b = backwards ? b->link.prev : b->link.next;
		} while (b != bw->b && (b == &bufs || b->internal || b->scratch));
		rc = switch_buffer(bw, b);
	} else if (!zcmp(cmd, "b") || !zcmp(cmd, "buffer")) {
		unsigned long number = 0;
		char *p = arg;
		B *b;
		while (*p >= '0' && *p <= '9' && number <= 100000000UL) number = number * 10 + *p++ - '0';
		b = *p || !number || bang ? NULL : numbered_buffer(number);
		if (!b) { message("No such buffer number (use :ls)"); rc = -1; }
		else rc = switch_buffer(bw, b);
	} else if (!zcmp(cmd, "ls") || !zcmp(cmd, "buffers")) {
		if (*arg || bang) { message("Trailing characters"); rc = -1; goto done; }
#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)
        if (!mijoe_macos_gui_list_buffers())
#endif
            rc=inspection_open(bw,"MiJoe buffers - :b number",vim_buffer_list_text());
	} else if (!zcmp(cmd, "bd") || !zcmp(cmd, "bdelete")) {
		int changed = bw->b->changed;
		B *b = bw->b;
		if (*arg) { message("Use :b number before :bd"); rc = -1; }
		else if (changed && !bang) { message("No write since last change (use :bd!)"); rc = -1; }
		else {
			clear_visual();
			if (bang) b->changed = 0;
			rc = ulose(w, 0);
			if (rc) b->changed = changed;
			active_number = 0;
		}
	} else if (!zcmp(cmd, "noh") || !zcmp(cmd, "nohlsearch")) {
        if (*arg || bang) { message("Trailing characters"); rc = -1; }
        else { search_highlight = 0; nredraw(maint->t); }
	} else if (!zcmp(cmd, "set")) {
		if (!zcmp(arg, "vim")) vim_enabled = 1;
		else if (!zcmp(arg, "novim")) {
			clear_visual(); finish_unit(); mode = VM_NORMAL; vim_enabled = 0;
		} else { message("Supported options: vim, novim"); rc = -1; }
	} else if (*cmd >= '0' && *cmd <= '9') {
		long n = 0;
		char *p = cmd;
		while (*p >= '0' && *p <= '9' && n <= COUNT_LIMIT) n = n * 10 + *p++ - '0';
		if (*p || *arg || n < 1 || n > COUNT_LIMIT) { message("Invalid line number"); rc = -1; }
		else if (lf_active(bw->b)) rc = lf_line_jump(bw,n-1);
        else { pline(bw->cursor, (off_t)(n - 1)); first_nonblank(bw->cursor); }
	} else {
		message("Command not implemented"); rc = -1;
	}
done:
	joe_free(copy);
	return rc;
}

static void command_display(void)
{
	char *text = vsdupz("");
	text = vsadd(text, (char)command_kind);
	text = vsncpy(sv(text), command, command_len);
    if (command_register_pending) text=vsadd(text,'"');
	message(text);
	obj_free(text);
}

static int command_key(BW *bw, int key)
{
    if (command_register_pending && key!=3) {
        char *text;
        ptrdiff_t i, length;
        command_register_pending=0;
        if (key==27) { command_display(); return 0; }
        if (key==18) { command_register_pending=1; command_display(); return 0; }
        text=register_text(bw,key,locale_map);
        if (!text) { command_display(); return -1; }
        length=zlen(text);
        if (command_len+length>=COMMAND_SIZE) { obj_free(text); message("Register exceeds command line capacity"); return -1; }
        for (i=0;i<length;++i) {
            unsigned char c=(unsigned char)text[i];
            if (c=='\r' && i+1<length && text[i+1]=='\n') ++i;
            command[command_len++]=c<32 || c==127 ? ' ' : c;
        }
        command[command_len]=0; obj_free(text); command_display(); return 0;
    }
    if (key==18) { command_register_pending=1; command_display(); return 0; }

	if (key == 27 || key == 3) { mode = VM_NORMAL; clear_pending(); return 0; }
	if (key == 13 || key == 10) {
		mode = VM_NORMAL;
		strcpy(history, command);
		if (command_kind == ':') { strcpy(last_ex,command); return vim_ex(bw->parent, command); }
		if (*command) {
			if (search_pattern) joe_free(search_pattern);
			search_pattern = zdup(command);
			search_back = command_kind == '?';
		}
		return search(bw, search_back, 1);
	}
	if (key == 8 || key == 127) {
		if (!command_len) { mode = VM_NORMAL; return 0; }
		do { --command_len; } while (locale_map->type && command_len && ((unsigned char)command[command_len] & 0xc0) == 0x80);
		command[command_len] = 0;
	} else if (key == VIM_UP) {
		strcpy(command, history); command_len = zlen(command);
	} else if (key >= 32 && key <= 0x10ffff) {
		char encoded[8];
		ptrdiff_t len;
		if (locale_map->type) len = utf8_encode(encoded, key);
		else { int c = from_uni(locale_map, key); if (c < 0) return -1; encoded[0] = (char)c; len = 1; }
		if (command_len + len >= COMMAND_SIZE) { message("Command line is too long"); return -1; }
		memcpy(command + command_len, encoded, (size_t)len);
		command_len += len; command[command_len] = 0;
	}
	command_display();
	return 0;
}

static int dispatch(BW *bw, int key);

static int repeat_change(BW *bw, long n)
{
	int *keys;
	ptrdiff_t i, len = last_len;
	int rc = 0;
	if (!len) { message("No previous change"); return -1; }
	keys = (int *)joe_malloc(len * SIZEOF(int));
	memcpy(keys, last_change, (size_t)(len * SIZEOF(int)));
	replaying = 1;
	while (n-- && !rc) for (i = 0; i < len && !rc; ++i) rc = dispatch(bw, keys[i]);
	replaying = 0;
	joe_free(keys);
	return rc;
}

static int apply_text_object(BW *bw, int key, long n)
{
	P *a = pdup(bw->cursor,"vim_object_a");
	P *z = pdup(bw->cursor,"vim_object_z");
	int rc=0, ok=0, op=pending;
	int backward=anchor && bw->cursor->byte < anchor->byte;
	int linewise=0;
	off_t original=bw->cursor->byte;
	if (key=='w' || key=='W') ok=word_object(bw,a,z,key=='W',object_pending=='a',n);
	else if (key=='"' || key=='\'' || key=='`') ok=quote_object(bw,a,z,key,object_pending=='a',n);
	else if (key>0 && key<128 && strchr("()[]{}<>bB",key)) ok=block_object(bw,a,z,key,object_pending=='a',n);
	if (!op && ok && a->byte==z->byte && key!='"' && key!='\'' && key!='`') ok=0;
	if (!ok) {
		if (visual_mode()) { clear_visual(); mode=VM_NORMAL; }
		prm(a); prm(z); clear_pending(); message("Text object not found or not supported"); return -1;
	}
	if (op) {
		if (key>0 && key<128 && strchr("()[]{}<>bB",key) && object_pending=='i' && z->line>a->line && pisbol(z)) {
			if (pisbol(a)) linewise=1;
			else prgetc(z);
		}
		pset(bw->cursor,a); start_unit();
		rc=operate(bw,a,z,op,linewise);
		if (rc) pgoto(bw->cursor,original);
		clear_pending(); if (mode!=VM_INSERT) finish_unit(); normal_cursor(bw);
	} else {
		mode=VM_VISUAL; block_eol=0;
		if (backward) {
			pset(bw->cursor,a); if (z->byte>a->byte) prgetc(z);
			pdupown(z,&anchor,"vim_object_anchor");
		} else {
			pdupown(a,&anchor,"vim_object_anchor");
			if (z->byte>a->byte || (z->byte==a->byte && !pisbol(z))) prgetc(z); pset(bw->cursor,z);
		}
		clear_pending(); bw->cursor->xcol=piscol(bw->cursor); update_visual(bw);
	}
	prm(a); prm(z); return rc;
}

static int dispatch(BW *bw, int key)
{
	long n;
	int linewise = 0, inclusive = 0, rc = 0, resolved_find = 0, moved;
	P *a, *z;
	/* User paging pair: Ctrl+U up, Ctrl+Shift+U down (encoded as PgDn).
	 * Prefix arguments and command-line editing keep their own input. */
	if (key==21 && mode!=VM_COMMAND && !find_pending && !object_pending) key=VIM_PGUP;
    if (vim_grep_key(key)) return 0;
	if (help_text_active(bw->t)) {
		if (key == 27 || key == 3 || key == 'q') help_text_close(bw->t);
		else if (key == VIM_PGDN || key == ' ' || key == 13 || key == 10)
			help_text_page(bw->t, 1);
		else if (key == VIM_PGUP || key == 'b') help_text_page(bw->t, -1);
		return 0;
	}
	if (bw->t->wind != skiptop && mode != VM_INSERT && mode != VM_COMMAND) {
		if (key == VIM_PGDN) return u_help_next(bw->parent, 0);
		if (key == VIM_PGUP) return u_help_prev(bw->parent, 0);
	}
	if (mode == VM_COMMAND) return command_key(bw, key);
    if (!find_pending) {
        switch (key) {
        case 1: key=VIM_HOME; break;
        case 5: key=VIM_END; break;
        case 16: key=VIM_UP; break;
        case 14: key=VIM_DOWN; break;
        case 2: key=VIM_BACK_CHAR; break;
        case 6: key=VIM_FORWARD_CHAR; break;
        }
    }
	if (!replaying && mode == VM_NORMAL && !pending && !g_pending && !find_pending && !object_pending && !count && !register_pending && selected_register=='"') sequence_len = 0;
	record_key(key);
	if (key == 3) {
		int selected = markv(0) && markb->b == bw->b;
		if (mode == VM_INSERT) finish_block_insert(bw);
		finish_unit(); clear_pending(); sequence_len = 0;
		clear_visual(); mode = VM_NORMAL;
		/* Match JOE's abort: dismiss a selection first, then use its
		 * shared-view, dirty-buffer and orphan-buffer exit rules. */
		if (selected) return 0;
		return uabort(bw->parent, 3);
	}
	if (key == 27) {
		if (mode == VM_INSERT) {
			finish_unit();
			if (bw->o.picture) u_goto_left(bw->parent, 0);
			else if (!pisbol(bw->cursor)) prgetc(bw->cursor);
		}
		if (visual_mode()) clear_visual();
		mode = VM_NORMAL;
		clear_pending(); normal_cursor(bw);
		return 0;
	}
    if (mode!=VM_INSERT && register_pending) {
        register_pending=0;
        if (register_index(key)<0 && key!='_') { clear_pending(); message("Invalid register name"); return -1; }
        selected_register=key;
        { char echo[112]; sprintf(echo,"\"%c [register selected: y/d/c, p/P]",key); message(echo); }
        return 0;
    }
    if (mode!=VM_INSERT && key=='"' && !pending && !find_pending && !object_pending) { register_pending=1; message("\" [register? 0-9 a-z - _; Esc cancels]"); return 0; }
	if (mode == VM_INSERT) {
        if (insert_register_pending) {
            char *text;
            ptrdiff_t length;
            insert_register_pending=0;
            text=register_text(bw,key,bw->b->o.charmap);
            if (!text) return -1;
            if (!writable(bw)) { obj_free(text); return -1; }
            drop_block_insert(); length=zlen(text);
            binsm(bw->cursor,text,length); pfwrd(bw->cursor,length); obj_free(text);
            bw->cursor->xcol=piscol(bw->cursor); recording_change=1; return 0;
        }
        if (key==18) { insert_register_pending=1; return 0; }
		block_insert_key(bw, key);
		if (key & VIM_LITERAL) {
			rc = execmd(findcmd("type"), key & ~VIM_LITERAL);
			if (!rc) recording_change = 1;
			return rc;
		}
		switch (key) {
		case 13: case 10: rc = execmd(findcmd("rtn"), 13); break;
		case 8: case 127: rc = execmd(findcmd("backs"), 8); break;
		case VIM_DEL: rc = execmd(findcmd("delch"), 0); break;
		case VIM_BACK_CHAR: case VIM_FORWARD_CHAR:
		case VIM_LEFT: case VIM_RIGHT: case VIM_UP: case VIM_DOWN:
		case VIM_HOME: case VIM_END: case VIM_PGUP: case VIM_PGDN:
			motion(bw, key, 1, 0, &linewise, &inclusive); return 0;
		case VIM_CLIPBOARD:
#ifdef JOEWIN
			rc = execmd(findcmd("winpaste"), 0);
#else
			message("Use terminal paste in Insert mode"); rc = -1;
#endif
			break;
		default:
			if ((key >= 32 && key <= 0x10ffff) || key == 9) rc = execmd(findcmd("type"), key);
			else return 0;
		}
		if (!rc) recording_change = 1;
		return rc;
	}
	if (object_pending) {
		n=count ? count : 1;
		if (op_count) {
			if (n>COUNT_LIMIT/op_count) { clear_pending(); message("Count is too large"); return -1; }
			n*=op_count;
		}
		return apply_text_object(bw,key,n);
	}
	if (find_pending) {
		if (key < 0 || key > 0x10ffff) {
			clear_pending(); message("Expected a character"); return -1;
		}
		motion_target = key;
		key = find_pending;
		if (!replaying) { last_find_target = motion_target; last_find_kind = key; }
		find_pending = 0;
		resolved_find = 1;
	}
	if (key >= '0' && key <= '9' && (key != '0' || count)) {
		if (count > (COUNT_LIMIT - (key - '0')) / 10) { clear_pending(); message("Count is too large"); return -1; }
		count = count * 10 + key - '0';
		return 0;
	}
	n = count ? count : 1;
	if (op_count) {
		if (n > COUNT_LIMIT / op_count) { clear_pending(); message("Count is too large"); return -1; }
		n *= op_count;
	}
	if (g_pending) {
		g_pending = 0;
		if (key != 'g') { clear_pending(); message("Command not implemented"); return -1; }
	} else if (key == 'g') { g_pending = 1; return 0; }
	if (find_kind(key) && !resolved_find) { find_pending = key; return 0; }
	if (!resolved_find && (pending || visual_mode()) && (key=='i' || key=='a')) { object_pending=key; return 0; }
	if (mode == VM_BLOCK && (key == 'I' || key == 'A')) return begin_block_insert(bw, key == 'A');
	if (pending) {
		int op = pending;
		int had_count = count || op_count;
		off_t original = bw->cursor->byte;
		a = pdup(bw->cursor, "vim_operator_a");
		if (key == op) {
			linewise = 1;
			pline(bw->cursor, bw->cursor->line + n - 1);
		} else if (op == 'c' && key == 'w' && word_class(bw->cursor)) {
			/* cw is ce on a nonblank; its first e includes the current word. */
			int cls = word_class(bw->cursor);
			while (!piseof(bw->cursor) && word_class(bw->cursor) == cls) pgetc(bw->cursor);
			while (--n) {
				while (!piseof(bw->cursor) && !word_class(bw->cursor)) pgetc(bw->cursor);
				cls = word_class(bw->cursor);
				while (!piseof(bw->cursor) && word_class(bw->cursor) == cls) pgetc(bw->cursor);
			}
		} else {
			moved = motion(bw, key, n, had_count, &linewise, &inclusive);
			if (moved <= 0) {
				pset(bw->cursor, a);
				prm(a); clear_pending();
				if (!moved) message("Motion not implemented");
				return -1;
			}
		}
		z = pdup(bw->cursor, "vim_operator_z");
		if (a->byte > z->byte) { P *t = a; a = z; z = t; }
		if (linewise) line_range(a, z);
		else if (inclusive && !piseol(z)) pgetc(z);
		/* dw at the end of a line must not join the next line. */
		else if (key == 'w' && op != 'y' && z->line > a->line) {
			P *eol = pdup(a, "vim_dw_eol");
			P *indent = pdup(z, "vim_dw_indent");
			p_goto_eol(eol);
			first_nonblank(indent);
			if (z->line == a->line + 1 && z->byte <= indent->byte) pset(z, eol);
			prm(indent);
			prm(eol);
		}
		pset(bw->cursor, a);
		start_unit();
		rc = operate(bw, a, z, op, linewise);
		if (op == 'y' && !find_kind(key) && key != ';' && key != ',' && key != '%')
			pgoto(bw->cursor, original);
		prm(a); prm(z);
		clear_pending();
		if (mode != VM_INSERT) finish_unit();
		normal_cursor(bw); return rc;
	}
	if ((mode == VM_VISUAL || mode == VM_LINE) && (key == 'd' || key == 'c' || key == 'y' || key == 'x')) {
		linewise = mode == VM_LINE;
		a = pdup(markb, "vim_selection_a"); z = pdup(markk, "vim_selection_z");
		clear_visual(); mode = VM_NORMAL;
		start_unit();
		rc = operate(bw, a, z, key == 'x' ? 'd' : key, linewise);
		prm(a); prm(z);
		clear_pending();
		if (mode != VM_INSERT) finish_unit();
		normal_cursor(bw); return rc;
	}
	moved = motion(bw, key, n, count != 0, &linewise, &inclusive);
	if (moved < 0) { clear_pending(); return -1; }
	if (moved) {
		off_t desired = bw->cursor->xcol;
		if (mode == VM_BLOCK) {
			int vertical = key == 'j' || key == 'k' || key == VIM_UP || key == VIM_DOWN ||
			               key == 4 || key == 21 || key == VIM_PGUP || key == VIM_PGDN;
			if (piseof(bw->cursor) && pisbol(bw->cursor) && !pisbof(bw->cursor)) {
				prgetc(bw->cursor); pcol(bw->cursor, desired);
			}
			if (key == '$' || key == VIM_END) block_eol = 1;
			else if (!vertical) block_eol = 0;
			if (vertical && block_eol) {
				p_goto_eol(bw->cursor);
				if (!pisbol(bw->cursor)) prgetc(bw->cursor);
			}
		}
		clear_pending(); normal_cursor(bw);
		if (key == 'j' || key == 'k' || key == VIM_UP || key == VIM_DOWN || key == 4 || key == 21)
			bw->cursor->xcol = desired;
		if (visual_mode()) update_visual(bw);
		return 0;
	}
	if (key == 'v' || key == 'V' || key == 22) {
		int new_mode = key == 'v' ? VM_VISUAL : key == 'V' ? VM_LINE : VM_BLOCK;
		if (mode == new_mode) { clear_visual(); mode = VM_NORMAL; }
		else {
			if (!anchor) pdupown(bw->cursor, &anchor, "vim_anchor");
			block_eol = 0;
			mode = new_mode; update_visual(bw);
		}
		clear_pending(); return 0;
	}
    if (key==':' || key=='/' || key=='?') {
        vim_capture_selection();
        if (visual_mode()) clear_visual();
        command_register_pending=0;
        command_kind=key; command_len=0; command[0]=0; mode=VM_COMMAND;
        clear_pending(); command_display(); return 0;
    }
	if (visual_mode()) { clear_pending(); message("Visual command not implemented"); return -1; }
	switch (key) {
	case 'i': case 'a': case 'I': case 'A': case 'o': case 'O': enter_insert(bw, key); return mode==VM_INSERT ? 0 : -1;
	case 'd': case 'c': case 'y': pending = key; op_count = count; count = 0;
        if (selected_register!='"') { char echo[32]; sprintf(echo,"\"%c%c",selected_register,key); message(echo); }
        return 0;
	case 'x': case VIM_DEL:
		a = pdup(bw->cursor, "vim_x_a"); z = pdup(a, "vim_x_z");
		while (n-- && !piseol(z)) pgetc(z);
		start_unit(); rc = operate(bw, a, z, 'd', 0); prm(a); prm(z); finish_unit(); break;
	case 'D': case 'C':
		a = pdup(bw->cursor, "vim_D_a"); z = pdup(a, "vim_D_z");
		pline(z, z->line + n - 1); p_goto_eol(z);
		start_unit(); rc = operate(bw, a, z, key == 'D' ? 'd' : 'c', 0); prm(a); prm(z);
		if (mode != VM_INSERT) finish_unit(); break;
	case 'p': case 'P': start_unit(); rc = put_register(bw, key == 'P', n); finish_unit(); break;
	case 'u': case 18:
		start_unit();
		while (n-- && !rc) rc = key == 'u' ? uundo(bw->parent, 0) : uredo(bw->parent, 0);
		bw->cursor->xcol = piscol(bw->cursor);
		break;
	case '.': clear_pending(); rc = repeat_change(bw, n); break;
	case '*': rc = search_word(bw, n); break;
	case 'n': case 'N': rc = search(bw, key == 'N' ? !search_back : search_back, n); break;
	case VIM_UNKNOWN: break;
	default: message("Command not implemented"); rc = -1; break;
	}
	clear_pending(); normal_cursor(bw);
	return rc;
}

#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)
int vim_gui_pasting(void)
{
	return paste;
}

void vim_gui_mouse_prepare(void)
{
	finish_unit(); clear_pending(); sequence_len = 0;
	if (visual_mode()) { clear_visual(); mode = VM_NORMAL; }
	if (mode == VM_COMMAND) mode = VM_NORMAL;
	if (mode == VM_INSERT) { record_key('i'); start_unit(); }
}

/* Native menu actions use the file engine directly, never Ex keystrokes.
 * Filename bytes cannot escape into commands; Insert is retained after save. */
int vim_gui_action(int action, const char *path)
{
	BW *bw = current_bw();
	int rc = 0;
	if (!bw || paste) return -1;
	finish_unit(); clear_pending(); sequence_len = 0;
	switch (action) {
	case MIJOE_GUI_IMPORT_TEXT: rc=import_text_result(bw,path,0); break;
	case MIJOE_GUI_SHOW_BUFFERS: rc=show_buffer_list(bw,path); break;
	case MIJOE_GUI_SAVE: rc = vim_write_file(bw->parent, NULL, 0, 0); break;
	case MIJOE_GUI_SAVE_AS: rc = vim_write_file(bw->parent, path, 1, 1); break;
	case MIJOE_GUI_COPY: vim_capture_selection(); break;
	case MIJOE_GUI_CUT:
		if (mode == VM_VISUAL || mode == VM_LINE) rc = dispatch(bw, 'd');
		else {
			rc = execmd(findcmd("blkdel"), 0);
			if (!rc) { clear_visual(); mode = VM_NORMAL; }
		}
		break;
	case MIJOE_GUI_UNDO: case MIJOE_GUI_REDO:
		if (visual_mode()) { clear_visual(); mode = VM_NORMAL; }
		rc = execmd(findcmd(action == MIJOE_GUI_UNDO ? "undo" : "redo"), 0);
		bw->cursor->xcol = piscol(bw->cursor);
		break;
	case MIJOE_GUI_NEXT_BUFFER: rc = vim_ex(bw->parent, "bn"); break;
	case MIJOE_GUI_PREV_BUFFER: rc = vim_ex(bw->parent, "bp"); break;
	case MIJOE_GUI_HELP: rc = vim_ex(bw->parent, "help"); break;
	case MIJOE_GUI_SELECT_ALL:
        if (bw->b->large) { message("Lazy mode: select and copy within the loaded region; whole-file selection is unavailable."); rc=-1; break; }
		if (visual_mode()) clear_visual();
		mode = VM_NORMAL;
		dispatch(bw, 'g'); dispatch(bw, 'g'); dispatch(bw, 'V'); dispatch(bw, 'G');
		break;
	default: rc = -1;
	}
	if (mode == VM_INSERT && !leave) { record_key('i'); start_unit(); }
	return rc;
}
#endif

static void paste_character(BW *bw, int key)
{
	if (mode == VM_COMMAND) {
		/* A pasted newline must never execute an Ex command. */
		if (key >= 32 && key <= 0x10ffff) command_key(bw, key);
	} else if (mode == VM_INSERT) dispatch(bw, VIM_LITERAL | key);
}

int vim_input(va_list args)
{
	int key = va_arg(args, int);
	BW *bw = current_bw();
	unsigned long number;
	if (!bw || bw->b->pid || (!vim_enabled && !lf_active(bw->b))) return 0;
	number = vim_buffer_number(bw->b);
	if (active_number != number) {
		clear_visual(); finish_unit(); clear_pending(); mode = VM_NORMAL;
		active_number = number;
	}
    if (!paste && (key==VIM_F5 || key==VIM_F6)) {
        if (vim_grep_active()) { message("Close grep before recording or playing a macro"); return 1; }
        if (key==VIM_F5) macro_toggle(); else macro_play();
        return 1;
    }
    if (vim_grep_active() && key==VIM_PASTE) { paste=1; paste_match=0; paste_insert=0; return 1; }
	if (paste) {
		const char ending[] = "\033[201~";
		if (key == (unsigned char)ending[paste_match]) {
			if (++paste_match == (int)sizeof(ending) - 1) {
				paste = paste_match = 0;
				if (paste_insert) dispatch(bw, 27);
			}
			return 1;
		}
		/* Failed terminator prefix: every buffered character remains data. */
		{
			int i, indent = bw->o.autoindent, wrap_words = bw->o.wordwrap;
			bw->o.autoindent = bw->o.wordwrap = 0;
			for (i = 0; i < paste_match; ++i) paste_character(bw, (unsigned char)ending[i]);
			paste_match = 0;
			if (key == 27) paste_match = 1;
			else paste_character(bw, key);
			bw->o.autoindent = indent; bw->o.wordwrap = wrap_words;
		}
		return 1;
	}
	if (key == VIM_PASTE) {
		if (help_text_active(bw->t) || mode == VM_BLOCK) {
			paste = 1; paste_match = 0; paste_insert = 0;
			return 1;
		}
		paste = 1; paste_match = 0; paste_insert = mode != VM_INSERT && mode != VM_COMMAND;
		if (mode == VM_VISUAL || mode == VM_LINE) dispatch(bw, 'c');
		else if (paste_insert) { sequence_len = 0; record_key('i'); enter_insert(bw, 'i'); }
		return 1;
	}
	if (key == VIM_PASTE_END) {
		paste = 0;
		if (paste_insert) dispatch(bw, 27);
		return 1;
	}
	vim_macro_result(dispatch(bw,key));
	staupd = dostaupd = 1;
	return 1;
}

/* Decode CSI/SS3 keys without turning a lone Escape into a prefix deadlock. */
int vim_readkey(int key)
{
	char seq[40];
	int len = 0, c, prefix;
	if (paste) return key;
	if (key != 27) return key;
	if (!ttwaitkey(40)) return 27;
	c = ttgetch();
	if (c != '[' && c != 'O') {
		queued_index = 0; queued_count = 1; queued_keys[0] = c;
		return 27;
	}
	prefix = c;
	do {
		if (!ttwaitkey(100)) {
			if (prefix == 'O') { queued_index = 0; queued_count = 1; queued_keys[0] = 'O'; return 27; }
			return VIM_UNKNOWN;
		}
		c = ttgetch();
		if (c < 0 || c > 127) return VIM_UNKNOWN;
		seq[len++] = (char)c;
	} while (len < (int)sizeof(seq) - 1 && (c < 0x40 || c > 0x7e));
	seq[len] = 0;
	if (prefix == 'O' && (len != 1 || !strchr("ABCDHF", c))) {
		int i;
		queued_index = 0; queued_count = len + 1; queued_keys[0] = 'O';
		for (i = 0; i < len; ++i) queued_keys[i + 1] = (unsigned char)seq[i];
		return 27;
	}
	if (!strcmp(seq, "200~")) return VIM_PASTE;
	if (!strcmp(seq, "201~")) return VIM_PASTE_END;
	if (!strcmp(seq, "2;2~")) return VIM_CLIPBOARD;
	/* CSI-u and xterm modifyOtherKeys encodings, when enabled by the
	 * user's terminal. Legacy ASCII collapses Ctrl+U and Ctrl+Shift+U. */
	if (!strcmp(seq,"117;6u") || !strcmp(seq,"85;6u") ||
	    !strcmp(seq,"27;6;117~") || !strcmp(seq,"27;6;85~")) return VIM_PGDN;
	switch (c) {
	case 'A': return VIM_UP;
	case 'B': return VIM_DOWN;
	case 'C': return VIM_RIGHT;
	case 'D': return VIM_LEFT;
	case 'H': return VIM_HOME;
	case 'F': return VIM_END;
	case '~':
		switch (atoi(seq)) {
		case 1: case 7: return VIM_HOME;
		case 4: case 8: return VIM_END;
		case 3: return VIM_DEL;
		case 15: return VIM_F5;
        case 17: return VIM_F6;
		case 5: return VIM_PGUP;
		case 6: return VIM_PGDN;
		}
	}
	return VIM_UNKNOWN;
}
