/* Bounded, immutable source-file windows. File offsets stay 64-bit on Win32. */
#ifndef MIJOE_LFILE_H
#define MIJOE_LFILE_H
struct large_file;
extern int lf_threshold_mb;
extern int lf_force;
B *lf_probe(int fd, const char *name, int *binary);
void lf_release(B *b);
int lf_active(B *b);
int lf_pending(B *b);
int lf_choose(BW *bw, int choice);
int lf_prompt_key(BW *bw, int key);
void lf_options(B *b, OPTIONS *o);
void lf_follow(BW *bw);
void lf_startup_option(B *b, const char *option);
int lf_motion(BW *bw, int key, long n, int counted);
int lf_line_jump(BW *bw, JOE_LONG_LONG line);
int lf_search(BW *bw, const char *pattern, int backwards, long n);
JOE_LONG_LONG lf_absolute(B *b, off_t byte);
JOE_LONG_LONG lf_size(B *b);
JOE_LONG_LONG lf_read_bytes(B *b);
const char *lf_status(BW *bw);
int lf_row_label(BW *bw, char *text, int size);
int lf_column_label(BW*,char*,int size);
int lf_goto_absolute(BW *bw, JOE_LONG_LONG position);
int lf_grep(BW *bw, const char *pattern, int (*emit)(JOE_LONG_LONG,JOE_LONG_LONG,const char *,void *), void *context);
#endif
