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

static bool companion_utf8;
bool kr_companion_utf8(void) { return companion_utf8; }

char *kr_transfer_to_utf8(const wchar_t *text)
{
    /* Terminal character tables are single-byte mappings, even when the
     * terminal uses UTF-8. File names must use the Windows Unicode API. */
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char *result = snewn(size ? size : 1, char);
    result[0] = 0;
    if (size) WideCharToMultiByte(CP_UTF8, 0, text, -1, result, size, NULL, NULL);
    return result;
}

bool kr_import_companion_conf(Conf *conf, const char *token, DWORD *error)
{
    unsigned long long value;
    unsigned long size;
    char trailing;
    *error = ERROR_INVALID_DATA;
    if (sscanf(token, "%llx:%lu%c", &value, &size, &trailing) != 2 ||
        !value || value > UINTPTR_MAX || !size || size > 16 * 1024 * 1024)
        return false;
    HANDLE handle = (HANDLE)(uintptr_t)value;
    DWORD flags;
    if (!GetHandleInformation(handle, &flags) || !(flags & HANDLE_FLAG_INHERIT))
        return false;
    void *data = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, size);
    if (!data) return false;
    CloseHandle(handle);
    BinarySource src[1];
    BinarySource_BARE_INIT(src, data, size);
    Conf *copy = conf_new();
    bool ok = conf_deserialise(copy, src) && !get_err(src) && src->pos == size;
    if (ok) {
        conf_copy_into(conf, copy);
        companion_utf8 = conf_get_bool(conf, CONF_kr_transfer_utf8);
        if (companion_utf8 && GetConsoleWindow()) {
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
        }
        *error = ERROR_SUCCESS;
    }
    conf_free(copy);
    UnmapViewOfFile(data);
    return ok;
}

bool kr_launch_companion_conf(const char *tool, const wchar_t *arguments,
                              Conf *conf, bool hide, DWORD *exit_code, DWORD *error)
{
    wchar_t *path = kr_companion_path(tool, error);
    if (!path) return false;
    strbuf *serialized = strbuf_new();
    conf_serialise(BinarySink_UPCAST(serialized), conf);
    HANDLE mapping = NULL, inherited = NULL;
    HANDLE streams[3] = {NULL, NULL, NULL};
    HANDLE handles[4]; size_t nhandles = 0;
    void *view = NULL;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
    bool attributes_initialized = false, ok = false;
    wchar_t *command = NULL;
    if (!serialized->len || serialized->len > 16 * 1024 * 1024) {
        *error = ERROR_BUFFER_OVERFLOW; goto cleanup;
    }
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                  0, serialized->len, NULL);
    if (!mapping) goto failed;
    view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, serialized->len);
    if (!view) goto failed;
    memcpy(view, serialized->s, serialized->len);
    if (!DuplicateHandle(GetCurrentProcess(), mapping, GetCurrentProcess(),
                         &inherited, FILE_MAP_READ, true, 0)) goto failed;
    handles[nhandles++] = inherited;
    if (hide) {
        const DWORD ids[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        for (size_t i = 0; i < lenof(ids); i++) {
            HANDLE original = GetStdHandle(ids[i]);
            if (original && original != INVALID_HANDLE_VALUE &&
                DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(),
                                &streams[i], 0, true, DUPLICATE_SAME_ACCESS)) {
                handles[nhandles++] = streams[i];
            } else {
                SECURITY_ATTRIBUTES security = {sizeof(security), NULL, true};
                streams[i] = CreateFileW(L"NUL", i ? GENERIC_WRITE : GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
                if (streams[i] == INVALID_HANDLE_VALUE) { streams[i] = NULL; goto failed; }
                handles[nhandles++] = streams[i];
            }
        }
    }
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    attributes = smalloc(attribute_size);
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) goto failed;
    attributes_initialized = true;
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   handles, nhandles * sizeof(*handles), NULL, NULL)) goto failed;
    size_t length = wcslen(path) + wcslen(arguments) + 100;
    command = snewn(length, wchar_t);
    swprintf(command, length, L"\"%ls\" --kitty-ng-conf %llx:%lu %ls", path,
             (unsigned long long)(uintptr_t)inherited, (unsigned long)serialized->len, arguments);
    STARTUPINFOEXW startup = {0};
    PROCESS_INFORMATION process = {0};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    if (hide) {
        startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdInput = streams[0];
        startup.StartupInfo.hStdOutput = streams[1];
        startup.StartupInfo.hStdError = streams[2];
    }
    if (!CreateProcessW(path, command, NULL, NULL, true,
                         EXTENDED_STARTUPINFO_PRESENT | (hide ? CREATE_NO_WINDOW : 0),
                         NULL, NULL, &startup.StartupInfo, &process)) goto failed;
    CloseHandle(process.hThread);
    /* Parent handles stay valid until the child has finished reading. */
    ok = WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process.hProcess, exit_code);
    *error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process.hProcess);
    goto cleanup;
  failed:
    *error = GetLastError();
  cleanup:
    if (attributes_initialized) DeleteProcThreadAttributeList(attributes);
    sfree(attributes); sfree(command);
    if (view) { smemclr(view, serialized->len); UnmapViewOfFile(view); }
    if (inherited) CloseHandle(inherited);
    if (mapping) CloseHandle(mapping);
    for (size_t i = 0; i < lenof(streams); i++) if (streams[i]) CloseHandle(streams[i]);
    smemclr(serialized->s, serialized->len); strbuf_free(serialized);
    sfree(path);
    return ok;
}

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
