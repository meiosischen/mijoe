#ifndef MIJOE_COMMAND_HISTORY_H
#define MIJOE_COMMAND_HISTORY_H
/* Only submitted command-bar input. Origin: 0 input, 1 recording, 2 playback. */
int mj_cmdhistory_add(int prefix, const char *command, int origin);
char *mj_cmdhistory_text(void);
const char *mj_cmdhistory_error(void);
#endif
