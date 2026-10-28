/* Resource directories for the optional, relocatable macOS terminal package. */
#ifndef MIJOE_MACOS_H
#define MIJOE_MACOS_H

const char *mijoe_macos_config_dir(void);
const char *mijoe_macos_data_dir(void);
char *mijoe_macos_shell_start(int csh);

#undef JOERC
#undef JOEDATA
#define JOERC (mijoe_macos_config_dir())
#define JOEDATA (mijoe_macos_data_dir())

#endif
