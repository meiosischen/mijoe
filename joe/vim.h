/* Portable Vim input layer. Keep this interface usable by Visual C++ 6.0. */
#ifndef JOE_VIM_H
#define JOE_VIM_H

extern int vim_enabled;
void vim_cancel_selection(void);
void vim_capture_selection(void);
/* Per displayed line; owns only its scan pointer, not the cached regex. */
struct vim_highlight {
    P *scan;
    struct regcomp *regex;
    off_t start, end, line;
};
void vim_highlight_begin(struct vim_highlight *highlight, BW *bw, P *line);
int vim_highlight_test(struct vim_highlight *highlight, off_t byte);
void vim_highlight_end(struct vim_highlight *highlight);
int vim_input(va_list args);
int vim_readkey(int key);
int vim_getch(void);
int vim_native_key(int key);
int vim_macro_recording(void);
int vim_macro_playing(void);
int vim_macro_length(void);
const char *vim_macro_status(void);
void vim_macro_capture(int key, int decoded);
int vim_macro_next(int *key, int *decoded);
void vim_macro_applied(void);
void vim_macro_result(int result);
int vim_command_active(void);
const char *vim_command_text(void);
int vim_grep_active(void);
int vim_grep_key(int key);
int vim_grep_render(Screen *t);
int vim_grep_count(void);
int vim_grep_selected(void);
const char *vim_mode_name(void);
unsigned long vim_buffer_number(B *b);
int vim_ex(W *w, const char *line);
#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)
int vim_gui_pasting(void);
int vim_gui_action(int action, const char *path);
void vim_gui_mouse_prepare(void);
#endif

/* Decoded terminal keys, outside the Unicode range. */
#define VIM_LEFT  0x200001
#define VIM_RIGHT 0x200002
#define VIM_UP    0x200003
#define VIM_DOWN  0x200004
#define VIM_HOME  0x200005
#define VIM_END   0x200006
#define VIM_PGUP  0x200007
#define VIM_PGDN  0x200008
#define VIM_DEL   0x200009
#define VIM_PASTE 0x20000a
#define VIM_PASTE_END 0x20000b
#define VIM_CLIPBOARD 0x20000c
#define VIM_UNKNOWN 0x20000d
#define VIM_BACK_CHAR 0x20000e
#define VIM_FORWARD_CHAR 0x20000f
#define VIM_F5 0x200010
#define VIM_F6 0x200011

#endif
