/* VC6/XP-compatible UI test. Operates only on the child editor it starts. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static DWORD editor_pid;
static HWND editor_window;

static BOOL CALLBACK find_editor(HWND window, LPARAM unused)
{
    DWORD pid;
    char name[80];
    GetWindowThreadProcessId(window, &pid);
    if (pid == editor_pid && IsWindowVisible(window)) {
        GetClassNameA(window, name, sizeof(name));
        if (!strcmp(name, "joewin")) { editor_window = window; return FALSE; }
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    wchar_t executable[1024], directory[1024], input[1200], output[1200], command[4096];
    STARTUPINFOW startup;
    PROCESS_INFORMATION child;
    unsigned char keys[65536], contents[65536];
    char path[1200];
    FILE *file;
    HANDLE handle;
    DWORD size, code = 0;
    size_t length, i;
    int ok = 0;
    if (argc != 3) return 2;
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, executable, 1024);
    MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, directory, 1024);
    wsprintfW(input, L"%s\\\x6d4b\x8bd5-vim.txt", directory);
    wsprintfW(output, L"%s\\result.bin", directory);
    sprintf(path, "%s\\initial.bin", argv[2]);
    file = fopen(path, "rb");
    if (!file) return 3;
    length = fread(contents, 1, sizeof(contents), file); fclose(file);
    handle = CreateFileW(input, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 4;
    WriteFile(handle, contents, (DWORD)length, &size, NULL); CloseHandle(handle);
    sprintf(path, "%s\\keys.bin", argv[2]);
    file = fopen(path, "rb");
    if (!file) return 5;
    length = fread(keys, 1, sizeof(keys), file); fclose(file);
    wsprintfW(command, L"\"%s\" -nonotice --joe_state -nobackups -nolocks \"%s\"", executable, input);
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, directory, &startup, &child)) {
        printf("FAIL CreateProcess: %lu\n", GetLastError()); return 6;
    }
    editor_pid = child.dwProcessId;
    for (i = 0; i < 150 && !editor_window; ++i) {
        EnumWindows(find_editor, 0);
        GetExitCodeProcess(child.hProcess, &code);
        if (code != STILL_ACTIVE) break;
        Sleep(100);
    }
    if (!editor_window) { printf("FAIL no editor window; exit=%lu\n", code); goto done; }
    Sleep(300);
    for (i = 0; i < length; ++i) {
        PostMessage(editor_window, WM_CHAR, keys[i], 1);
        Sleep(8);
    }
    if (WaitForSingleObject(child.hProcess, 15000) != WAIT_OBJECT_0) {
        printf("FAIL editor did not exit\n"); goto done;
    }
    GetExitCodeProcess(child.hProcess, &code);
    if (code) { printf("FAIL editor exit=%lu\n", code); goto done; }
    if (!CopyFileW(input, output, FALSE)) { printf("FAIL result copy\n"); goto done; }
    printf("PASS child editor opened Unicode filename, handled keys, saved and exited\n");
    ok = 1;
done:
    GetExitCodeProcess(child.hProcess, &code);
    if (code == STILL_ACTIVE) TerminateProcess(child.hProcess, 1);
    CloseHandle(child.hProcess); CloseHandle(child.hThread);
    return ok ? 0 : 1;
}
