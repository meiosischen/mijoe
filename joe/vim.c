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
static int pending, g_pending, find_pending;
static int motion_target, last_find_kind, last_find_target;
static P *anchor;
static int block_eol;
static P *block_insert_start;
static off_t block_last_line, block_insert_col;
static int block_insert_append, block_insert_eol, block_prepared;
static B *reg;
static int reg_line;
static struct charmap *reg_map;
static int *sequence, *last_change;
static ptrdiff_t sequence_len, sequence_cap, last_len;
static int recording_change, replaying;
static int paste, paste_insert, paste_match;
static int command_kind;
static char command[COMMAND_SIZE], history[COMMAND_SIZE];
static ptrdiff_t command_len;
static char *search_pattern;
static int search_back;
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
	case VM_COMMAND: return "COMMAND";
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
	pending = g_pending = find_pending = 0;
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

static void normal_cursor(BW *bw)
{
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
int vim_native_key(int key)
{
	BW *bw = current_bw();
	KBD *kbd;
	if (!bw || paste || mode == VM_COMMAND || find_pending) return 0;
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
		if (!pisbol(p)) prgetc(p);
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
		pset(block_insert_start, bw->cursor);
		block_prepared = 1;
	}
}

static void yank_range(P *a, P *z, int linewise)
{
	if (reg) brm(reg);
	reg = bcpy(a, z);
	reg_map = a->b->o.charmap;
	reg_line = linewise;
	/* An unterminated last line is still a complete line in a line register. */
	if (linewise && reg->eof->byte) {
		P *p = pdup(reg->eof, "vim_yank_tail");
		if (prgetc(p) != '\n') binsm(reg->eof, "\n", 1);
		prm(p);
	}
}

static int operate(BW *bw, P *a, P *z, int op, int linewise)
{
	P *d;
	int tail, ending = 0;
	if (a->byte == z->byte && op != 'c') return 0;
	if (op != 'y' && !writable(bw)) return -1;
	yank_range(a, z, linewise);
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
	return 0;
}

static int put_register(BW *bw, int before, long n)
{
	P *start;
	char *converted = NULL;
	int add_newline = 0;
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
	else if (!pisbof(bw->cursor)) prgetc(bw->cursor);
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
	case 'a': if (!piseol(bw->cursor)) pgetc(bw->cursor); break;
	case 'I': first_nonblank(bw->cursor); break;
	case 'A': p_goto_eol(bw->cursor); break;
	case 'o':
		p_goto_eol(bw->cursor);
		if (!execmd(findcmd("rtn"), 13)) recording_change = 1;
		break;
	case 'O':
		p_goto_bol(bw->cursor);
		binsc(bw->cursor, '\n');
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
	pattern = my_iconv(NULL, bw->b->o.charmap, search_pattern, locale_map);
	compiled = joe_regcomp(bw->b->o.charmap, pattern, zlen(pattern), 0, 1, 0);
	obj_free(pattern);
	if (compiled->err) { message(compiled->err); joe_regfree(compiled); return -1; }
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
	if (found) pset(bw->cursor, p);
	else message("Pattern not found");
	prm(p);
	joe_regfree(compiled);
	return found ? 0 : -1;
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
	if (choose(bw->parent, bw->parent, names, &selected) != 0) return -1;
	return set_color_scheme(bw->parent, names[selected]);
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
	if (!zcmp(cmd, "help") || !zcmp(cmd, "h")) {
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
		B *b;
		char *list = vsdupz("");
		char number[40];
		if (*arg || bang) { obj_free(list); message("Trailing characters"); rc = -1; goto done; }
		for (b = bufs.link.next; b != &bufs; b = b->link.next) if (!b->internal && !b->scratch) {
			joe_snprintf_1(number, SIZEOF(number), "%lu", vim_buffer_number(b));
			list = vsncpy(sv(list), sz(number));
			list = vsncpy(sv(list), sz(b == bw->b ? "%" : " "));
			list = vsncpy(sv(list), sz(b->changed ? "+ " : "  "));
			list = vsncpy(sv(list), sz(b->name ? b->name : "[No Name]"));
			list = vsncpy(sv(list), sz("; "));
		}
		message(list);
		obj_free(list);
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
	message(text);
	obj_free(text);
}

static int command_key(BW *bw, int key)
{
	if (key == 27 || key == 3) { mode = VM_NORMAL; clear_pending(); return 0; }
	if (key == 13 || key == 10) {
		mode = VM_NORMAL;
		strcpy(history, command);
		if (command_kind == ':') return vim_ex(bw->parent, command);
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

static int dispatch(BW *bw, int key)
{
	long n;
	int linewise = 0, inclusive = 0, rc = 0, resolved_find = 0, moved;
	P *a, *z;
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
	if (!replaying && mode == VM_NORMAL && !pending && !g_pending && !find_pending && !count) sequence_len = 0;
	record_key(key);
	if (key == 27 || key == 3) {
		if (mode == VM_INSERT) {
			finish_unit();
			if (!pisbol(bw->cursor)) prgetc(bw->cursor);
		}
		if (visual_mode()) clear_visual();
		mode = VM_NORMAL;
		clear_pending(); normal_cursor(bw);
		return 0;
	}
	if (mode == VM_INSERT) {
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
	if (visual_mode()) { clear_pending(); message("Visual command not implemented"); return -1; }
	switch (key) {
	case 'i': case 'a': case 'I': case 'A': case 'o': case 'O': enter_insert(bw, key); return 0;
	case 'd': case 'c': case 'y': pending = key; op_count = count; count = 0; return 0;
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
		break;
	case '.': clear_pending(); rc = repeat_change(bw, n); break;
	case ':': case '/': case '?':
		command_kind = key; command_len = 0; command[0] = 0; mode = VM_COMMAND;
		command_display(); break;
	case 'n': case 'N': rc = search(bw, key == 'N' ? !search_back : search_back, n); break;
	case VIM_UNKNOWN: break;
	default: message("Command not implemented"); rc = -1; break;
	}
	clear_pending(); normal_cursor(bw);
	return rc;
}

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
	if (!bw || bw->b->pid || !vim_enabled) return 0;
	number = vim_buffer_number(bw->b);
	if (active_number != number) {
		clear_visual(); finish_unit(); clear_pending(); mode = VM_NORMAL;
		active_number = number;
	}
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
	dispatch(bw, key);
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
		case 5: return VIM_PGUP;
		case 6: return VIM_PGDN;
		}
	}
	return VIM_UNKNOWN;
}
