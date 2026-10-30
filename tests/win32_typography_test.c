/* VC6/XP test: send real function-key messages to an owned child editor. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD pid, tid;
static HWND editor;
static HKEY settings;
static const char *keyname = "Software\\JoeEditor\\joe";
static const char *names[2] = { "FontSize", "LineSpacing" };
static DWORD saved[2], types[2];
static int existed[2];
static PROCESS_INFORMATION process;
static int checks;
static int visible_caret = 1;

static BOOL CALLBACK find_editor(HWND w, LPARAM unused)
{
    DWORD owner;
    char name[80];
    DWORD thread = GetWindowThreadProcessId(w, &owner);
    if (owner == pid && IsWindowVisible(w)) {
        GetClassNameA(w, name, sizeof(name));
        if (!strcmp(name, "joewin")) { editor = w; tid = thread; return FALSE; }
    }
    return TRUE;
}

static int metric(RECT *caret)
{
    GUITHREADINFO info;
    memset(&info, 0, sizeof(info)); info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(tid, &info) || info.hwndCaret != editor) return 0;
    *caret = info.rcCaret;
    return 1;
}

static void text(const char *s)
{
    while (*s) { PostMessage(editor, WM_CHAR, (unsigned char)*s++, 1); Sleep(15); }
    Sleep(150);
}

static int value(int which)
{
    DWORD n = 0, type, bytes = sizeof(n);
    if (RegQueryValueExA(settings, names[which], NULL, &type, (BYTE *)&n, &bytes) || type != REG_DWORD) return -999;
    return (int)n;
}

static int pair(UINT key, int font, int spacing, int dh)
{
    RECT before, after, window, resized;
    DWORD reply;
    memset(&before, 0, sizeof(before));
    if (visible_caret && !metric(&before)) { printf("FAIL cannot read initial caret\n"); return 0; }
    after = before;
    GetWindowRect(editor, &window);
    PostMessage(editor, WM_KEYDOWN, key, 1 | ((0x3b + key - VK_F1) << 16));
    PostMessage(editor, WM_KEYUP, key, 0xc0000001 | ((0x3b + key - VK_F1) << 16));
    Sleep(200);
    if (!SendMessageTimeoutA(editor, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 10000, &reply)) {
        printf("FAIL font refresh did not finish\n"); return 0;
    }
    GetWindowRect(editor, &resized);
    if ((visible_caret && !metric(&after)) || value(0) != font || value(1) != spacing ||
        memcmp(&window, &resized, sizeof(window)) ||
        (visible_caret && dh && after.bottom - after.top != before.bottom - before.top + dh) ||
        (visible_caret && !dh && key == VK_F1 && after.bottom - after.top <= before.bottom - before.top) ||
        (visible_caret && !dh && key == VK_F2 && after.bottom - after.top >= before.bottom - before.top)) {
        printf("FAIL F%u FS=%d H=%d caret=%ldx%ld -> %ldx%ld\n", key - VK_F1 + 1,
               value(0), value(1), before.right-before.left, before.bottom-before.top,
               after.right-after.left, after.bottom-after.top);
        printf("window %ld,%ld,%ld,%ld -> %ld,%ld,%ld,%ld\n",
               window.left, window.top, window.right, window.bottom,
               resized.left, resized.top, resized.right, resized.bottom);
        return 0;
    }
    ++checks;
    return 1;
}

static int cycle(const char *mode)
{
    if (!pair(VK_F1, 15, 0, 0) || !pair(VK_F2, 14, 0, 0) ||
        !pair(VK_F3, 14, 1, 1) || !pair(VK_F4, 14, 0, -1)) return 0;
    printf("PASS F1-F4 preserve %s and fixed window geometry\n", mode);
    return 1;
}

int main(int argc, char **argv)
{
    STARTUPINFOA startup;
    DWORD disposition, n, bytes;
    char command[4096], file[1200], contents[80];
    FILE *input;
    int i, ok = 0, base_height;
    RECT caret;
    if (argc != 3) return 2;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, keyname, 0, NULL, 0, KEY_ALL_ACCESS,
                        NULL, &settings, &disposition)) return 3;
    for (i = 0; i < 2; ++i) {
        bytes = sizeof(DWORD);
        existed[i] = !RegQueryValueExA(settings, names[i], NULL, &types[i], (BYTE *)&saved[i], &bytes);
        n = i ? 0 : 14; RegSetValueExA(settings, names[i], 0, REG_DWORD, (BYTE *)&n, sizeof(n));
    }
    sprintf(file, "%s\\input.txt", argv[2]);
    input = fopen(file, "wb"); if (!input) goto done;
    fputs("alpha\nbeta\n", input); fclose(input);
    sprintf(command, "\"%s\" -nonotice --joe_state -nobackups -nolocks \"%s\"", argv[1], file);
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, argv[2], &startup, &process)) goto done;
    pid = process.dwProcessId;
    for (i = 0; i < 150 && !editor; ++i) { EnumWindows(find_editor, 0); Sleep(100); }
    if (!editor) { printf("FAIL no child window\n"); goto done; }
    SetForegroundWindow(editor); Sleep(300);
    if (!metric(&caret)) goto done;
    base_height = caret.bottom - caret.top;
    if (!cycle("Normal")) goto done;
    if (!pair(VK_F4, 14, -1, -1) || !pair(VK_F3, 14, 0, 1)) goto done;
    text("iN\033i"); if (!cycle("Insert")) goto done;
    text("I\0330v"); if (!cycle("Visual")) goto done;
    text("du:w"); if (!cycle("command input")) goto done;
    text("\r:help\r"); if (!cycle("help")) goto done;
    text("qiH\033:grep alpha\r");
    /* The document caret is hidden during grep; its accessibility rectangle
     * is not a reliable font measurement until the popup closes. */
    visible_caret = 0;
    if (!cycle("grep")) goto done;
    text("iBAD\033");
    visible_caret = 1;
    ShowWindow(editor, SW_MAXIMIZE); Sleep(300);
    if (!cycle("maximized window")) goto done;
    ShowWindow(editor, SW_RESTORE); Sleep(300);
    text(":wq\r");
    if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) { printf("FAIL did not exit\n"); goto done; }
    GetExitCodeProcess(process.hProcess, &n); if (n) goto done;
    input = fopen(file, "rb"); if (!input) goto done;
    bytes = (DWORD)fread(contents, 1, sizeof(contents), input); fclose(input);
    if (bytes != 14 || memcmp(contents, "HINalpha\nbeta\n", 14)) {
        printf("FAIL text or selection/mode changed, length %lu\n", bytes); goto done;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    memset(&process, 0, sizeof(process));
    n = (DWORD)-2; RegSetValueExA(settings, names[1], 0, REG_DWORD, (BYTE *)&n, sizeof(n));
    editor = NULL;
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, argv[2], &startup, &process)) goto done;
    pid = process.dwProcessId;
    for (i = 0; i < 150 && !editor; ++i) { EnumWindows(find_editor, 0); Sleep(100); }
    if (!editor) goto done;
    SetForegroundWindow(editor); Sleep(300);
    if (!metric(&caret) || caret.bottom - caret.top != base_height - 2) {
        printf("FAIL persisted line spacing not loaded\n"); goto done;
    }
    if (!pair(VK_F3, 14, -1, 1) || !pair(VK_F4, 14, -2, -1)) goto done;
    text(":q\r");
    if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) goto done;
    GetExitCodeProcess(process.hProcess, &n); if (n) goto done;
    printf("PASS saved typography loads after restarting the editor\n");
    printf("PASS %d key checks, visible caret metrics, mode continuity, window geometry and saved text\n", checks);
    ok = 1;
done:
    if (process.hProcess) {
        GetExitCodeProcess(process.hProcess, &n);
        if (n == STILL_ACTIVE) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
    for (i = 0; i < 2; ++i) {
        if (existed[i]) RegSetValueExA(settings, names[i], 0, types[i], (BYTE *)&saved[i], sizeof(DWORD));
        else RegDeleteValueA(settings, names[i]);
    }
    RegCloseKey(settings);
    return ok ? 0 : 1;
}
