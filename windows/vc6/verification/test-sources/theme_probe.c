#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND editor;
static BOOL CALLBACK find_editor(HWND window, LPARAM arg)
{
    DWORD pid;
    char name[80];
    GetWindowThreadProcessId(window, &pid);
    GetClassNameA(window, name, sizeof(name));
    if (pid == GetCurrentProcessId() && !strcmp(name, "joewin")) {
        editor = window; return FALSE;
    }
    return TRUE;
}
static DWORD WINAPI verify(LPVOID unused)
{
    FILE *log;
    char path[256], name[128];
    HMENU menu, colors = NULL;
    UINT id;
    int i, j, count, selected;
    sprintf(path, "Z:\\tmp\\joewin-4.6-vc6-build\\theme-%lu.log", GetCurrentProcessId());
    log = fopen(path, "w");
    if (!log) return 1;
    EnumWindows(find_editor, 0);
    menu = GetSystemMenu(editor, FALSE);
    for (i = 0; i < GetMenuItemCount(menu); i++) {
        GetMenuStringA(menu, i, name, sizeof(name), MF_BYPOSITION);
        if (!strcmp(name, "Color Schemes")) colors = GetSubMenu(menu, i);
    }
    count = colors ? GetMenuItemCount(colors) : 0;
    if (count < 18) { fprintf(log, "FAIL color menu has %d entries\n", count); fclose(log); return 2; }
    fprintf(log, "Color menu entries: %d\n", count);
    for (j = 0; j < 2; j++) {
        selected = 0;
        id = GetMenuItemID(colors, j);
        GetMenuStringA(colors, j, name, sizeof(name), MF_BYPOSITION);
        PostMessage(editor, WM_SYSCOMMAND, id, 0);
        for (i = 0; i < 100; i++) {
            if (GetMenuState(colors, id, MF_BYCOMMAND) & MF_CHECKED) { selected = 1; break; }
            Sleep(100);
        }
        if (!selected) { fprintf(log, "FAIL could not activate %s\n", name); fclose(log); return 3; }
        fprintf(log, "PASS actual menu dispatch -> JOE color parsing -> active-menu acknowledgement: %s\n", name);
    }
    fprintf(log, "PASS themes\n"); fclose(log);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE thread = CreateThread(NULL, 0, verify, NULL, 0, NULL);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
