/* VC6/XP-compatible UI test. Operates only on the child editor it starts. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500
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

/* Detect the cell geometry from this child's caret, then double click
 * only the status row. Compare UTF-16 clipboard data with its full path. */
static int status_path_copy(const wchar_t *expected)
{
    GUITHREADINFO info;
    DWORD thread=GetWindowThreadProcessId(editor_window,NULL);
    int width,height,x,y,column,found=0;
    memset(&info,0,sizeof(info));info.cbSize=sizeof(info);
    if (!GetGUIThreadInfo(thread,&info) || info.hwndCaret!=editor_window) return 0;
    width=info.rcCaret.right-info.rcCaret.left;
    height=info.rcCaret.bottom-info.rcCaret.top;
    if (width<1 || height<1) return 0;
    y=info.rcCaret.top-height+height/2;
    for(column=1;column<26 && !found;++column) {
        int click;
        x=info.rcCaret.left+column*width+width/2;
        for(click=0;click<2;++click) {
            PostMessage(editor_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));Sleep(40);
            PostMessage(editor_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));Sleep(40);
        }
        if (OpenClipboard(NULL)) {
            HANDLE value=GetClipboardData(CF_UNICODETEXT);
            if(value) {
                wchar_t *text=(wchar_t *)GlobalLock(value);
                if(text) {
                    wchar_t normalized[1400];int n;
                    wcsncpy(normalized,text,1399);normalized[1399]=0;
                    for(n=0;normalized[n];++n)if(normalized[n]==L'/')normalized[n]=L'\\';
                    found=!_wcsicmp(normalized,expected);
                    GlobalUnlock(value);
                }
            }
            CloseClipboard();
        }
    }
    return found;
}

static void capture_failure(const char *directory)
{
    RECT r;
    HDC dc=GetDC(editor_window),memory;
    HBITMAP bitmap,previous;
    BITMAPINFO info;
    BITMAPFILEHEADER header;
    unsigned char *pixels;
    DWORD bytes;
    FILE *file;
    char path[1400];
    if(!dc || !GetClientRect(editor_window,&r))return;
    memory=CreateCompatibleDC(dc);bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);
    previous=(HBITMAP)SelectObject(memory,bitmap);BitBlt(memory,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);
    memset(&info,0,sizeof(info));info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    bytes=r.right*r.bottom*4;pixels=(unsigned char *)malloc(bytes);
    SelectObject(memory,previous);
    if(pixels && GetDIBits(dc,bitmap,0,r.bottom,pixels,&info,DIB_RGB_COLORS)) {
        memset(&header,0,sizeof(header));header.bfType=0x4d42;
        header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+bytes;
        sprintf(path,"%s\\failure.bmp",directory);file=fopen(path,"wb");
        if(file){fwrite(&header,1,sizeof(header),file);fwrite(&info.bmiHeader,1,sizeof(BITMAPINFOHEADER),file);fwrite(pixels,1,bytes,file);fclose(file);}
    }
    if(pixels)free(pixels);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(editor_window,dc);
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
    if (argc != 3 && !(argc == 4 && (!strcmp(argv[3], "--verify") || !strcmp(argv[3], "--path") || !strcmp(argv[3], "--wrap")))) return 2;
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
    if (argc==4 && !strcmp(argv[3],"--path")) {
        if (!status_path_copy(input)) { printf("FAIL status path clipboard\n"); goto done; }
        printf("PASS status double click copied the full Unicode path\n");
    }
    if (argc==4 && !strcmp(argv[3],"--wrap")) {
        const char *setup=":set wrap\r\024R12\r";
        GUITHREADINFO info;
        DWORD thread=GetWindowThreadProcessId(editor_window,NULL);
        int x,y,height;
        while(*setup) { PostMessage(editor_window,WM_CHAR,(unsigned char)*setup++,1);Sleep(12); }
        Sleep(250);memset(&info,0,sizeof(info));info.cbSize=sizeof(info);
        if(!GetGUIThreadInfo(thread,&info) || info.hwndCaret!=editor_window) { printf("FAIL wrapped caret geometry\n");goto done; }
        height=info.rcCaret.bottom-info.rcCaret.top;
        /* Win32 frontend rounds clicks to the nearest text caret. */
        x=info.rcCaret.left;
        y=info.rcCaret.top+height+height/2;
        PostMessage(editor_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));Sleep(50);
        PostMessage(editor_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));Sleep(100);
    }
    for (i = 0; i < length; ++i) {
        PostMessage(editor_window, WM_CHAR, keys[i], 1);
        Sleep(8);
    }
    if (WaitForSingleObject(child.hProcess, 15000) != WAIT_OBJECT_0) {
        printf("FAIL editor did not exit; pid=%lu window=%d\n",editor_pid,IsWindow(editor_window));capture_failure(argv[2]); goto done;
    }
    GetExitCodeProcess(child.hProcess, &code);
    if (code) { printf("FAIL editor exit=%lu\n", code); goto done; }
    if (!CopyFileW(input, output, FALSE)) { printf("FAIL result copy\n"); goto done; }
    if (argc == 4) {
        size_t expected_length, actual_length;
        int too_long;
        sprintf(path, "%s\\expected.bin", argv[2]);
        file = fopen(path, "rb");
        if (!file) { printf("FAIL missing expected bytes\n"); goto done; }
        expected_length = fread(keys, 1, sizeof(keys), file);
        too_long = fgetc(file) != EOF; fclose(file);
        if (too_long) { printf("FAIL expected file exceeds test limit\n"); goto done; }
        sprintf(path, "%s\\result.bin", argv[2]);
        file = fopen(path, "rb");
        if (!file) { printf("FAIL missing result bytes\n"); goto done; }
        actual_length = fread(contents, 1, sizeof(contents), file);
        too_long = fgetc(file) != EOF; fclose(file);
        if (too_long || actual_length != expected_length || memcmp(contents, keys, actual_length)) {
            printf("FAIL result differs from expected bytes\n"); goto done;
        }
    }
    printf("PASS child editor opened Unicode filename, handled keys, saved and exited\n");
    ok = 1;
done:
    GetExitCodeProcess(child.hProcess, &code);
    if (code == STILL_ACTIVE) TerminateProcess(child.hProcess, 1);
    CloseHandle(child.hProcess); CloseHandle(child.hThread);
    return ok ? 0 : 1;
}
