/* Portable Vim input layer. Keep this interface usable by Visual C++ 6.0. */
#ifndef JOE_VIM_H
#define JOE_VIM_H

extern int vim_enabled;
int vim_input(va_list args);
int vim_readkey(int key);
int vim_getch(void);
int vim_native_key(int key);
const char *vim_mode_name(void);
unsigned long vim_buffer_number(B *b);
int vim_ex(W *w, const char *line);

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

#endif
