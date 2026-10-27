/* Windows integration harness for the actual WFX DLL and a real SSH server.
 *
 * Usage: wfx-tests.exe FULL_PLUGIN_PATH LOCAL_SCRATCH [SESSION_WFX_PATH]
 *                     [LEGACY_FLAT_LABEL]
 * The plugin's adjacent INI must already select a KiTTY-ng portable directory
 * containing an authenticated TestServer session (or SESSION_WFX_PATH). Its
 * RemoteRoot must be a dedicated disposable test root. The harness creates a
 * unique child directory remotely and locally and confines all mutation there.
 * It never asks for credentials, accepts host keys, or changes configuration.
 *
 * Compile with a matching Windows C compiler, -municode, and no plugin sources.
 * All plugin operations are invoked through GetProcAddress on the built DLL.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/wfx.h"

#define PATH_CAP 4096
#define DATA_A_SIZE 300123u
#define DATA_B_SIZE 262183u
#define DATA_LARGE_SIZE (8u * 1024u * 1024u + 17u)
#define DATA_A_SEED 17u
#define DATA_B_SEED 201u

typedef int (WINAPI *InitFn)(int, tProgressProc, tLogProc, tRequestProc);
typedef int (WINAPI *InitWFn)(int, tProgressProcW, tLogProcW, tRequestProcW);
typedef HANDLE (WINAPI *FirstWFn)(wchar_t *, WIN32_FIND_DATAW *);
typedef BOOL (WINAPI *NextWFn)(HANDLE, WIN32_FIND_DATAW *);
typedef int (WINAPI *CloseFn)(HANDLE);
typedef int (WINAPI *GetWFn)(wchar_t *, wchar_t *, int, RemoteInfoStruct *);
typedef int (WINAPI *PutWFn)(wchar_t *, wchar_t *, int);
typedef int (WINAPI *RenameWFn)(wchar_t *, wchar_t *, BOOL, BOOL, RemoteInfoStruct *);
typedef BOOL (WINAPI *PathWFn)(wchar_t *);
typedef int (WINAPI *ExecuteWFn)(HWND, wchar_t *, wchar_t *);
typedef BOOL (WINAPI *TimeWFn)(wchar_t *, FILETIME *, FILETIME *, FILETIME *);
typedef void (WINAPI *RootFn)(char *, int);
typedef void (WINAPI *ParamsFn)(FsDefaultParamStruct *);

typedef struct Api {
    InitFn init;
    InitWFn initw;
    FirstWFn first;
    NextWFn next;
    CloseFn close;
    GetWFn get;
    PutWFn put;
    RenameWFn rename;
    PathWFn mkdir, remove, rmdir, disconnect;
    ExecuteWFn execute;
    TimeWFn settime;
    RootFn root;
    ParamsFn params;
} Api;

static Api api;
static unsigned checks, failures, connect_messages;
static unsigned progress_zero, progress_hundred;
static int cancel_mode, cancelled_percent = -1;

static int check(int condition, const char *description)
{
    ++checks;
    if (!condition) ++failures;
    printf("%s %s\n", condition ? "PASS" : "FAIL", description);
    fflush(stdout);
    return condition;
}

static int WINAPI progress_cb(int plugin, wchar_t *source, wchar_t *target, int percent)
{
    (void)plugin; (void)source; (void)target;
    if (percent == 0) ++progress_zero;
    if (percent == 100) ++progress_hundred;
    if (cancel_mode == 1 || (cancel_mode == 2 && percent > 0)) {
        cancelled_percent = percent;
        return 1;
    }
    /* During cancellation tests, make the first chunk's next progress poll
     * observable even on localhost. This is test callback latency only. */
    if (cancel_mode == 2 && percent == 0) Sleep(110);
    return 0;
}

static void WINAPI log_cb(int plugin, int type, wchar_t *message)
{
    char text[12288];
    (void)plugin;
    if (type == MSGTYPE_CONNECT) ++connect_messages;
    if (!WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof(text), NULL, NULL))
        strcpy(text, "[log conversion failed]");
    printf("LOG %d %s\n", type, text);
    fflush(stdout);
}

static BOOL WINAPI request_cb(int plugin, int type, wchar_t *title,
                              wchar_t *text, wchar_t *result, int length)
{
    (void)plugin; (void)type; (void)title; (void)text;
    if (result && length) result[0] = 0;
    return FALSE;
}

static int load_symbol(HMODULE dll, void *destination, size_t size, const char *name)
{
    FARPROC p = GetProcAddress(dll, name);
    if (!p || size != sizeof(p)) {
        fprintf(stderr, "Required export missing: %s\n", name);
        return 0;
    }
    memcpy(destination, &p, size);
    return 1;
}

#define LOAD(field, name) do { if (!load_symbol(dll, &api.field, sizeof(api.field), name)) goto cleanup; } while (0)

static int join_path(wchar_t out[PATH_CAP], const wchar_t *a, const wchar_t *b)
{
    int n = swprintf(out, PATH_CAP, L"%ls\\%ls", a, b);
    if (n < 0 || n >= PATH_CAP) { out[0] = 0; return 0; }
    return 1;
}

static int long_local(const wchar_t *path, wchar_t out[PATH_CAP + 16])
{
    wchar_t absolute[PATH_CAP];
    DWORD n;
    if (!wcsncmp(path, L"\\\\?\\", 4)) {
        if (wcslen(path) >= PATH_CAP + 16) return 0;
        wcscpy(out, path); return 1;
    }
    n = GetFullPathNameW(path, PATH_CAP, absolute, NULL);
    if (!n || n >= PATH_CAP) return 0;
    if (!wcsncmp(absolute, L"\\\\", 2))
        return swprintf(out, PATH_CAP + 16, L"\\\\?\\UNC\\%ls", absolute + 2) >= 0;
    return swprintf(out, PATH_CAP + 16, L"\\\\?\\%ls", absolute) >= 0;
}

static unsigned char pattern(uint64_t i, unsigned seed)
{
    return (unsigned char)(i * 131u + (i >> 8) + seed);
}

static int create_data(const wchar_t *path, size_t size, unsigned seed)
{
    wchar_t full[PATH_CAP + 16];
    HANDLE h;
    unsigned char data[65536];
    size_t offset = 0;
    int ok = 1;
    if (!long_local(path, full)) return 0;
    h = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    while (offset < size) {
        DWORD done, n = (DWORD)(size - offset > sizeof(data) ? sizeof(data) : size - offset);
        size_t i;
        for (i = 0; i < n; ++i) data[i] = pattern(offset + i, seed);
        if (!WriteFile(h, data, n, &done, NULL) || done != n) { ok = 0; break; }
        offset += n;
    }
    CloseHandle(h);
    return ok;
}

static int matches_data(const wchar_t *path, size_t size, unsigned seed)
{
    wchar_t full[PATH_CAP + 16];
    HANDLE h;
    LARGE_INTEGER actual;
    unsigned char data[65536];
    size_t offset = 0;
    int ok = 1;
    if (!long_local(path, full)) return 0;
    h = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(h, &actual) || actual.QuadPart != (LONGLONG)size) ok = 0;
    while (ok && offset < size) {
        DWORD got, n = (DWORD)(size - offset > sizeof(data) ? sizeof(data) : size - offset);
        size_t i;
        if (!ReadFile(h, data, n, &got, NULL) || got != n) { ok = 0; break; }
        for (i = 0; i < got; ++i) if (data[i] != pattern(offset + i, seed)) { ok = 0; break; }
        offset += got;
    }
    CloseHandle(h);
    return ok;
}

static int delete_local(const wchar_t *path)
{
    wchar_t full[PATH_CAP + 16];
    return long_local(path, full) && DeleteFileW(full);
}

/* Return 1 when found, 0 when absent, -1 on an enumeration error. */
static int find_entry(const wchar_t *directory, const wchar_t *name,
                      WIN32_FIND_DATAW *found)
{
    WIN32_FIND_DATAW data;
    HANDLE h = api.first((wchar_t *)directory, &data);
    unsigned n = 0;
    int result = 0;
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_NO_MORE_FILES ? 0 : -1;
    do {
        if (!wcscmp(data.cFileName, name)) {
            if (found) *found = data;
            result = 1;
            break;
        }
        if (++n > 100000) { result = -1; break; }
    } while (api.next(h, &data));
    api.close(h);
    return result;
}

static int empty_directory(const wchar_t *directory)
{
    WIN32_FIND_DATAW data;
    HANDLE h;
    DWORD error;
    SetLastError(ERROR_SUCCESS);
    h = api.first((wchar_t *)directory, &data);
    error = GetLastError();
    if (h != INVALID_HANDLE_VALUE) api.close(h);
    return h == INVALID_HANDLE_VALUE && error == ERROR_NO_MORE_FILES;
}

/* Bounded cleanup is called only for the new directory made by this harness. */
static int cleanup_remote(const wchar_t *directory, unsigned depth)
{
    WIN32_FIND_DATAW data;
    HANDLE h;
    unsigned count = 0;
    int ok = 1;
    if (depth > 8) return 0;
    h = api.first((wchar_t *)directory, &data);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t child[PATH_CAP];
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
            if (wcschr(data.cFileName, L'\\') || wcschr(data.cFileName, L'/') ||
                ++count > 1000 || !join_path(child, directory, data.cFileName)) { ok = 0; break; }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!cleanup_remote(child, depth + 1)) ok = 0;
            } else if (!api.remove(child)) ok = 0;
        } while (api.next(h, &data));
        api.close(h);
    } else if (GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
    if (!api.rmdir((wchar_t *)directory)) ok = 0;
    return ok;
}

static void cleanup_local(const wchar_t *directory)
{
    wchar_t mask[PATH_CAP], full[PATH_CAP + 16];
    WIN32_FIND_DATAW data;
    HANDLE h;
    if (!join_path(mask, directory, L"*") || !long_local(mask, full)) return;
    h = FindFirstFileW(full, &data);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t child[PATH_CAP];
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && join_path(child, directory, data.cFileName))
                delete_local(child);
        } while (FindNextFileW(h, &data));
        FindClose(h);
    }
    if (long_local(directory, full)) RemoveDirectoryW(full);
}

static FILETIME unix_filetime(uint32_t seconds)
{
    ULARGE_INTEGER n;
    FILETIME result;
    n.QuadPart = ((uint64_t)seconds + UINT64_C(11644473600)) * UINT64_C(10000000);
    result.dwLowDateTime = n.LowPart; result.dwHighDateTime = n.HighPart;
    return result;
}

/* Traverse only virtual groups and the session entry, without opening the
 * session itself. Accepts a canonical relative WFX path such as
 * Lab\Nested\TestServer. Intermediate groups are directories, while the
 * final session is a file-like shortcut. All these calls must stay offline. */
static int visible_session_path(const wchar_t *session)
{
    wchar_t parent[PATH_CAP] = L"\\", child[MAX_PATH];
    const wchar_t *p = session;
    WIN32_FIND_DATAW data;
    while (*p) {
        const wchar_t *end = wcschr(p, L'\\');
        size_t count;
        if (!end) end = p + wcslen(p);
        count = (size_t)(end - p);
        if (!count || count >= MAX_PATH) return 0;
        memcpy(child,p,count*sizeof(wchar_t)); child[count]=0;
        if (find_entry(parent,child,&data) != 1) return 0;
        if (!*end) return data.dwFileAttributes == FILE_ATTRIBUTE_NORMAL;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
        if (wcslen(parent) + count + 2 >= PATH_CAP) return 0;
        if (wcslen(parent) > 1) wcscat(parent,L"\\");
        wcscat(parent,child);
        p=end+1;
    }
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    HMODULE dll = NULL;
    const wchar_t *session = argc > 3 ? argv[3] : L"TestServer";
    const wchar_t *remote_filename = L"данные 100%20 %.bin";
    const wchar_t *encoded_filename = L"~kg~6261643A6E616D65"; /* bad:name */
    wchar_t session_root[PATH_CAP], remote_dir[PATH_CAP] = L"", local_dir[PATH_CAP] = L"";
    wchar_t unique[128], src_a[PATH_CAP], src_b[PATH_CAP], src_large[PATH_CAP];
    wchar_t dst[PATH_CAP], sentinel[PATH_CAP], long_dst[PATH_CAP], move_source[PATH_CAP], move_dest[PATH_CAP];
    wchar_t local_reserved[PATH_CAP], reserved_check[PATH_CAP], remote_reserved[PATH_CAP];
    wchar_t remote_file[PATH_CAP], remote_encoded[PATH_CAP], remote_renamed[PATH_CAP];
    wchar_t remote_empty[PATH_CAP], remote_move[PATH_CAP], remote_cancel[PATH_CAP], cross[PATH_CAP];
    wchar_t session_parent[PATH_CAP], legacy_root[PATH_CAP];
    wchar_t opened_root[PATH_CAP], redirect_target[PATH_CAP];
    wchar_t long_basename[250];
    WIN32_FIND_DATAW data;
    RemoteInfoStruct info = {0};
    FsDefaultParamStruct params = {0};
    FILETIME changed;
    unsigned before, i;
    int initialized = 0, remote_created = 0, local_created = 0, result, success = 0;
    char label[64];
    if (argc < 3) {
        fprintf(stderr, "Usage: wfx-tests.exe FULL_PLUGIN_PATH LOCAL_SCRATCH [SESSION_WFX_PATH] [LEGACY_FLAT_LABEL]\n");
        return 2;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetConsoleOutputCP(CP_UTF8);
    if (!*session || wcslen(session) > 1023 || *session == L'\\' ||
        session[wcslen(session)-1] == L'\\' || wcschr(session, L'/')) return 2;
    swprintf(session_root, PATH_CAP, L"\\%ls", session);
    wcscpy(session_parent,session_root);
    {
        wchar_t *last=wcsrchr(session_parent,L'\\');
        if (last) *last=0;
    }
    dll = LoadLibraryExW(argv[1], NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!check(dll != NULL, "load actual plugin DLL")) {
        fprintf(stderr, "LoadLibraryExW error: %lu\n", (unsigned long)GetLastError());
        goto cleanup;
    }
    LOAD(init, "FsInit"); LOAD(initw, "FsInitW"); LOAD(first, "FsFindFirstW");
    LOAD(next, "FsFindNextW"); LOAD(close, "FsFindClose"); LOAD(get, "FsGetFileW");
    LOAD(put, "FsPutFileW"); LOAD(rename, "FsRenMovFileW"); LOAD(mkdir, "FsMkDirW");
    LOAD(remove, "FsDeleteFileW"); LOAD(rmdir, "FsRemoveDirW"); LOAD(disconnect, "FsDisconnectW");
    LOAD(execute, "FsExecuteFileW"); LOAD(settime, "FsSetTimeW"); LOAD(root, "FsGetDefRootName");
    LOAD(params, "FsSetDefaultParams");
    check(sizeof(RemoteInfoStruct) == 20 && sizeof(FsDefaultParamStruct) == 272 &&
          sizeof(WIN32_FIND_DATAW) == 592, "SDK structure sizes");
    memset(label, 0xa5, sizeof(label));
    api.root(label, 5);
    check(label[4] == 0 && (unsigned char)label[5] == 0xa5, "root name before init obeys supplied buffer size");
    check(api.init(37, NULL, NULL, NULL) == 0, "ANSI initializer export");
    check(api.initw(37, progress_cb, log_cb, request_cb) == 0, "Unicode initializer and callbacks");
    initialized = 1;
    params.size = sizeof(params); params.PluginInterfaceVersionHi = 2; params.PluginInterfaceVersionLow = 1;
    api.params(&params);
    before=connect_messages;
    if (!check(visible_session_path(session) && connect_messages == before,
               "portable SSH shortcut reachable through directory groups without connecting")) goto cleanup;
    wcscpy(opened_root,session_root);
    swprintf(redirect_target,PATH_CAP,L"%ls\\",session_root);
    if (!check(api.execute(NULL,opened_root,L"open") == FS_EXEC_SYMLINK &&
               !wcscmp(opened_root,redirect_target) && connect_messages == before,
               "Enter redirects the SSH shortcut to its canonical directory before connecting")) goto cleanup;
    if (argc > 4 && *argv[4] && wcslen(argv[4]) < 255) {
        swprintf(legacy_root,PATH_CAP,L"\\%ls",argv[4]);
        check(api.execute(NULL,legacy_root,L"open") == FS_EXEC_SYMLINK &&
              !wcscmp(legacy_root,redirect_target) && connect_messages == before,
              "legacy flat shortcut redirects to the canonical directory without connecting");
    }
    before = connect_messages;
    result = find_entry(opened_root, L"__no_such_entry_expected__", NULL);
    if (!check(result >= 0, "follow shortcut target and enumerate configured session root")) goto cleanup;
    check(connect_messages > before, "connection reported through WFX log callback");
    if (argc > 4 && *argv[4] && wcslen(argv[4]) < 255) {
        swprintf(legacy_root,PATH_CAP,L"\\%ls",argv[4]);
        before=connect_messages;
        check(find_entry(legacy_root,L"__no_such_entry_expected__",NULL) >= 0 && connect_messages == before,
              "legacy flat bookmark resolves to the same existing SSH connection");
    }
    swprintf(unique, 128, L"wfx-test-%lu-%llu", (unsigned long)GetCurrentProcessId(),
             (unsigned long long)GetTickCount64());
    if (!join_path(remote_dir, session_root, unique) || !join_path(local_dir, argv[2], unique)) goto cleanup;
    if (!check(CreateDirectoryW(local_dir, NULL), "create isolated local test directory")) goto cleanup;
    local_created = 1;
    if (!check(api.mkdir(remote_dir), "create isolated remote test directory")) goto cleanup;
    remote_created = 1;
    check(empty_directory(remote_dir), "empty remote directory returns ERROR_NO_MORE_FILES");
    if (!join_path(src_a, local_dir, L"исходник 100%20 %.bin") ||
        !join_path(src_b, local_dir, L"replacement.bin") ||
        !join_path(src_large, local_dir, L"large.bin") ||
        !join_path(dst, local_dir, L"download.bin") ||
        !join_path(sentinel, local_dir, L"sentinel.bin") ||
        !join_path(move_source, local_dir, L"move-source.bin") ||
        !join_path(move_dest, local_dir, L"move-downloaded.bin") ||
        !join_path(local_reserved, local_dir, L"~kg~3A") ||
        !join_path(reserved_check, local_dir, L"reserved-alias-check.bin") ||
        !join_path(remote_reserved, remote_dir, L"~kg~3A") ||
        !join_path(remote_file, remote_dir, remote_filename) ||
        !join_path(remote_encoded, remote_dir, encoded_filename) ||
        !join_path(remote_renamed, remote_dir, L"renamed.bin") ||
        !join_path(remote_empty, remote_dir, L"empty") ||
        !join_path(remote_move, remote_dir, L"move.bin") ||
        !join_path(remote_cancel, remote_dir, L"cancelled.bin")) goto cleanup;
    if (!check(create_data(src_a, DATA_A_SIZE, DATA_A_SEED) &&
               create_data(src_b, DATA_B_SIZE, DATA_B_SEED) &&
               create_data(src_large, DATA_LARGE_SIZE, 93) &&
               create_data(local_reserved, 147, 39), "create deterministic binary transfer fixtures")) goto cleanup;
    check(api.mkdir(remote_empty), "mkdir child directory");
    check(empty_directory(remote_empty), "enumerate empty child directory");
    if (!check(api.put(src_a, remote_file, 0) == FS_FILE_OK, "upload >128 KiB with Unicode and literal percent name")) goto cleanup;
    check(find_entry(remote_dir, remote_filename, &data) == 1 && data.nFileSizeLow == DATA_A_SIZE &&
          data.nFileSizeHigh == 0, "listing preserves literal percent sequences, Unicode and file size");
    check(api.get(remote_file, dst, 0, &info) == FS_FILE_OK && matches_data(dst, DATA_A_SIZE, DATA_A_SEED),
          "download exact original bytes");
    delete_local(dst);
    check(api.put(src_b, remote_file, 0) == FS_FILE_EXISTS, "upload without overwrite reports existing destination");
    check(api.get(remote_file, dst, 0, &info) == FS_FILE_OK && matches_data(dst, DATA_A_SIZE, DATA_A_SEED),
          "refused upload preserved original remote content");
    delete_local(dst);
    check(api.put(src_b, remote_file, FS_COPYFLAGS_OVERWRITE) == FS_FILE_OK, "atomic overwrite upload");
    check(api.get(remote_file, dst, 0, &info) == FS_FILE_OK && matches_data(dst, DATA_B_SIZE, DATA_B_SEED),
          "download exact replacement bytes");
    check(create_data(sentinel, 123, 55), "create existing local destination fixture");
    check(api.get(remote_file, sentinel, 0, &info) == FS_FILE_EXISTS && matches_data(sentinel, 123, 55),
          "download without overwrite preserves existing local destination");
    check(api.get(remote_file, sentinel, FS_COPYFLAGS_OVERWRITE, &info) == FS_FILE_OK &&
          matches_data(sentinel, DATA_B_SIZE, DATA_B_SEED), "download overwrite replaces exact local bytes");
    for (i = 0; i < 240; ++i) long_basename[i] = L'L';
    wcscpy(long_basename + 240, L".bin");
    if (join_path(long_dst, local_dir, long_basename))
        check(api.get(remote_file, long_dst, 0, &info) == FS_FILE_OK && matches_data(long_dst, DATA_B_SIZE, DATA_B_SEED),
              "download supports a 244-character Windows basename");
    check(api.put(src_a, remote_encoded, 0) == FS_FILE_OK &&
          find_entry(remote_dir, encoded_filename, NULL) == 1, "escaped incompatible remote filename round trip");
    /* A literal local reserved-looking name must never silently address ':'.
     * Cover both absence and an existing ':' created by an explicit target. */
    result = api.put(local_reserved, remote_reserved, 0) == FS_FILE_NOTSUPPORTED &&
             api.put(local_reserved, remote_reserved, FS_COPYFLAGS_OVERWRITE) == FS_FILE_NOTSUPPORTED &&
             find_entry(remote_dir, L"~kg~3A", NULL) == 0;
    if (result) result = api.put(src_a, remote_reserved, 0) == FS_FILE_OK;
    if (result) {
        result = api.put(local_reserved, remote_reserved, FS_COPYFLAGS_OVERWRITE) == FS_FILE_NOTSUPPORTED &&
                 api.get(remote_reserved, reserved_check, 0, &info) == FS_FILE_OK &&
                 matches_data(reserved_check, DATA_A_SIZE, DATA_A_SEED) &&
                 matches_data(local_reserved, 147, 39);
    }
    if (find_entry(remote_dir, L"~kg~3A", NULL) == 1 && !api.remove(remote_reserved)) result = 0;
    check(result, "literal local reserved alias cannot create or overwrite ':' implicitly");
    check(api.put(src_b, remote_encoded, FS_COPYFLAGS_RESUME) == FS_FILE_NOTSUPPORTED,
          "unsupported upload resume rejected");
    check(api.get(remote_file, dst, FS_COPYFLAGS_RESUME, &info) == FS_FILE_NOTSUPPORTED &&
          matches_data(dst, DATA_B_SIZE, DATA_B_SEED), "unsupported download resume preserves destination");
    progress_zero = progress_hundred = 0;
    check(api.rename(remote_file, remote_renamed, TRUE, FALSE, &info) == FS_FILE_OK,
          "same-session rename");
    check(progress_zero && progress_hundred, "rename reports beginning and end progress");
    check(find_entry(remote_dir, remote_filename, NULL) == 0 &&
          find_entry(remote_dir, L"renamed.bin", NULL) == 1, "rename changed directory entries");
    check(api.rename(remote_renamed, remote_encoded, TRUE, FALSE, &info) == FS_FILE_EXISTS,
          "rename refuses to overwrite existing remote file");
    before = connect_messages;
    /* The nested v1 fixture supplies an OtherServer profile beside this
     * session. Sharing a parent group must never be mistaken for sharing a
     * connection, and no transport for OtherServer may be opened. */
    swprintf(cross, PATH_CAP, L"%ls\\OtherServer\\%ls\\unreachable.bin", session_parent, unique);
    check(api.rename(remote_renamed, cross, TRUE, FALSE, &info) == FS_FILE_NOTSUPPORTED &&
          connect_messages == before, "different session under the same group rejected without another connection");
    check(api.rename(remote_renamed, remote_file, FALSE, FALSE, &info) == FS_FILE_NOTSUPPORTED,
          "unsupported remote copy rejected");
    check(api.execute(NULL, remote_renamed, L"chmod 640") == FS_EXEC_OK,
          "Unix chmod through FsExecuteFileW");
    check(find_entry(remote_dir, L"renamed.bin", &data) == 1 && (data.dwReserved0 & 0777u) == 0640u,
          "Unix permission bits reported through WFX enumeration");
    changed = unix_filetime(UINT32_C(1700000000));
    check(api.settime(remote_renamed, NULL, NULL, &changed), "set remote modification time with nullable other times");
    check(find_entry(remote_dir, L"renamed.bin", &data) == 1 &&
          !CompareFileTime(&data.ftLastWriteTime, &changed), "remote timestamp round trip");
    check(api.execute(NULL, remote_renamed, L"open") == FS_EXEC_YOURSELF,
          "ordinary file open delegates download and opening to Total Commander");
    cancel_mode = 1; cancelled_percent = -1;
    result = api.put(src_a, remote_cancel, 0);
    cancel_mode = 0;
    check(result == FS_FILE_USERABORT && cancelled_percent == 0,
          "early upload cancellation returns USERABORT");
    check(find_entry(remote_dir, L"cancelled.bin", NULL) == 0 && matches_data(src_a, DATA_A_SIZE, DATA_A_SEED),
          "early cancellation created no destination and preserved source");
    cancel_mode = 2; cancelled_percent = -1;
    result = api.put(src_large, remote_renamed, FS_COPYFLAGS_OVERWRITE);
    cancel_mode = 0;
    check(result == FS_FILE_USERABORT && cancelled_percent > 0 && cancelled_percent < 100,
          "upload cancellation during data transfer returns USERABORT");
    delete_local(dst);
    check(api.get(remote_renamed, dst, 0, &info) == FS_FILE_OK && matches_data(dst, DATA_B_SIZE, DATA_B_SEED),
          "cancelled overwrite preserved previous remote content and reconnects");
    cancel_mode = 2; cancelled_percent = -1;
    result = api.get(remote_renamed, sentinel, FS_COPYFLAGS_OVERWRITE, &info);
    cancel_mode = 0;
    check(result == FS_FILE_USERABORT && cancelled_percent > 0 && cancelled_percent < 100,
          "download cancellation during data transfer returns USERABORT");
    check(matches_data(sentinel, DATA_B_SIZE, DATA_B_SEED), "cancelled download preserved previous local content");
    check(create_data(move_source, 65539, 71), "create MOVE source fixture");
    check(api.put(move_source, remote_move, FS_COPYFLAGS_MOVE) == FS_FILE_OK &&
          GetFileAttributesW(move_source) == INVALID_FILE_ATTRIBUTES,
          "upload MOVE deletes local source only after successful commit");
    check(api.get(remote_move, move_dest, FS_COPYFLAGS_MOVE, &info) == FS_FILE_OK &&
          matches_data(move_dest, 65539, 71) && find_entry(remote_dir, L"move.bin", NULL) == 0,
          "download MOVE preserves bytes and removes remote source");
    check(api.remove(remote_encoded) && find_entry(remote_dir, encoded_filename, NULL) == 0,
          "delete escaped-name file");
    check(api.remove(remote_renamed) && find_entry(remote_dir, L"renamed.bin", NULL) == 0,
          "delete normal remote file");
    check(api.rmdir(remote_empty) && find_entry(remote_dir, L"empty", NULL) == 0,
          "remove empty remote directory");
    success = 1;
cleanup:
    cancel_mode = 0;
    if (remote_created) check(cleanup_remote(remote_dir, 0), "remove only the isolated remote test directory and leftovers");
    if (initialized) {
        check(api.disconnect(session_root), "disconnect active session");
        check(api.disconnect(session_root), "disconnect already-closed session is idempotent");
        if (success)
            check(find_entry(session_root, L"__no_such_entry_expected__", NULL) >= 0,
                  "reconnect before unloading DLL with an active SSH process");
    }
    if (local_created) cleanup_local(local_dir);
    if (dll) {
        ULONGLONG started = GetTickCount64();
        check(FreeLibrary(dll), "unload actual plugin DLL");
        check(GetTickCount64() - started < 2000, "DLL detach with an active connection remains bounded");
    }
    printf("RESULT %u/%u checks passed; %u failed.\n", checks - failures, checks, failures);
    return success && !failures ? 0 : 1;
}
