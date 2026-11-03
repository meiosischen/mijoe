#ifndef MIJOE_RECENT_H
#define MIJOE_RECENT_H
#define MJ_RECENT_RO 1
#define MJ_RECENT_HEX 2
#define MJ_RECENT_LAZY 4
#define MJ_RECENT_SOFT 8
void mj_recent_initialize(void);
void mj_recent_record(BW *bw, int opened);
int mj_recent_open(BW *bw);
int mj_recent_active(void);
int mj_recent_key(int key);
int mj_recent_input(va_list args);
int mj_recent_render(Screen *screen);
int mj_recent_mouse(int event, int x, int y);
int mj_recent_count(void);
int mj_recent_selected(void);
int mj_recent_top(void);
int mj_recent_rows(void);
#endif
