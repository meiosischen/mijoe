/* macOS package paths are relative to the real executable, not the working
 * directory or a developer's build/install directory. Other builds use their
 * existing compile-time paths unchanged. */
#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)

static const char fallback_config[] = JOERC;
static const char fallback_data[] = JOEDATA;

#include "types.h"
#include <mach-o/dyld.h>
#include <stdint.h>
#include <limits.h>

static char *config_dir;
static char *data_dir;
static int paths_ready;

static void init_paths(void)
{
	char executable[PATH_MAX], resolved[PATH_MAX], *separator;
	uint32_t size = sizeof(executable);
	size_t length;
	if (paths_ready) return;
	paths_ready = 1;
	if (_NSGetExecutablePath(executable, &size) || !realpath(executable, resolved)) return;
	separator = strrchr(resolved, '/'); /* bin/joe */
	if (!separator) return;
	*separator = 0;
	separator = strrchr(resolved, '/'); /* package root */
	if (!separator) return;
	*separator = 0;
	length = strlen(resolved);
	config_dir = (char *)malloc(length + sizeof("/etc/joe/"));
	data_dir = (char *)malloc(length + sizeof("/share/joe/"));
	if (!config_dir || !data_dir) {
		free(config_dir); free(data_dir);
		config_dir = data_dir = NULL;
		return;
	}
	strcpy(config_dir, resolved); strcat(config_dir, "/etc/joe/");
	strcpy(data_dir, resolved); strcat(data_dir, "/share/joe/");
}

const char *mijoe_macos_config_dir(void)
{
	init_paths();
	return config_dir ? config_dir : fallback_config;
}

const char *mijoe_macos_data_dir(void)
{
	init_paths();
	return data_dir ? data_dir : fallback_data;
}

char *mijoe_macos_shell_start(int csh)
{
	const char *path = mijoe_macos_config_dir();
	char *command = vsdupz(csh ? "source '" : ". '");
	while (*path) {
		if (*path == '\'') command = vsncpy(sv(command), sz("'\\''"));
		else command = vsadd(command, *path);
		++path;
	}
	return vsncpy(sv(command), sz(csh ? "shell.csh'\n" : "shell.sh'\n"));
}

#endif
