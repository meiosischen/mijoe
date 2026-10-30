/* Resource directories for the optional, relocatable macOS terminal package. */
#ifndef MIJOE_MACOS_H
#define MIJOE_MACOS_H

const char *mijoe_macos_config_dir(void);
const char *mijoe_macos_data_dir(void);
char *mijoe_macos_shell_start(int csh);
void mijoe_macos_gui_update(void);
void mijoe_macos_gui_frame(int begin);
const char *mijoe_macos_gui_typography(void);
int mijoe_macos_gui_pending(void);
int mijoe_macos_gui_action(va_list args);
int mijoe_macos_gui_open_text(const char *path);
int mijoe_macos_gui_list_buffers(void);

#include "../macos/gui_protocol.h"

#undef JOERC
#undef JOEDATA
#define JOERC (mijoe_macos_config_dir())
#define JOEDATA (mijoe_macos_data_dir())

#endif
