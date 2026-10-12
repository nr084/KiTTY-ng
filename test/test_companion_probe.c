/* GUI-subsystem probe: records the actual executable and argv without UI. */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>

static int record(FILE *file, const wchar_t *text)
{
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char *bytes = malloc(size);
    if (!bytes) return 0;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, bytes, size, NULL, NULL);
    int ok = fwrite(bytes, 1, size, file) == (size_t)size;
    free(bytes);
    return ok;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 10;
    const wchar_t *report = argc >= 2 ? argv[1] : _wgetenv(L"KITTY_NG_PROBE_REPORT");
    if (!report) { LocalFree(argv); return 10; }
    FILE *file = _wfopen(report, L"wb");
    if (!file) { LocalFree(argv); return 11; }
    wchar_t module[32768];
    DWORD length = GetModuleFileNameW(NULL, module, sizeof(module) / sizeof(*module));
    int ok = length && length < sizeof(module) / sizeof(*module) && record(file, module);
    for (int i = 1; i < argc; i++) ok = record(file, argv[i]) && ok;
    ok = fclose(file) == 0 && ok;
    LocalFree(argv);
    return ok ? 0 : 12;
}
