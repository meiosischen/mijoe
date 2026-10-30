/* macOS package paths are relative to the real executable, not the working
 * directory or a developer's build/install directory. Other builds use their
 * existing compile-time paths unchanged. */
#if defined(__APPLE__) && defined(MIJOE_PORTABLE_MACOS)

static const char fallback_config[] = JOERC;
static const char fallback_data[] = JOEDATA;

#include "types.h"
#include "vim.h"
#include <mach-o/dyld.h>
#include <stdint.h>
#include <limits.h>
#include <arpa/inet.h>
#include <sys/select.h>

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

static int gui_initialized, gui_request = -1, gui_response = -1;
static char *last_gui_status;

static void gui_init(void)
{
	const char *request, *response;
	if (gui_initialized) return;
	gui_initialized = 1;
	request = getenv("MIJOE_GUI_REQUEST_FD");
	response = getenv("MIJOE_GUI_RESPONSE_FD");
	if (!request || !response) return;
	gui_request = atoi(request); gui_response = atoi(response);
	if (gui_request < 3 || gui_response < 3 || gui_request == gui_response ||
	    fcntl(gui_request, F_GETFD) < 0 || fcntl(gui_response, F_GETFD) < 0) {
		gui_request = gui_response = -1; return;
	}
	/* Shells started from the editor never inherit the GUI control channel. */
	fcntl(gui_request, F_SETFD, FD_CLOEXEC);
	fcntl(gui_response, F_SETFD, FD_CLOEXEC);
}

static BW *gui_buffer(void)
{
	W *w;
	if (!maint || !maint->curwin) return NULL;
	w = maint->curwin;
	if (!(w->watom->what & TYPETW)) w = w->main;
	return w && (w->watom->what & TYPETW) ? (BW *)w->object : NULL;
}

static int gui_dirty_count(void)
{
	B *b;
	int count = 0;
	for (b = bufs.link.next; b != &bufs; b = b->link.next)
		if (!b->internal && !b->scratch && b->changed) ++count;
	return count;
}

static char *gui_json_string(char *out, const char *value)
{
	char escaped[8];
	unsigned char c;
	out = vsadd(out, '"');
	while (value && *value) {
		c = (unsigned char)*value++;
		if (c == '"' || c == '\\') { out = vsadd(out, '\\'); out = vsadd(out, c); }
		else if (c < 32) { sprintf(escaped, "\\u%04x", c); out = vsncpy(sv(out), sz(escaped)); }
		else out = vsadd(out, c);
	}
	return vsadd(out, '"');
}

/* Resolve the active scheme for native padding, caret and IME composition.
 * Attributes in a truecolor set are indices into the scheme's RGB palette. */
static int gui_rgb(int attr, int background)
{
	static const int ansi[16] = { 0x000000, 0xcd0000, 0x00cd00, 0xcdcd00,
		0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5, 0x7f7f7f, 0xff0000,
		0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff };
	static const int cube[6] = { 0, 95, 135, 175, 215, 255 };
	int index;
	if (!(attr & (background ? BG_NOT_DEFAULT : FG_NOT_DEFAULT))) return -1;
	index = background ? (attr & BG_VALUE) >> BG_SHIFT : (attr & FG_VALUE) >> FG_SHIFT;
	if (attr & (background ? BG_TRUECOLOR : FG_TRUECOLOR))
		return curschemeset && curschemeset->palette ? curschemeset->palette[index] : -1;
	if (index < 16) return ansi[index];
	if (index >= 232) return (8 + (index - 232) * 10) * 0x010101;
	index -= 16;
	return (cube[index / 36] << 16) | (cube[index / 6 % 6] << 8) | cube[index % 6];
}

static double gui_font_size = 13, gui_line_spacing = -6;

/* Native frontend commits complete terminal frames, never partial repaint bytes. */
void mijoe_macos_gui_frame(int begin)
{
    gui_init();
    if (gui_response<0) return;
    ttputs(begin ? "\033[?2026h" : "\033[?2026l");
    if (!begin) ttflsh();
}

const char *mijoe_macos_gui_typography(void)
{
	static char label[80];
	gui_init();
	if (gui_response < 0) return NULL;
	sprintf(label, " FS %g H %g ", gui_font_size, gui_line_spacing);
	return label;
}

static void gui_report(unsigned long id, int ok, const char *error, const char *text, int update)
{
	BW *bw = gui_buffer();
	char number[512], *json;
	if (gui_response < 0 || !bw) return;
	sprintf(number, "{\"id\":%lu,\"ok\":%s,\"changed\":%s,\"dirtyBuffers\":%d,\"readonly\":%s,\"file\":",
	        id, ok ? "true" : "false", bw->b->changed ? "true" : "false",
	        gui_dirty_count(), bw->b->rdonly ? "true" : "false");
	json = gui_json_string(vsdupz(number), bw->b->name);
    sprintf(number, ",\"largePending\":%s,\"lazy\":%s,\"binary\":%s,\"normalAllowed\":%s",
            lf_pending(bw->b) ? "true" : "false", lf_active(bw->b) ? "true" : "false",
            bw->b->binary_file ? "true" : "false", lf_size(bw->b) <= (JOE_LONG_LONG)MAXOFF ? "true" : "false");
    json = vsncpy(sv(json),sz(number));
    sprintf(number, ",\"fileBytes\":%" JOE_INT64_FORMAT "d,\"fileOffset\":%" JOE_INT64_FORMAT "d,\"readBytes\":%" JOE_INT64_FORMAT "d,\"loadedBytes\":%" JOE_INT64_FORMAT "d",
            lf_size(bw->b),lf_absolute(bw->b,bw->cursor->byte),lf_read_bytes(bw->b),(JOE_LONG_LONG)bw->b->eof->byte);
    json = vsncpy(sv(json),sz(number));
	sprintf(number,",\"grepActive\":%s,\"grepCount\":%d,\"grepSelected\":%d",vim_grep_active() ? "true" : "false",vim_grep_count(),vim_grep_selected());
    json=vsncpy(sv(json),sz(number));
	json = vsncpy(sv(json), sz(",\"scheme\":"));
	json = gui_json_string(json, scheme_name);
	sprintf(number, ",\"textForeground\":%d,\"textBackground\":%d,\"cursorBackground\":%d",
	        gui_rgb(bg_text, 0), gui_rgb(bg_text, 1), gui_rgb(bg_cursor, 1));
	json = vsncpy(sv(json), sz(number));
	json = vsncpy(sv(json), sz(",\"mode\":"));
	json = gui_json_string(json, vim_mode_name());
	sprintf(number,",\"macroRecording\":%s,\"macroPlaying\":%s,\"macroSteps\":%d",vim_macro_recording() ? "true" : "false",vim_macro_playing() ? "true" : "false",vim_macro_length());
    json=vsncpy(sv(json),sz(number));
    json=vsncpy(sv(json),sz(",\"command\":"));
    json=gui_json_string(json,vim_command_text());
	json = vsncpy(sv(json), sz(",\"error\":"));
	json = gui_json_string(json, error);
	if (text) { json = vsncpy(sv(json), sz(",\"text\":")); json = gui_json_string(json, text); }
	json = vsncpy(sv(json), sz("}\n"));
	if (!update || !last_gui_status || zcmp(last_gui_status, json)) {
		joe_write(gui_response, json, zlen(json));
		if (update) {
			if (last_gui_status) joe_free(last_gui_status);
			last_gui_status = zdup(json);
		}
	}
	obj_free(json);
}

void mijoe_macos_gui_update(void)
{
	gui_init();
	if (gui_response >= 0) gui_report(0, 1, NULL, NULL, 1);
}

int mijoe_macos_gui_pending(void)
{
	fd_set input;
	struct timeval timeout;
	gui_init();
	if (gui_request < 0) return 0;
	FD_ZERO(&input); FD_SET(gui_request, &input);
	timeout.tv_sec = timeout.tv_usec = 0;
	return select(gui_request + 1, &input, NULL, NULL, &timeout) > 0;
}

int mijoe_macos_gui_action(va_list args)
{
	uint32_t header[3];
	unsigned long length, id;
	int action, rc = 0;
	char path[1024], *text = NULL, *raw;
	const char *error = NULL;
	BW *bw = gui_buffer();
	if (joe_read(gui_request, (char *)header, sizeof(header)) != sizeof(header)) return -1;
	length = ntohl(header[0]); id = ntohl(header[1]); action = ntohl(header[2]);
	if (length >= sizeof(path)) { close(gui_request); gui_request = -1; return -1; }
	if (length && joe_read(gui_request, path, length) != length) return -1;
	path[length] = 0;
	if (strlen(path) != length) error = "The filename contains a NUL byte.";
	else if (action == MIJOE_GUI_STATUS) { /* Status is also available while a menu is open. */ }
    else if (action == MIJOE_GUI_LARGE_CHOICE) {
        int choice = !zcmp(path,"lazy") ? 0 : !zcmp(path,"normal") ? 1 : !zcmp(path,"cancel") ? 2 : -1;
        if (choice < 0 || !bw || !lf_pending(bw->b) || lf_choose(bw,choice)) error = "Large-file opening was not completed.";
    }
	else if (action == MIJOE_GUI_TYPOGRAPHY) {
		double font, spacing;
		char trailing;
		if (sscanf(path, "%lf %lf %c", &font, &spacing, &trailing) != 2 ||
		    !(font >= 9 && font <= 32 && spacing >= -64 && spacing <= 32))
			error = "Invalid typography values.";
		else { gui_font_size = font; gui_line_spacing = spacing; }
	}
	else if (vim_grep_active() && action!=MIJOE_GUI_SCROLL && action!=MIJOE_GUI_QUIT && action!=MIJOE_GUI_DISCARD) error="Close the grep results with Enter or Esc before using this command.";
	else if (action == MIJOE_GUI_MOUSE) {
		int event, column, row;
		if (sscanf(path, "%d;%d;%d", &event, &column, &row) != 3 ||
		    column < 0 || column > 999 || row < 0 || row > 499) error = "Invalid mouse position.";
		else if (event == 1) { vim_gui_mouse_prepare(); mousedn(column + 1, row + 1); }
		else if (event == 2) mousedrag(column + 1, row + 1);
		else if (event == 3) mouseup(column + 1, row + 1);
		else error = "Invalid mouse event.";
	} else if (action == MIJOE_GUI_SCROLL) {
		int amount = atoi(path), steps = abs(amount);
		int menu = maint->curwin->watom->what & TYPEMENU;
		const char *name = menu ? (amount > 0 ? "uparwmenu" : "dnarwmenu") : (amount > 0 ? "upslide" : "dnslide");
		if (steps > 20) steps = 20;
		while (steps--) { if (vim_grep_active()) vim_grep_key(amount>0 ? VIM_UP : VIM_DOWN); else execmd(findcmd(name), 0); }
	}
	else if (action == MIJOE_GUI_QUIT || action == MIJOE_GUI_DISCARD) {
		if (action == MIJOE_GUI_QUIT && gui_dirty_count()) error = "There are unsaved buffers.";
		else leave = 1;
	} else if (!bw || maint->curwin != bw->parent || bw->b->pid) {
		error = "Close the editor menu or prompt before using this command.";
	} else {
		if (action == MIJOE_GUI_COPY || action == MIJOE_GUI_CUT) {
			if (!markv(0) || markb->b != bw->b || square) error = "Select text with v/V or the mouse first.";
			else if (markk->byte - markb->byte > 16 * 1024 * 1024) error = "The selection exceeds 16 MiB.";
			else {
				raw = brs(markb, markk->byte - markb->byte);
				text = my_iconv(NULL, utf8_map, raw, bw->b->o.charmap);
				joe_free(raw);
			}
		}
		if (!error) {
			rc = vim_gui_action(action, path);
			if (rc) error = "The editor could not complete the command. Check the message at the bottom of the window.";
		}
	}
	if (id || error) gui_report(id, error == NULL, error, text, 0);
	if (text) obj_free(text);
	staupd = dostaupd = 1;
	return error ? -1 : rc;
}

#endif
