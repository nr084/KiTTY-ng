#include "putty.h"
#include <wchar.h>
#include "kittyng-companions.h"

#if defined(__aarch64__) || defined(_M_ARM64)
#define KR_ARCH "arm64"
#define KR_MACHINE IMAGE_FILE_MACHINE_ARM64
#elif defined(_WIN64)
#define KR_ARCH "x64"
#define KR_MACHINE IMAGE_FILE_MACHINE_AMD64
#else
#define KR_ARCH "x86"
#define KR_MACHINE IMAGE_FILE_MACHINE_I386
#endif

const char *kr_companion_name(const char *tool)
{
    static const struct { const char *tool, *file; } tools[] = {
        {"kitty-ng", "kitty-ng-" KR_ARCH ".exe"},
        {"klink", "klink-" KR_ARCH ".exe"},
        {"kscp", "kscp-" KR_ARCH ".exe"},
        {"ksftp", "ksftp-" KR_ARCH ".exe"},
        {"kittygen", "kittygen-" KR_ARCH ".exe"},
        {"kageant", "kageant-" KR_ARCH ".exe"},
    };
    for (size_t i = 0; i < lenof(tools); i++)
        if (!strcmp(tool, tools[i].tool))
            return tools[i].file;
    return NULL;
}

wchar_t *kr_companion_path(const char *tool, DWORD *error)
{
    const char *name = kr_companion_name(tool);
    *error = ERROR_INVALID_PARAMETER;
    if (!name) return NULL;
    DWORD size = 512, length;
    wchar_t *module = NULL;
    while (true) {
        module = sresize(module, size, wchar_t);
        length = GetModuleFileNameW(NULL, module, size);
        if (!length) { *error = GetLastError(); sfree(module); return NULL; }
        if (length < size) break;
        if (size >= 32768) { *error = ERROR_INSUFFICIENT_BUFFER; sfree(module); return NULL; }
        size *= 2;
    }
    wchar_t *slash = wcsrchr(module, L'\\');
    if (!slash) { sfree(module); return NULL; }
    size_t prefix = slash - module + 1;
    wchar_t *filename = dup_mb_to_wc(CP_UTF8, name);
    wchar_t *path = snewn(prefix + wcslen(filename) + 1, wchar_t);
    wmemcpy(path, module, prefix);
    wcscpy(path + prefix, filename);
    sfree(module); sfree(filename);

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError(); sfree(path); return NULL;
    }
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER header;
    DWORD read, signature;
    bool valid = ReadFile(file, &dos, sizeof(dos), &read, NULL) &&
        read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew >= sizeof(dos) && dos.e_lfanew <= 1024 * 1024 &&
        SetFilePointer(file, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(file, &signature, sizeof(signature), &read, NULL) &&
        read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE &&
        ReadFile(file, &header, sizeof(header), &read, NULL) &&
        read == sizeof(header) && header.Machine == KR_MACHINE;
    CloseHandle(file);
    if (!valid) { *error = ERROR_BAD_EXE_FORMAT; sfree(path); return NULL; }
    *error = ERROR_SUCCESS;
    return path;
}

bool kr_launch_companion(const char *tool, const wchar_t *arguments,
                         bool wait, DWORD *exit_code, DWORD *error)
{
    wchar_t *path = kr_companion_path(tool, error);
    if (!path) return false;
    size_t size = wcslen(path) + wcslen(arguments) + 5;
    wchar_t *command = snewn(size, wchar_t);
    swprintf(command, size, L"\"%ls\" %ls", path, arguments);
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.cb = sizeof(startup);
    bool success = CreateProcessW(path, command, NULL, NULL, false, 0,
                                   NULL, NULL, &startup, &process);
    *error = success ? ERROR_SUCCESS : GetLastError();
    if (success) {
        CloseHandle(process.hThread);
        if (wait) {
            if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
                !GetExitCodeProcess(process.hProcess, exit_code)) {
                *error = GetLastError(); success = false;
            }
        }
        CloseHandle(process.hProcess);
    }
    sfree(command); sfree(path);
    return success;
}

void kr_quote_companion_argument(strbuf *command, const char *argument)
{
    put_byte(command, '"');
    while (*argument) {
        unsigned slashes = 0;
        while (*argument == '\\') { slashes++; argument++; }
        unsigned copies = (*argument == '"' || !*argument) ? 2 * slashes : slashes;
        while (copies--) put_byte(command, '\\');
        if (*argument == '"') put_byte(command, '\\');
        if (*argument) put_byte(command, *argument++);
    }
    put_byte(command, '"');
}
