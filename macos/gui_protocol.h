/* Private protocol between MiJoe.app and its own child editor.
 * Request: three network-order uint32 values (path length, id, operation),
 * followed by the UTF-8 path. Response: one UTF-8 JSON object per line.
 * The private Unicode key wakes the editor after the complete pipe write. */
#ifndef MIJOE_GUI_PROTOCOL_H
#define MIJOE_GUI_PROTOCOL_H
#define MIJOE_GUI_KEY 0x10fffd
#define MIJOE_GUI_STATUS 1
#define MIJOE_GUI_SAVE 2
#define MIJOE_GUI_SAVE_AS 3
#define MIJOE_GUI_COPY 4
#define MIJOE_GUI_CUT 5
#define MIJOE_GUI_UNDO 6
#define MIJOE_GUI_REDO 7
#define MIJOE_GUI_QUIT 8
#define MIJOE_GUI_DISCARD 9
#define MIJOE_GUI_NEXT_BUFFER 10
#define MIJOE_GUI_PREV_BUFFER 11
#define MIJOE_GUI_HELP 12
#define MIJOE_GUI_SELECT_ALL 13
#define MIJOE_GUI_MOUSE 14
#define MIJOE_GUI_SCROLL 15
#define MIJOE_GUI_TYPOGRAPHY 16
#define MIJOE_GUI_LARGE_CHOICE 17
#define MIJOE_GUI_IMPORT_TEXT 18
#define MIJOE_GUI_LIST_BUFFERS 19
#define MIJOE_GUI_SHOW_BUFFERS 20
#endif
