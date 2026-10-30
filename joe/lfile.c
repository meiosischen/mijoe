/* Read-only large-file viewer. The gap buffer is a bounded viewing window,
 * never a complete editable representation of the source. */
#include "types.h"
#include "vim.h"
#include <ctype.h>

#define LF_WINDOW (1024L * 1024L)
#define LF_MARGIN (64L * 1024L)
#define LF_IO 65536
int lf_threshold_mb = 512;
int lf_force = 0;

struct large_file {
    int fd, pending, encoding, crlf, original_ro, options_seen, failed;
    int known_line, partial_start, view_hex;
    JOE_LONG_LONG size, start, end, line_base, stamp, reads;
    unsigned long previous;
    off_t previous_byte, previous_top;
    unsigned int *raw_map;
    ptrdiff_t map_len;
};
struct saved_point { P *point; JOE_LONG_LONG absolute; off_t xcol; struct saved_point *next; };

static int lf_stamp(int fd, JOE_LONG_LONG *size, JOE_LONG_LONG *stamp)
{
#ifdef JOEWIN
    FILETIME time;
    *size = _filelengthi64(fd);
    if (*size < 0 || !GetFileTime((HANDLE)_get_osfhandle(fd), NULL, NULL, &time)) return -1;
    *stamp = ((JOE_LONG_LONG)time.dwHighDateTime << 32) | time.dwLowDateTime;
#else
    struct stat st;
    if (fstat(fd, &st)) return -1;
    *size = st.st_size;
#ifdef __APPLE__
    *stamp = (JOE_LONG_LONG)st.st_mtime * 1000000000 + st.st_mtimespec.tv_nsec;
#else
    *stamp = st.st_mtime;
#endif
#endif
    return 0;
}
static ptrdiff_t lf_read(struct large_file *f, JOE_LONG_LONG offset, char *data, ptrdiff_t length)
{
    ptrdiff_t done = 0, amount;
#ifdef JOEWIN
    if (_lseeki64(f->fd, offset, SEEK_SET) < 0) return -1;
#else
    if (lseek(f->fd, (off_t)offset, SEEK_SET) < 0) return -1;
#endif
    while (done < length) {
        amount = joe_read(f->fd, data + done, length - done);
        if (amount < 0) return -1;
        if (!amount) break;
        done += amount;
    }
    f->reads += done;
    return done;
}
static int lf_valid(struct large_file *f)
{
    JOE_LONG_LONG size, stamp;
    if (f->failed || lf_stamp(f->fd, &size, &stamp) || size != f->size || stamp != f->stamp) {
        f->failed = 1;
        return 0;
    }
    return 1;
}
static int lf_cancelled(void)
{
    if (ttcheck()) {
        int key = ttgetch();
        if (key == 3 || key == 27) return 1;
        nungetc(key);
    }
    return 0;
}
/* Recognise text encodings before testing NUL/control bytes. */
static int lf_kind(const unsigned char *s, ptrdiff_t length, int *encoding, int *crlf)
{
    ptrdiff_t i, controls = 0;
    *encoding = 0; *crlf = 0;
    if ((length >= 4 && (!memcmp(s,"\177ELF",4) || !memcmp(s,"PK\003\004",4) || !memcmp(s,"%PDF",4))) ||
        (length >= 2 && s[0]=='M' && s[1]=='Z') ||
        (length >= 8 && !memcmp(s,"\211PNG\r\n\032\n",8))) return 1;
    if (length >= 2 && s[0] == 0xff && s[1] == 0xfe) { *encoding = 1; return 0; }
    if (length >= 2 && s[0] == 0xfe && s[1] == 0xff) { *encoding = 2; return 0; }
    /* BOM-less UTF-16: ASCII in one byte lane, zero in the other. */
    if (length >= 16) {
        ptrdiff_t even = 0, odd = 0, ascii = 0;
        for (i = 0; i + 1 < length; i += 2) {
            if (!s[i]) ++even;
            if (!s[i+1]) ++odd;
            if ((s[i] >= 32 && s[i] < 127) || (s[i+1] >= 32 && s[i+1] < 127)) ++ascii;
        }
        if (ascii > length/8 && odd > length/4 && even < length/32) { *encoding = 1; return 0; }
        if (ascii > length/8 && even > length/4 && odd < length/32) { *encoding = 2; return 0; }
    }
    for (i = 0; i < length; ++i) {
        if (s[i] == '\r' && i+1 < length && s[i+1] == '\n') *crlf = 1;
        if (!s[i]) return 1;
        if (s[i] < 32 && s[i] != '\n' && s[i] != '\r' && s[i] != '\t' && s[i] != '\f' && s[i] != 27) ++controls;
    }
    return length && controls > length/100;
}
B *lf_probe(int fd, const char *name, int *binary)
{
    struct large_file sample, *f;
    unsigned char bytes[LF_IO];
    ptrdiff_t count;
    B *b;
    char prompt[512];
    memset(&sample, 0, sizeof(sample)); sample.fd = fd;
    *binary = 0;
    if (lf_stamp(fd, &sample.size, &sample.stamp)) return NULL;
    count = lf_read(&sample, 0, (char *)bytes, sample.size < LF_IO ? (ptrdiff_t)sample.size : LF_IO);
    if (count < 0) return NULL;
    *binary = lf_kind(bytes, count, &sample.encoding, &sample.crlf);
    if (sample.encoding) {
        ptrdiff_t i;
        for (i = 0; i+3 < count; i += 2)
            if ((sample.encoding==1 && bytes[i]==13 && !bytes[i+1] && bytes[i+2]==10 && !bytes[i+3]) ||
                (sample.encoding==2 && !bytes[i] && bytes[i+1]==13 && !bytes[i+2] && bytes[i+3]==10)) sample.crlf=1;
    }
    /* Return the original descriptor to the normal loader's beginning. */
#ifdef JOEWIN
    _lseeki64(fd, 0, SEEK_SET);
#else
    lseek(fd, 0, SEEK_SET);
#endif
    if (!lf_force && sample.size <= (JOE_LONG_LONG)lf_threshold_mb * 1024 * 1024 && sample.size <= (JOE_LONG_LONG)MAXOFF) return NULL;
    f = (struct large_file *)joe_calloc(1, sizeof(*f));
    *f = sample; f->fd = dup(fd); f->pending = 1; f->known_line = 1;
    if (f->fd < 0) f->failed = 1;
    if (maint && maint->curwin && (maint->curwin->watom->what & TYPETW)) {
        BW *previous = (BW *)maint->curwin->object;
        f->previous = vim_buffer_number(previous->b);
        f->previous_byte = previous->cursor->byte; f->previous_top = previous->top->byte;
    }
    b = bmk(NULL);
    sprintf(prompt, "Large %s file: %" JOE_INT64_FORMAT "d bytes\n\n[L / Enter] Read-only lazy open (recommended)\n[N] Normal full-file open\n[Esc / C] Cancel\n\nLazy mode reads bounded windows; editing and saving are disabled.\nLiteral search and line-number jumps scan on demand (Esc cancels).\n",
            *binary ? "binary" : "text", f->size);
    binsm(b->bof, prompt, zlen(prompt));
    b->changed = 0; b->binary_file = *binary;
    b->large = f; b->er = berror = 0;
    (void)name;
    return b;
}
void lf_release(B *b)
{
    struct large_file *f = b->large;
    if (!f) return;
    close(f->fd);
    if (f->raw_map) joe_free(f->raw_map);
    joe_free(f); b->large = NULL;
}
int lf_pending(B *b) { return b->large && b->large->pending; }
int lf_active(B *b) { return b->large && !b->large->pending; }
JOE_LONG_LONG lf_size(B *b) { return b->large ? b->large->size : b->eof->byte; }
JOE_LONG_LONG lf_read_bytes(B *b) { return b->large ? b->large->reads : 0; }
JOE_LONG_LONG lf_absolute(B *b, off_t byte)
{
    struct large_file *f = b->large;
    if (!f) return byte;
    if (f->raw_map) {
        if (byte < 0) byte = 0;
        if (byte > f->map_len) byte = f->map_len;
        return f->start + f->raw_map[(ptrdiff_t)byte];
    }
    return f->start + byte;
}
static off_t lf_relative(struct large_file *f, JOE_LONG_LONG absolute, off_t length)
{
    JOE_LONG_LONG raw = absolute - f->start;
    ptrdiff_t low = 0, high = f->map_len;
    if (raw <= 0) return 0;
    if (!f->raw_map) return raw > length ? length : (off_t)raw;
    while (low < high) {
        ptrdiff_t mid = low + (high-low)/2;
        if (f->raw_map[mid] < raw) low = mid+1; else high = mid;
    }
    return (off_t)low;
}
void lf_options(B *b, OPTIONS *o)
{
    struct large_file *f = b->large;
    if (f) {
        if (!f->options_seen) { f->original_ro = b->rdonly || o->readonly; f->options_seen = 1; }
        if (f->pending && (o->hex || b->binary_file)) f->view_hex=1;
        b->rdonly = o->readonly = 1;
        o->highlight = o->title = o->wordwrap = o->picture = o->ansi = o->linums = 0;
        o->mfirst = NULL;
        o->hex = !f->pending && f->view_hex;
        o->crlf = !f->view_hex && f->crlf;
        if (f->encoding && !f->view_hex) o->charmap = find_charmap("utf-8");
    } else if (b->binary_file) o->hex = 1;
    if (o->hex) { o->charmap = find_charmap("c"); o->crlf = 0; o->overtype = 1; }
}
void lf_startup_option(B *b, const char *option)
{
    if (!b->large || !b->large->pending) return;
    if (!zcmp(option,"rdonly")) b->large->original_ro=1;
    else if (!zcmp(option,"-rdonly")) b->large->original_ro=0;
}
static void lf_window_options(B *b)
{
    W *w;
    lf_options(b, &b->o);
    if (!maint || !maint->topwin) return;
    w = maint->topwin;
    do {
        if ((w->watom->what & TYPETW) && w->object) {
            BW *bw = (BW *)w->object;
            if (bw->b == b) { bw->o = b->o; bw->offset = 0; bw->top_changed = 1; }
        }
        w = w->link.next;
    } while (w != maint->topwin);
}
/* Decode a UTF-16 window, retaining a bounded byte-to-source map. */
static char *lf_decode(struct large_file *f, const unsigned char *raw, ptrdiff_t length, ptrdiff_t *outlen, unsigned int **map)
{
    ptrdiff_t i = 0, used = 0;
    char *out = (char *)joe_malloc(length*2 + 8);
    unsigned int *positions = (unsigned int *)joe_malloc((length*2+9)*sizeof(unsigned int));
    while (i+1 < length) {
        ptrdiff_t begin = i, n, j;
        unsigned int c = f->encoding == 1 ? raw[i] | (raw[i+1]<<8) : (raw[i]<<8) | raw[i+1];
        i += 2;
        if (c >= 0xd800 && c <= 0xdbff && i+1 < length) {
            unsigned int d = f->encoding == 1 ? raw[i] | (raw[i+1]<<8) : (raw[i]<<8) | raw[i+1];
            if (d >= 0xdc00 && d <= 0xdfff) { c = 0x10000 + ((c-0xd800)<<10) + d-0xdc00; i += 2; }
            else c = 0xfffd;
        } else if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
        if (!f->start && !begin && c == 0xfeff) continue;
        n = utf8_encode(out + used, (int)c);
        for (j = 0; j < n; ++j) positions[used+j] = (unsigned int)begin;
        used += n;
    }
    positions[used] = (unsigned int)i; out[used] = 0;
    *map = positions; *outlen = used;
    return out;
}
static int lf_load(B *b, JOE_LONG_LONG target, JOE_LONG_LONG line_hint)
{
    struct large_file *f = b->large;
    struct saved_point *points = NULL, *entry;
    unsigned char *raw;
    char *decoded;
    unsigned int *map = NULL;
    ptrdiff_t length, count, skip = 0;
    JOE_LONG_LONG start, old_start = f->start, known_base = 0;
    int known = 0;
    B *n;
    P *p;
    if (!lf_valid(f)) return -1;
    if (target < 0) target = 0;
    if (target > f->size) target = f->size;
    start = target > LF_WINDOW/2 ? target - LF_WINDOW/2 : 0;
    if (start + LF_WINDOW > f->size) start = f->size > LF_WINDOW ? f->size-LF_WINDOW : 0;
    if (f->view_hex) start -= start%16;
    else if (f->encoding) start -= start%2;
    raw = (unsigned char *)joe_malloc(LF_WINDOW+32);
    length = f->size-start < LF_WINDOW+16 ? (ptrdiff_t)(f->size-start) : LF_WINDOW;
    count = lf_read(f, start, (char *)raw, length);
    if (count != length || !lf_valid(f)) { joe_free(raw); return -1; }
    if (start && !f->view_hex) {
        ptrdiff_t limit = count < LF_MARGIN ? count : LF_MARGIN;
        if (f->encoding) {
            for (skip = 0; skip+1 < limit; skip += 2)
                if ((f->encoding == 1 && raw[skip] == 10 && !raw[skip+1]) ||
                    (f->encoding == 2 && !raw[skip] && raw[skip+1] == 10)) { skip += 2; break; }
        } else {
            for (skip = 0; skip < limit && raw[skip] != '\n'; ++skip) ;
            if (skip < limit) ++skip;
        }
        if (skip >= limit || start+skip > target) skip = 0;
        if (!f->encoding) while (skip < count && (raw[skip]&0xc0) == 0x80) ++skip;
        if (skip) { memmove(raw, raw+skip, count-skip); start += skip; count -= skip; }
    }
    if (!f->view_hex && f->encoding && count >= 2) {
        unsigned int first = f->encoding==1 ? raw[0] | (raw[1]<<8) : (raw[0]<<8) | raw[1];
        unsigned int last = f->encoding==1 ? raw[count-2] | (raw[count-1]<<8) : (raw[count-2]<<8) | raw[count-1];
        if (start+count < f->size && last >= 0xd800 && last <= 0xdbff) count -= 2;
        if (start && first >= 0xdc00 && first <= 0xdfff) { memmove(raw,raw+2,count-2); start+=2; count-=2; }
    }
    if (!f->view_hex && !f->encoding && start+count < f->size) {
        /* Do not leave a partial UTF-8 sequence at the end of a window. */
        ptrdiff_t tail = count-1;
        int needed;
        while (tail > 0 && (raw[tail]&0xc0) == 0x80) --tail;
        needed = raw[tail] >= 0xf0 ? 4 : raw[tail] >= 0xe0 ? 3 : raw[tail] >= 0xc0 ? 2 : 1;
        if (count-tail < needed) count = tail;
    }
    if (!start) { known = 1; known_base = 0; }
    else if (!f->pending && f->known_line && start >= old_start && start <= f->end) {
        P *q = pdup(b->bof, "lazy_line_base");
        pgoto(q, lf_relative(f, start, b->eof->byte));
        known = 1; known_base = f->line_base + q->line; prm(q);
    }
    if (markb && markb->b == b) { vim_cancel_selection(); if (maint && maint->curwin) msgnw(maint->curwin,"Selection cleared when loading another file region."); }
    for (p = b->bof->link.next; p != b->bof; p = p->link.next) if (p != b->eof) {
        entry = (struct saved_point *)joe_malloc(sizeof(*entry));
        entry->point = p; entry->absolute = f->pending ? 0 : lf_absolute(b,p->byte);
        entry->xcol = p->xcol; entry->next = points; points = entry;
    }
    n = bmk(NULL);
    f->start = start;
    if (f->encoding && !f->view_hex) {
        decoded = lf_decode(f, raw, count, &length, &map);
        binsm(n->bof, decoded, length); joe_free(decoded);
    } else { length = count; binsm(n->bof, (char *)raw, count); }
    joe_free(raw);
    if (f->raw_map) joe_free(f->raw_map);
    f->raw_map = map; f->map_len = length;
    f->partial_start = start && !skip;
    f->end = start+count; f->pending = 0; f->known_line = known; f->line_base = known_base;
    n->name = zdup(b->name); n->o = b->o; n->rdonly = 1;
    n->binary_file = b->binary_file; n->mod_time = b->mod_time;
    b->large = NULL; n->large = f;
    breplace(b,n);
    while (points) {
        entry = points; points = points->next;
        pgoto(entry->point, lf_relative(f, entry->absolute, b->eof->byte));
        entry->point->xcol = entry->xcol; joe_free(entry);
    }
    lf_window_options(b);
    if (line_hint >= 0) {
        P *q = pdup(b->bof,"lazy_line_hint");
        pgoto(q,lf_relative(f,target,b->eof->byte));
        f->known_line = 1; f->line_base = line_hint-q->line; prm(q);
    }
    return 0;
}
static int lf_seek(BW *bw, JOE_LONG_LONG target, JOE_LONG_LONG line_hint)
{
    struct large_file *f = bw->b->large;
    if (!lf_valid(f)) { msgnw(bw->parent,"File changed on disk; close and reopen it to refresh."); return -1; }
    if (target < 0) target = 0;
    if (target >= f->size && f->size) target = f->size-1;
    if (target < f->start || target >= f->end) {
        if (lf_load(bw->b,target,line_hint)) { msgnw(bw->parent,"Could not read lazy file window; source may have changed."); return -1; }
    }
    pgoto(bw->cursor,lf_relative(f,target,bw->b->eof->byte));
    if (line_hint >= 0) { f->known_line=1; f->line_base=line_hint-bw->cursor->line; }
    bw->cursor->xcol = piscol(bw->cursor);
    nredraw(bw->t->t); staupd = dostaupd = 1;
    return 0;
}
int lf_choose(BW *bw, int choice)
{
    B *b = bw->b, *n, *previous;
    struct large_file *f = b->large;
    if (!f || !f->pending) return -1;
    if (choice == 2) {
        unsigned long number = f->previous;
        off_t byte = f->previous_byte, top = f->previous_top;
        previous = NULL;
        for (n = bufs.link.next; n != &bufs; n = n->link.next)
            if (n != b && n->vim_number == number && number) { previous = n; ++previous->count; break; }
        if (!previous) previous = bfind("");
        ++b->count; /* Prevent the cancelled placeholder from being orphaned. */
        get_buffer_in_window(bw,previous);
        bw = (BW *)maint->curwin->object;
        pgoto(bw->cursor,byte > previous->eof->byte ? previous->eof->byte : byte);
        pgoto(bw->top,top > previous->eof->byte ? previous->eof->byte : top);
        brm(previous); brm(b);
        nredraw(maint->t); return 0;
    }
    if (choice == 1) {
        int old_guess = guess_utf16, error;
        if (f->size > (JOE_LONG_LONG)MAXOFF) { msgnw(bw->parent,"This build cannot fully load that size; choose read-only lazy mode."); return -1; }
        if (!lf_valid(f)) return -1;
#ifdef JOEWIN
        _lseeki64(f->fd,0,SEEK_SET);
#else
        lseek(f->fd,0,SEEK_SET);
#endif
        if (b->binary_file) guess_utf16 = 0;
        n = bread(f->fd,(off_t)f->size); error = berror; guess_utf16 = old_guess;
        if (error) { brm(n); return -1; }
        n->name = zdup(b->name); n->binary_file = b->binary_file;
        setopt(n,b->name); n->o.readonly = n->rdonly = f->original_ro; n->o.hex=f->view_hex;
        n->mod_time = b->mod_time;
        b->o = n->o;
        breplace(b,n); lf_window_options(b);
    } else if (lf_load(b,0,0)) { msgnw(bw->parent,"Could not open lazy source."); return -1; }
    p_goto_bof(bw->cursor); p_goto_bof(bw->top);
    nredraw(bw->t->t); staupd = dostaupd = 1;
    return 0;
}
int lf_prompt_key(BW *bw, int key)
{
    if (!lf_pending(bw->b)) return 0;
    if (key == 'l' || key == 'L' || key == 13 || key == 10) lf_choose(bw,0);
    else if (key == 'n' || key == 'N') lf_choose(bw,1);
    else if (key == 27 || key == 3 || key == 'c' || key == 'C' || key == 'q') lf_choose(bw,2);
    else msgnw(bw->parent,"Large file: L/Enter lazy read-only, N normal, Esc cancel");
    return 1;
}
void lf_follow(BW *bw)
{
    struct large_file *f = bw->b->large;
    JOE_LONG_LONG target;
    off_t margin;
    if (f && f->pending && lf_force) { lf_choose(bw,0); f = bw->b->large; }
    if (!f || f->pending || f->failed) return;
    lf_options(bw->b,&bw->o);
    margin = bw->b->eof->byte > LF_MARGIN*2 ? LF_MARGIN : 0;
    if ((f->start && bw->cursor->byte < margin) ||
        (f->end < f->size && bw->cursor->byte >= bw->b->eof->byte-margin)) {
        target = lf_absolute(bw->b,bw->cursor->byte);
        if (lf_load(bw->b,target,-1)) { f->failed = 1; msgnw(bw->parent,"Source changed or read failed; close and reopen file."); }
    }
}
/* Find a raw line separator without creating a file-sized line index. */
static JOE_LONG_LONG lf_newline(struct large_file *f, JOE_LONG_LONG position, int backwards)
{
    unsigned char bytes[LF_IO];
    ptrdiff_t amount, i, unit = f->encoding ? 2 : 1;
    while (position >= 0 && position < f->size) {
        JOE_LONG_LONG start = backwards ? (position >= LF_IO-unit ? position-LF_IO+unit : 0) : position;
        ptrdiff_t want = backwards ? (ptrdiff_t)(position-start)+unit : (f->size-start < LF_IO ? (ptrdiff_t)(f->size-start) : LF_IO);
        if (f->encoding) { start -= start%2; want -= want%2; }
        amount = lf_read(f,start,(char *)bytes,want);
        if (amount != want || lf_cancelled()) return -2;
        for (i = backwards ? amount-unit : 0; i >= 0 && i+unit <= amount; i += backwards ? -unit : unit) {
            int newline = !f->encoding ? bytes[i] == 10 :
                (f->encoding == 1 ? bytes[i] == 10 && !bytes[i+1] : !bytes[i] && bytes[i+1] == 10);
            if (newline) return start+i;
        }
        position = backwards ? start-unit : start+amount;
    }
    return -1;
}
/* Count many line moves in one stream rather than rereading a chunk per line. */
static JOE_LONG_LONG lf_step_lines(struct large_file *f, JOE_LONG_LONG position, long lines, int backwards, JOE_LONG_LONG *moved)
{
    unsigned char bytes[LF_IO];
    int unit=f->encoding ? 2 : 1;
    JOE_LONG_LONG target=position, scan=backwards ? position-2*unit : position;
    *moved=0;
    while (lines>0 && scan>=0 && scan<f->size) {
        JOE_LONG_LONG start=backwards ? (scan>=LF_IO-unit ? scan-LF_IO+unit : 0) : scan;
        ptrdiff_t i, want=backwards ? (ptrdiff_t)(scan-start)+unit : (f->size-start<LF_IO ? (ptrdiff_t)(f->size-start) : LF_IO);
        ptrdiff_t amount=lf_read(f,start,(char *)bytes,want);
        if (amount!=want || lf_cancelled()) return -2;
        for (i=backwards ? amount-unit : 0; i>=0 && i+unit<=amount; i+=backwards ? -unit : unit) {
            int newline=!f->encoding ? bytes[i]==10 : f->encoding==1 ? bytes[i]==10 && !bytes[i+1] : !bytes[i] && bytes[i+1]==10;
            if (newline && start+i+unit<f->size) {
                target=start+i+unit; ++*moved;
                if (!--lines) return target;
            }
        }
        scan=backwards ? start-unit : start+amount;
    }
    if (backwards && lines>0 && target>0) { target=0; ++*moved; }
    return target;
}
int lf_line_jump(BW *bw, JOE_LONG_LONG line)
{
    struct large_file *f = bw->b->large;
    JOE_LONG_LONG position = 0, row = 0, target = 0;
    unsigned char bytes[LF_IO];
    int unit = f->encoding ? 2 : 1;
    if (bw->o.hex) return lf_seek(bw,line > f->size/16 ? f->size-1 : line*16,-1);
    while (row < line && position < f->size) {
        ptrdiff_t i, want = f->size-position < LF_IO ? (ptrdiff_t)(f->size-position) : LF_IO;
        ptrdiff_t amount = lf_read(f,position,(char *)bytes,want);
        if (amount != want || lf_cancelled()) { msgnw(bw->parent,"Line scan cancelled or read failed."); return -1; }
        for (i = 0; i+unit <= amount; i += unit) {
            int newline = !f->encoding ? bytes[i]==10 : f->encoding==1 ? bytes[i]==10 && !bytes[i+1] : !bytes[i] && bytes[i+1]==10;
            if (newline && position+i+unit < f->size) { target = position+i+unit; ++row; if (row==line) break; }
        }
        position += amount;
    }
    return lf_seek(bw,target,row);
}

int lf_motion(BW *bw, int key, long n, int counted)
{
    struct large_file *f = bw->b->large;
    JOE_LONG_LONG position, found, moved, hint = -1;
    long lines;
    int unit;
    if (!f || f->pending) return 0;
    position = lf_absolute(bw->b,bw->cursor->byte); unit = !bw->o.hex && f->encoding ? 2 : 1;
    if (key == 'g') return lf_line_jump(bw,counted ? n-1 : 0) ? -1 : 1;
    if (key == 'G') {
        if (counted) return lf_line_jump(bw,n-1) ? -1 : 1;
        if (lf_seek(bw,f->size ? f->size-unit : 0,-1)) return -1;
        if (!bw->o.hex) p_goto_bol(bw->cursor);
        return 1;
    }
    if (key == '%' && counted) {
        if (n > 100) return -1;
        position = (f->size/100)*n + ((f->size%100)*n)/100;
        if (lf_seek(bw,position,-1)) return -1;
        if (!bw->o.hex) p_goto_bol(bw->cursor);
        return 1;
    }
    lines = n;
    if (key == VIM_PGDN || key == VIM_PGUP || key == 4 || key == 21)
        lines *= (key == 4 || key == 21) ? (bw->h > 1 ? bw->h/2 : 1) : bw->h;
    if (bw->o.hex) {
        switch (key) {
        case 'h': case VIM_LEFT: case VIM_BACK_CHAR: position -= n; break;
        case 'l': case VIM_RIGHT: case VIM_FORWARD_CHAR: position += n; break;
        case 'j': case VIM_DOWN: case VIM_PGDN: case 4: position += (JOE_LONG_LONG)lines*16; break;
        case 'k': case VIM_UP: case VIM_PGUP: case 21: position -= (JOE_LONG_LONG)lines*16; break;
        case '0': case VIM_HOME: position -= position%16; break;
        case '$': case VIM_END: position = position-position%16+15; break;
        default: return 0;
        }
        return lf_seek(bw,position,-1) ? -1 : 1;
    }
    if (key == 'j' || key == VIM_DOWN || key == VIM_PGDN || key == 4) {
        if (bw->cursor->line+lines < bw->b->eof->line) return 0;
        if (f->known_line) hint = f->line_base+bw->cursor->line;
        found=lf_step_lines(f,position,lines,0,&moved);
        if (found==-2) return -1;
        if (hint>=0) hint+=moved;
        position=found;
        return lf_seek(bw,position,hint) ? -1 : 1;
    }
    if (key == 'k' || key == VIM_UP || key == VIM_PGUP || key == 21) {
        P *q;
        if (bw->cursor->line >= lines) return 0;
        q = pdup(bw->cursor,"lazy_previous_line"); p_goto_bol(q);
        position = lf_absolute(bw->b,q->byte); prm(q);
        if (f->known_line) hint = f->line_base+bw->cursor->line;
        found=lf_step_lines(f,position,lines,1,&moved);
        if (found==-2) return -1;
        if (hint>=0) hint-=moved;
        position=found;
        return lf_seek(bw,position,hint) ? -1 : 1;
    }
    if ((key == '0' || key == VIM_HOME || key == '^') && bw->cursor->line == 0 && f->start) {
        found = lf_newline(f,position-unit,1);
        if (found == -2) return -1;
        return lf_seek(bw,found < 0 ? 0 : found+unit,-1) ? -1 : 1;
    }
    if ((key == '$' || key == VIM_END) && bw->cursor->line == bw->b->eof->line && f->end < f->size) {
        found = lf_newline(f,position,0);
        if (found == -2) return -1;
        return lf_seek(bw,found < 0 ? f->size-unit : found-unit,-1) ? -1 : 1;
    }
    return 0;
}
const char *lf_status(BW *bw)
{
    static char text[192];
    struct large_file *f = bw->b->large;
    if (!f) return bw->b->binary_file ? " HEX " : NULL;
    if (f->pending) sprintf(text," OPEN? %" JOE_INT64_FORMAT "d bytes ",f->size);
    else sprintf(text," RO-LAZY %d%% B %" JOE_INT64_FORMAT "d/%" JOE_INT64_FORMAT "d ",
                 f->size ? (int)(100.0*(double)lf_absolute(bw->b,bw->cursor->byte)/(double)f->size) : 0,
                 lf_absolute(bw->b,bw->cursor->byte),f->size);
    return text;
}
int lf_row_label(BW *bw, char *text, int size)
{
    struct large_file *f = bw->b->large;
    if (!f || f->pending) return 0;
    if (bw->o.hex) joe_snprintf_1(text,size,"%" JOE_INT64_FORMAT "d",lf_absolute(bw->b,bw->cursor->byte)/16+1);
    else if (f->known_line) joe_snprintf_1(text,size,"%" JOE_INT64_FORMAT "d",f->line_base+bw->cursor->line+1);
    else joe_snprintf_0(text,size,"?");
    return 1;
}
static int lf_word_at(BW *bw, JOE_LONG_LONG offset, int before)
{
    struct large_file *f = bw->b->large;
    unsigned char data[8];
    ptrdiff_t amount, left;
    const char *at;
    int c;
    JOE_LONG_LONG start = before ? offset-(f->encoding ? 4 : 4) : offset;
    if (start < 0) start=0;
    if (offset <= 0 && before) return 0;
    amount = (ptrdiff_t)((before ? offset : (offset+4 < f->size ? offset+4 : f->size))-start);
    if (amount <= 0 || lf_read(f,start,(char *)data,amount) != amount) return 0;
    if (f->encoding) {
        ptrdiff_t i = before ? amount-2 : 0;
        c=f->encoding==1 ? data[i] | (data[i+1]<<8) : (data[i]<<8) | data[i+1];
        if (before && c>=0xdc00 && c<=0xdfff && i>=2) {
            int high=f->encoding==1 ? data[i-2] | (data[i-1]<<8) : (data[i-2]<<8) | data[i-1];
            if (high>=0xd800 && high<=0xdbff) c=0x10000+((high-0xd800)<<10)+c-0xdc00;
        }
    } else if (bw->o.charmap->type) {
        ptrdiff_t i=before ? amount-1 : 0;
        if (before) while (i>0 && (data[i]&0xc0)==0x80) --i;
        at=(const char *)data+i; left=amount-i; c=utf8_decode_fwrd(&at,&left);
    } else c=data[before ? amount-1 : 0];
    return c>=0 && (joe_isalnum_(bw->o.charmap,c) || c=='_');
}
int lf_column_label(BW *bw, char *text, int size)
{
    struct large_file *f=bw->b->large;
    if (!f || f->pending) return 0;
    if (bw->o.hex) joe_snprintf_1(text,size,"%d",(int)(lf_absolute(bw->b,bw->cursor->byte)%16)+1);
    else if (f->partial_start && bw->cursor->line==0) joe_snprintf_0(text,size,"?");
    else return 0;
    return 1;
}
/* Streaming literal search. Deliberately rejects unbounded regex constructs. */
int lf_search(BW *bw, const char *pattern, int backwards, long n)
{
    struct large_file *f = bw->b->large;
    unsigned char *needle;
    ptrdiff_t length = 0, i, *failure, matched;
    unsigned char bytes[LF_IO];
    JOE_LONG_LONG original = lf_absolute(bw->b,bw->cursor->byte), position, found = -1;
    int word = 0, pass, cancelled = 0;
    if (!lf_valid(f)) { msgnw(bw->parent,"Source changed on disk; reopen before searching."); return -1; }
    needle = (unsigned char *)joe_malloc(zlen(pattern)*4+8);
    for (i = 0; pattern[i]; ++i) {
        unsigned char c = (unsigned char)pattern[i];
        if (c == '\\') {
            c = (unsigned char)pattern[++i];
            if (c == '<' || c == '>') { word = 1; continue; }
            if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t';
            else if (!c || !strchr("\\.^$[]*+?(){}|/",c)) { cancelled = 1; break; }
        } else if (strchr(".^$[]*+?(){}|",c)) { cancelled = 1; break; }
        needle[length++] = c;
    }
    if (cancelled || !length) { joe_free(needle); msgnw(bw->parent,"Lazy search accepts literal text (regex requires normal opening)."); return -1; }
    if (f->encoding) {
        char *utf8 = (char *)joe_malloc(length+1), *at;
        ptrdiff_t left = length;
        int c;
        memcpy(utf8,needle,length); utf8[length] = 0; at = utf8; length = 0;
        while (left > 0 && (c = utf8_decode_fwrd((const char **)&at,&left)) >= 0) {
            char encoded[8]; ptrdiff_t amount;
            amount = f->encoding == 1 ? utf16_encode(encoded,c) : utf16r_encode(encoded,c);
            memcpy(needle+length,encoded,amount); length += amount;
        }
        joe_free(utf8);
    }
    if (backwards) for (i = 0; i < length/2; ++i) { unsigned char swap=needle[i]; needle[i]=needle[length-1-i]; needle[length-1-i]=swap; }
    failure = (ptrdiff_t *)joe_malloc(length*sizeof(*failure)); failure[0] = 0;
    for (i = 1, matched = 0; i < length; ++i) {
        while (matched && needle[i] != needle[matched]) matched = failure[matched-1];
        if (needle[i] == needle[matched]) ++matched;
        failure[i] = matched;
    }
    position = original + (backwards ? -1 : 1);
    while (n-- > 0) {
        found = -1;
        for (pass = 0; pass < 2 && found < 0 && !cancelled; ++pass) {
            JOE_LONG_LONG boundary = pass ? (backwards ? original-1 : (original+length < f->size ? original+length : f->size)) : (backwards ? -1 : f->size);
            matched = 0;
            while ((!backwards && position < boundary) || (backwards && position > boundary)) {
                JOE_LONG_LONG start = backwards ? (position >= LF_IO-1 ? position-LF_IO+1 : 0) : position;
                ptrdiff_t want = backwards ? (ptrdiff_t)(position-start+1) : (boundary-position < LF_IO ? (ptrdiff_t)(boundary-position) : LF_IO);
                ptrdiff_t amount = lf_read(f,start,(char *)bytes,want);
                if (amount != want || lf_cancelled()) { cancelled = 1; break; }
                for (i = backwards ? amount-1 : 0; i >= 0 && i < amount; i += backwards ? -1 : 1) {
                    JOE_LONG_LONG here = start+i;
                    if (backwards && here <= boundary) break;
                    while (matched && bytes[i] != needle[matched]) matched = failure[matched-1];
                    if (bytes[i] == needle[matched]) ++matched;
                    if (matched == length) {
                        JOE_LONG_LONG candidate = backwards ? here : here-length+1;
                        int accepted = (!f->encoding || !(candidate%2)) && (!pass || (backwards ? candidate >= original : candidate <= original));
                        if (word && accepted) {
                            if (lf_word_at(bw,candidate,1) || lf_word_at(bw,candidate+length,0)) accepted=0;
                        }
                        if (accepted) { found = candidate; break; }
                        matched = failure[matched-1];
                    }
                }
                if (found >= 0) break;
                position = backwards ? start-1 : start+amount;
            }
            if (found < 0) position = backwards ? f->size-1 : 0;
        }
        if (found < 0 || cancelled) break;
        original = found; position = found + (backwards ? -1 : 1);
    }
    joe_free(needle); joe_free(failure);
    if (cancelled) { msgnw(bw->parent,"Lazy search cancelled."); return -1; }
    if (found < 0) { msgnw(bw->parent,"Pattern not found."); return -1; }
    return lf_seek(bw,found,-1);
}

/* Stream matching lines without materialising the source or its line index. */
int lf_grep(BW *bw, const char *pattern, int (*emit)(JOE_LONG_LONG,JOE_LONG_LONG,JOE_LONG_LONG,JOE_LONG_LONG,const char *,void *), void *context)
{
    struct large_file *f=bw->b->large;
    unsigned char *needle, bytes[LF_IO], preview[1024];
    ptrdiff_t length=0, i, *failure, matched=0, used=0;
    JOE_LONG_LONG position=0, row=1, line_start=0, hit=-1;
    int unit, result=0;
    if (!f || f->pending || bw->o.hex || !lf_valid(f)) return -1;
    unit=f->encoding ? 2 : 1;
    needle=(unsigned char *)joe_malloc(zlen(pattern)*4+8);
    for (i=0; pattern[i]; ++i) {
        unsigned char c=(unsigned char)pattern[i];
        if (c=='\\' && pattern[i+1] && strchr("\\.^$[]*+?(){}|/",pattern[i+1])) c=(unsigned char)pattern[++i];
        else if (strchr("\\.^$[]*+?(){}|",c)) { result=-3; break; }
        needle[length++]=c;
    }
    if (result || !length) { joe_free(needle); return -3; }
    if (f->encoding) {
        char *text=(char *)joe_malloc(length+1), *at;
        ptrdiff_t left=length;
        int c;
        memcpy(text,needle,length); text[length]=0; at=text; length=0;
        while (left>0 && (c=utf8_decode_fwrd((const char **)&at,&left))>=0) {
            char encoded[8]; ptrdiff_t amount=f->encoding==1 ? utf16_encode(encoded,c) : utf16r_encode(encoded,c);
            memcpy(needle+length,encoded,amount); length+=amount;
        }
        joe_free(text);
    }
    failure=(ptrdiff_t *)joe_malloc(length*sizeof(*failure)); failure[0]=0;
    for (i=1,matched=0;i<length;++i) {
        while (matched && needle[i]!=needle[matched]) matched=failure[matched-1];
        if (needle[i]==needle[matched]) ++matched;
        failure[i]=matched;
    }
    matched=0;
    while (position<f->size && !result) {
        ptrdiff_t amount, want=f->size-position<LF_IO ? (ptrdiff_t)(f->size-position) : LF_IO;
        amount=lf_read(f,position,(char *)bytes,want);
        if (amount!=want || !lf_valid(f)) { result=-1; break; }
        if (lf_cancelled()) { result=-2; break; }
        for (i=0;i<amount;++i) {
            int end_line=0;
            if (used<(ptrdiff_t)sizeof(preview)-8) preview[used++]=bytes[i];
            while (matched && bytes[i]!=needle[matched]) matched=failure[matched-1];
            if (bytes[i]==needle[matched]) ++matched;
            if (matched==length) {
                JOE_LONG_LONG candidate=position+i-length+1;
                if (hit<0 && (!f->encoding || !(candidate%2))) hit=candidate;
                matched=failure[matched-1];
            }
            if (unit==1) end_line=bytes[i]==10;
            else if ((position+i)%2==1 && i>0) end_line=f->encoding==1 ? bytes[i-1]==10 && !bytes[i] : !bytes[i-1] && bytes[i]==10;
            if (end_line || position+i+1==f->size) {
                if (hit>=0) {
                    char *text;
                    if (f->encoding) {
                        struct large_file decoder=*f;
                        unsigned int *map;
                        ptrdiff_t outlen;
                        decoder.start=line_start;
                        text=lf_decode(&decoder,preview,used,&outlen,&map); joe_free(map);
                    } else {
                        preview[used]=0;
                        text=my_iconv(NULL,utf8_map,(const char *)preview,bw->o.charmap);
                    }
                    result=emit(row,hit,line_start,position+i+1,text,context) ? 1 : 0;
                    if (f->encoding) joe_free(text); else obj_free(text);
                    if (result) break;
                }
                ++row; line_start=position+i+1; hit=-1; used=matched=0;
            }
        }
        position+=amount;
    }
    joe_free(needle); joe_free(failure);
    return result;
}
int lf_goto_absolute(BW *bw, JOE_LONG_LONG position) { return lf_seek(bw,position,-1); }

/* Export a known matching line with bounded decoding buffers. Never change
 * the source viewing window or materialise an arbitrarily long source line. */
int lf_export_text(BW *bw, JOE_LONG_LONG start, JOE_LONG_LONG end, FILE *output, JOE_LONG_LONG *written, JOE_LONG_LONG limit)
{
    struct large_file *f=bw->b->large;
    unsigned char bytes[LF_IO+1];
    int unit, k;
    if (!f || start<0 || end<start || end>f->size || !lf_valid(f)) return -1;
    unit=f->encoding ? 2 : 1;
    for (k=0;k<2 && end-start>=unit;++k) {
        int c;
        if (lf_read(f,end-unit,(char *)bytes,unit)!=unit) return -1;
        c=unit==1 ? bytes[0] : f->encoding==1 ? bytes[0]|bytes[1]<<8 : bytes[0]<<8|bytes[1];
        if (c!=(k ? 13 : 10)) break;
        end-=unit;
    }
    if (!start && !f->encoding && bw->b->o.charmap->type && end>=3 && lf_read(f,0,(char *)bytes,3)==3 && !memcmp(bytes,"\357\273\277",3)) start=3;
    while (start<end) {
        ptrdiff_t amount=end-start<LF_IO ? (ptrdiff_t)(end-start) : LF_IO, length;
        char *text;
        unsigned int *map=NULL;
        if (lf_cancelled()) return -2;
        if (lf_read(f,start,(char *)bytes,amount)!=amount || !lf_valid(f)) return -1;
        if (f->encoding) {
            struct large_file decoder=*f;
            int last=amount>=2 ? (f->encoding==1 ? bytes[amount-2]|bytes[amount-1]<<8 : bytes[amount-2]<<8|bytes[amount-1]) : 0;
            if (start+amount<end && last>=0xd800 && last<=0xdbff) amount-=2;
            decoder.start=start;
            text=lf_decode(&decoder,bytes,amount,&length,&map);
        } else if (bw->b->o.charmap->type) { text=(char *)bytes; length=amount; }
        else { bytes[amount]=0; text=my_iconv(NULL,utf8_map,(char *)bytes,bw->b->o.charmap); length=zlen(text); }
        if (*written+length>limit || fwrite(text,1,length,output)!=(size_t)length) {
            if (map) { joe_free(map); joe_free(text); }
            else if (text!=(char *)bytes) obj_free(text);
            return *written+length>limit ? -4 : -1;
        }
        *written+=length;
        if (map) { joe_free(map); joe_free(text); }
        else if (text!=(char *)bytes) obj_free(text);
        start+=amount;
    }
    return lf_valid(f) ? 0 : -1;
}
