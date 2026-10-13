#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
static DWORD app_pid;
static HWND app_window;
static int theme_and_keys(HANDLE process)
{
    HANDLE thread, snapshot;
    PROCESSENTRY32 entry;
    LPVOID remote;
    const char dll[] = "Z:\\tmp\\joewin-4.6-vc6-build\\theme_probe.dll";
    char path[256], data[4096];
    DWORD written, status;
    FILE *log;
    size_t n;
    int i, children = 0, passed = 0;
    sprintf(path, "Z:\\tmp\\joewin-4.6-vc6-build\\theme-%lu.log", app_pid);
    DeleteFileA(path);
    data[0] = 0;
    remote = VirtualAllocEx(process, NULL, sizeof(dll), MEM_COMMIT, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(process, remote, dll, sizeof(dll), &written)) return 1;
    thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, remote, 0, NULL);
    if (!thread) return 1;
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) return 1;
    GetExitCodeThread(thread, &status); CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    if (!status) { printf("could not load local menu-verification DLL\n"); return 1; }
    sprintf(path, "Z:\\tmp\\joewin-4.6-vc6-build\\theme-%lu.log", app_pid);
    for (i = 0; i < 150; i++) {
        log = fopen(path, "rb");
        if (log) {
            n = fread(data, 1, sizeof(data) - 1, log); data[n] = 0; fclose(log);
            if (strstr(data, "PASS themes")) { passed = 1; break; }
            if (strstr(data, "FAIL")) break;
        }
        Sleep(100);
    }
    if (!passed) { printf("theme probe failed: %s\n", data); return 1; }
    printf("%s", data);
    for (i = 0; i < 4; i++) {
        PostMessage(app_window, WM_KEYDOWN, VK_F1 + i, 1 | ((0x3b + i) << 16));
        PostMessage(app_window, WM_KEYUP, VK_F1 + i, 0xc0000001 | ((0x3b + i) << 16));
        Sleep(200);
    }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;
    memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) do {
        if (entry.th32ParentProcessID == app_pid) children++;
    } while (Process32Next(snapshot, &entry));
    CloseHandle(snapshot);
    if (children) { printf("unexpected child processes after F1-F4: %d\n", children); return 1; }
    printf("PASS F1-F4 launched no child processes\n");
    return 0;
}
static BOOL CALLBACK find_window(HWND window, LPARAM param)
{
    DWORD pid;
    char name[80];
    GetWindowThreadProcessId(window, &pid);
    if (pid == app_pid && IsWindowVisible(window)) {
        GetClassNameA(window, name, sizeof(name));
        if (!strcmp(name, "joewin")) {
            app_window = window;
            return FALSE;
        }
    }
    return TRUE;
}
static int test(char *exe, int editing)
{
    wchar_t filename[MAX_PATH];
    wchar_t command[2048], wexe[1024];
    const char *input = "VC6-4.6-test\r";
    const char *expected = "VC6-4.6-test\ninitial\n";
    char content[100] = {0};
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    HANDLE file, module;
    DWORD size, code;
    int i, ok=0;
    wsprintfW(filename, L"Z:\\tmp\\joewin-4.6-vc6-build\\\x6d4b\x8bd5-vc6-%lu.txt", GetCurrentProcessId());
    module = LoadLibraryExA(exe, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) { printf("resource load failed %lu\n",GetLastError()); return 1; }
    if (!FindResourceW(module,L"F:*JOERC",MAKEINTRESOURCEW(10)) ||
        !FindResourceW(module,MAKEINTRESOURCEW(200),MAKEINTRESOURCEW(14))) {
        printf("missing config/icon resource\n"); FreeLibrary(module); return 2;
    }
    FreeLibrary(module);
    file=CreateFileW(filename,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    if (file==INVALID_HANDLE_VALUE) return 3;
    WriteFile(file,"initial\n",8,&size,NULL); CloseHandle(file);
    MultiByteToWideChar(CP_ACP,0,exe,-1,wexe,1024);
    wsprintfW(command,L"\"%s\" \"%s\"",wexe,filename);
    memset(&startup,0,sizeof(startup)); startup.cb=sizeof(startup);
    if (!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process)) {
        printf("launch failed %lu\n",GetLastError()); return 4;
    }
    app_pid=process.dwProcessId; app_window=NULL;
    for(i=0;i<150 && !app_window;i++) {
        EnumWindows(find_window,0);
        GetExitCodeProcess(process.hProcess,&code);
        if(code!=STILL_ACTIVE) break;
        Sleep(100);
    }
    if(!app_window) {
        printf("no editor window, exit=%lu\n",code);
        goto finish;
    }
    printf("window opened: %s\n",exe);
    Sleep(1000);
    if (!editing) { ok=1; goto finish; }
    if (editing == 2) {
        const char *command = "echo VC6-cmd-output\r";
        PostMessage(app_window, WM_CHAR, 11, 1); Sleep(100);
        PostMessage(app_window, WM_CHAR, '!', 1); Sleep(100);
        for (i = 0; command[i]; i++) {
            PostMessage(app_window, WM_CHAR, command[i], 1); Sleep(30);
        }
        Sleep(1500);
    } else {
    if (theme_and_keys(process.hProcess)) goto finish;
    PostMessage(app_window,WM_CHAR,11,1); Sleep(100);
    PostMessage(app_window,WM_CHAR,'u',1); Sleep(100);
    for(i=0;input[i];i++) {
        PostMessage(app_window,WM_CHAR,(WPARAM)input[i],1);
        Sleep(50);
    }
    }
    PostMessage(app_window,WM_CHAR,11,1); Sleep(100);
    PostMessage(app_window,WM_CHAR,'x',1);
    if(WaitForSingleObject(process.hProcess,10000)!=WAIT_OBJECT_0) {
        printf("save/exit timed out\n"); goto finish;
    }
    file=CreateFileW(filename,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE) goto finish;
    ReadFile(file,content,sizeof(content)-1,&size,NULL); CloseHandle(file);
    if(editing == 2) {
        if (!strstr(content, "VC6-cmd-output")) { printf("run output missing: %s\n", content); goto finish; }
        printf("PASS ordinary command execution and output capture without joewinpty\n");
    } else {
        if(strcmp(content,expected)) {printf("wrong saved content: %s\n",content);goto finish;}
        printf("PASS Unicode filename + edit + save + exit\n");
    }
    ok=1;
finish:
    GetExitCodeProcess(process.hProcess,&code);
    if(code==STILL_ACTIVE) TerminateProcess(process.hProcess,0);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    return !ok;
}
int main(int argc,char **argv)
{
    if(argc<2)return 99;
    return test(argv[1],argc<3 ? 1 : (!strcmp(argv[2], "run") ? 2 : 0));
}
