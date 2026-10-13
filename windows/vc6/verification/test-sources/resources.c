#include "jwwin.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "jwbuiltin.h"

int main(int argc, char **argv)
{
    HMODULE module;
    const wchar_t *name = NULL;
    wchar_t input[128];
    char path[2048], narrow[256];
    int count = 0, themes = 0;
    JFILE *file;
    FILE *output;
    if (argc != 3) return 99;
    module = LoadLibraryExA(argv[1], NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) return 1;
    jwAddResourceHandle(module);
    while ((name = jwnextbuiltin(name, NULL)) != NULL) {
        wcscpy(input, L"*"); wcscat(input, name);
        file = jwfopen(input, L"r");
        if (!file || !file->p || !file->sz) return 2;
        wcstombs(narrow, name, sizeof(narrow));
        sprintf(path, "%s\\%s", argv[2], narrow);
        output = fopen(path, "wb");
        if (!output || fwrite(file->p, 1, file->sz, output) != file->sz) return 3;
        fclose(output); jwfclose(file); count++;
    }
    name = NULL;
    while ((name = jwnextbuiltin(name, L".jcf")) != NULL) themes++;
    FreeLibrary(module);
    printf("PASS decoded %d embedded resources using the actual JOE/miniz loader; themes=%d\n", count, themes);
    return themes == 18 ? 0 : 4;
}
