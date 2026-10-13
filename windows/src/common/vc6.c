/* Resolve XP shell/kernel APIs that are absent from the VC6 import libraries. */
#include "jwwin.h"
#include <string.h>

BOOL WINAPI jwGetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *module)
{
    typedef BOOL (WINAPI *GetModuleFn)(DWORD, LPCSTR, HMODULE *);
    GetModuleFn fn = (GetModuleFn)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                               "GetModuleHandleExA");
    if (!fn) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    return fn(flags, name, module);
}

BOOL WINAPI jwGetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module)
{
    typedef BOOL (WINAPI *GetModuleFn)(DWORD, LPCWSTR, HMODULE *);
    GetModuleFn fn = (GetModuleFn)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                               "GetModuleHandleExW");
    if (!fn) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    return fn(flags, name, module);
}

HRESULT WINAPI jwSHGetFolderPathW(HWND window, int folder, HANDLE token,
                                 DWORD flags, LPWSTR path)
{
    typedef HRESULT (WINAPI *GetFolderFn)(HWND, int, HANDLE, DWORD, LPWSTR);
    HMODULE shell = LoadLibraryA("shell32.dll");
    GetFolderFn fn;
    HRESULT result;
    if (!shell)
        return E_NOTIMPL;
    fn = (GetFolderFn)GetProcAddress(shell, "SHGetFolderPathW");
    result = fn ? fn(window, folder, token, flags, path) : E_NOTIMPL;
    FreeLibrary(shell);
    return result;
}

BOOL WINAPI jwAttachConsole(DWORD process)
{
    typedef BOOL (WINAPI *AttachFn)(DWORD);
    AttachFn fn = (AttachFn)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                          "AttachConsole");
    if (!fn) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }
    return fn(process);
}

DWORD WINAPI jwGetConsoleProcessList(LPDWORD processes, DWORD count)
{
    typedef DWORD (WINAPI *ProcessListFn)(LPDWORD, DWORD);
    ProcessListFn fn = (ProcessListFn)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                    "GetConsoleProcessList");
    if (!fn) {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return 0;
    }
    return fn(processes, count);
}

char *jwStrtok(char *str, const char *delimiters, char **context)
{
    char *start = str ? str : *context;
    char *end;
    if (!start)
        return NULL;
    start += strspn(start, delimiters);
    if (!*start) {
        *context = NULL;
        return NULL;
    }
    end = start + strcspn(start, delimiters);
    if (*end) {
        *end = '\0';
        *context = end + 1;
    } else {
        *context = NULL;
    }
    return start;
}
