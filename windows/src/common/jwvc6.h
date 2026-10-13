/* Compatibility with the SDK shipped with Visual C++ 6.0 (32-bit only). */
#ifndef JOEWIN_VC6_COMPAT_H
#define JOEWIN_VC6_COMPAT_H

#ifndef CSIDL_LOCAL_APPDATA
#define CSIDL_LOCAL_APPDATA 0x001c
#endif
#ifndef CSIDL_PROFILE
#define CSIDL_PROFILE 0x0028
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
#endif
#ifndef DS_FIXEDSYS
#define DS_FIXEDSYS 0x0008L
#endif
#ifndef LOCALE_NAME_MAX_LENGTH
#define LOCALE_NAME_MAX_LENGTH 85
#endif

#ifndef RC_INVOKED
/* These pointer-sized types and operations are 32-bit in every VC6 build. */
typedef long LONG_PTR;
#ifndef _INTPTR_T_DEFINED
typedef int intptr_t;
#define _INTPTR_T_DEFINED
#endif
#define InterlockedExchangePointer(target, value) \
    ((PVOID)InterlockedExchange((LPLONG)(target), (LONG)(value)))
#define InterlockedCompareExchangePointer(target, value, compare) \
    ((PVOID)InterlockedCompareExchange((PVOID *)(target), (PVOID)(value), (PVOID)(compare)))

/* FlashWindowEx is loaded dynamically by PuTTY. */
typedef struct {
    UINT cbSize;
    HWND hwnd;
    DWORD dwFlags;
    UINT uCount;
    DWORD dwTimeout;
} FLASHWINFO, *PFLASHWINFO;
/* Value from EXTENDED_NAME_FORMAT in newer security.h. */
#define NameUserPrincipal 8

BOOL WINAPI jwGetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE *module);
BOOL WINAPI jwGetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module);
HRESULT WINAPI jwSHGetFolderPathW(HWND window, int folder, HANDLE token,
                                 DWORD flags, LPWSTR path);
BOOL WINAPI jwAttachConsole(DWORD process);
DWORD WINAPI jwGetConsoleProcessList(LPDWORD processes, DWORD count);
char *jwStrtok(char *str, const char *delimiters, char **context);
#ifdef UNICODE
#define GetModuleHandleEx jwGetModuleHandleExW
#else
#define GetModuleHandleEx jwGetModuleHandleExA
#endif
#define SHGetFolderPathW jwSHGetFolderPathW
#define AttachConsole jwAttachConsole
#define GetConsoleProcessList jwGetConsoleProcessList
#define strtok_s jwStrtok
#endif

#endif
