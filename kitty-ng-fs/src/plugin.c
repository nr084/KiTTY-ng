#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "wfx.h"
#include "resource.h"
#include "metadata.h"
#include "sftp.h"
#include "profiles.h"
#include "session_tree.h"
#include "transport.h"
#include "pathutil.h"

#define KG_PATH 4096
#define KG_MAX_CONNECTIONS 16
#define KG_MAX_ENTRIES 100000
#define KG_UNIX_ATTR UINT32_C(0x80000000)
#define KG_S_IFMT 0170000u
#define KG_S_IFDIR 0040000u
#define KG_S_IFLNK 0120000u
#define KG_FIND_MAGIC UINT32_C(0x4b474653)
static const wchar_t settings_name[] = L"[Настройки KiTTY-ng]";

typedef struct {
    wchar_t stored_root[KG_PATH], root[KG_PATH], remote_root[1024];
    DWORD timeout_ms;
} Config;
typedef struct {
    wchar_t *session_root, *load_name;
    KgTransport *transport;
    SfClient sf;
    char *base;
    ULONGLONG used;
} Connection;
typedef struct {
    uint32_t magic;
    WIN32_FIND_DATAW *items;
    size_t count, next, capacity;
} FindList;
typedef struct {
    const wchar_t *source, *target;
    int percent, cancelled;
    ULONGLONG last_poll;
} Operation;

static HINSTANCE module_instance;
static CRITICAL_SECTION mutex;
static int in_call, initialized, plugin_number;
static tProgressProc progress_a;
static tProgressProcW progress_w;
static tLogProc log_a;
static tLogProcW log_w;
static tRequestProc request_a;
static tRequestProcW request_w;
static Config config;
static wchar_t module_dir[KG_PATH], ini_path[KG_PATH];
static KgProfiles profiles;
static KgTree session_tree;
/* Icon callbacks may run in TC's background thread. They see a complete
 * immutable cache and never wait for SSH or read profile files. */
static SRWLOCK profile_cache_lock = SRWLOCK_INIT;
static Connection connections[KG_MAX_CONNECTIONS];
static Operation operation;

static void log_message(int type, const wchar_t *message)
{
    if (log_w) log_w(plugin_number, type, (wchar_t *)message);
    else if (log_a) {
        char text[8192];
        if (WideCharToMultiByte(CP_ACP, 0, message, -1, text, sizeof(text), NULL, NULL))
            log_a(plugin_number, type, text);
    }
}

static int request_input(void *ctx, int kind, const wchar_t *title,
                         const wchar_t *text, wchar_t *result, size_t cap)
{
    (void)ctx;
    if (!result || !cap || cap > INT_MAX) return 0;
    result[0] = 0;
    if (request_w) return request_w(plugin_number, kind, (wchar_t *)title,
                                   (wchar_t *)text, result, (int)cap);
    if (request_a) {
        char heading[512], body[16384], answer[4096] = {0};
        if (!WideCharToMultiByte(CP_ACP, 0, title, -1, heading, sizeof(heading), NULL, NULL) ||
            !WideCharToMultiByte(CP_ACP, 0, text, -1, body, sizeof(body), NULL, NULL)) return 0;
        BOOL ok = request_a(plugin_number, kind, heading, body, answer, sizeof(answer));
        if (ok) ok = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, answer, -1, result, (int)cap) != 0;
        SecureZeroMemory(answer, sizeof(answer));
        return ok;
    }
    return 0;
}

static void connection_dialog(const wchar_t *text)
{
    if (request_w || request_a) {
        wchar_t ignored[2];
        request_input(NULL, 8, L"KiTTY-ng FS", text, ignored, 2);
    } else MessageBoxW(GetActiveWindow(), text, L"KiTTY-ng FS", MB_OK | MB_ICONERROR);
}

static void error_message(const wchar_t *message)
{
    log_message(MSGTYPE_IMPORTANTERROR, message);
}

static int poll_operation(void *unused)
{
    ULONGLONG now = GetTickCount64();
    int abort = 0;
    (void)unused;
    if (operation.cancelled) return 1;
    if (now - operation.last_poll < 80) return 0;
    operation.last_poll = now;
    if (progress_w)
        abort = progress_w(plugin_number, (wchar_t *)(operation.source ? operation.source : L""),
                           (wchar_t *)(operation.target ? operation.target : L""), operation.percent);
    else if (progress_a) {
        char source[KG_PATH], target[KG_PATH];
        source[0] = target[0] = 0;
        WideCharToMultiByte(CP_ACP, 0, operation.source ? operation.source : L"", -1, source, sizeof(source), NULL, NULL);
        WideCharToMultiByte(CP_ACP, 0, operation.target ? operation.target : L"", -1, target, sizeof(target), NULL, NULL);
        abort = progress_a(plugin_number, source, target, operation.percent);
    }
    if (abort) operation.cancelled = 1;
    return operation.cancelled;
}

static int progress_now(int percent)
{
    operation.percent = percent;
    operation.last_poll = 0;
    return !poll_operation(NULL);
}

static void begin_operation(const wchar_t *source, const wchar_t *target)
{
    memset(&operation, 0, sizeof(operation));
    operation.source = source; operation.target = target;
}

static void connection_close(Connection *c)
{
    if (c->transport) kg_transport_close(c->transport);
    free(c->session_root); free(c->load_name); free(c->base);
    memset(c, 0, sizeof(*c));
}

static void disconnect_all(void)
{
    size_t i;
    for (i = 0; i < KG_MAX_CONNECTIONS; ++i) connection_close(&connections[i]);
}

static int resolve_config_root(Config *cfg)
{
    wchar_t expanded[KG_PATH], joined[KG_PATH];
    DWORD n = ExpandEnvironmentStringsW(cfg->stored_root, expanded, KG_PATH);
    if (!n || n > KG_PATH || !expanded[0]) { cfg->root[0] = 0; return 0; }
    if (PathIsRelativeW(expanded)) {
        if (swprintf(joined, KG_PATH, L"%ls\\%ls", module_dir, expanded) < 0) return 0;
    } else wcscpy(joined, expanded);
    n = GetFullPathNameW(joined, KG_PATH, cfg->root, NULL);
    if (!n || n >= KG_PATH) { cfg->root[0] = 0; return 0; }
    return 1;
}

static void initialize(void)
{
    wchar_t *slash;
    DWORD n;
    if (initialized) return;
    initialized = 1;
    n = GetModuleFileNameW(module_instance, module_dir, KG_PATH);
    if (!n || n >= KG_PATH) { module_dir[0] = 0; return; }
    slash = wcsrchr(module_dir, L'\\');
    if (!slash) { module_dir[0] = 0; return; }
    *slash = 0;
    if (swprintf(ini_path, KG_PATH, L"%ls\\kittyngfs.ini", module_dir) < 0) { ini_path[0] = 0; return; }
    GetPrivateProfileStringW(L"KiTTYngFS", L"PortableDir", L"%COMMANDER_PATH%\\Tools\\KiTTY-ng",
                            config.stored_root, KG_PATH, ini_path);
    GetPrivateProfileStringW(L"KiTTYngFS", L"RemoteRoot", L"/", config.remote_root, 1024, ini_path);
    n = GetPrivateProfileIntW(L"KiTTYngFS", L"TimeoutSeconds", 60, ini_path);
    if (n < 5 || n > 600) n = 60;
    config.timeout_ms = n * 1000;
    if (!config.remote_root[0]) wcscpy(config.remote_root, L"/");
    resolve_config_root(&config);
}

static int enter_plugin(void)
{
    EnterCriticalSection(&mutex);
    if (in_call) { LeaveCriticalSection(&mutex); SetLastError(ERROR_BUSY); return 0; }
    in_call = 1;
    initialize();
    return 1;
}

static void leave_plugin(void)
{
    /* These paths borrow caller memory (including ANSI wrapper stack buffers).
     * They are only valid during this serialized operation. */
    operation.source = operation.target = NULL;
    in_call = 0; LeaveCriticalSection(&mutex);
}

static int refresh_profiles(int announce)
{
    KgProfiles fresh = {0};
    KgTree fresh_tree = {0};
    wchar_t error[1024] = L"";
    if (!config.root[0] || !kg_profiles_load(config.root, &fresh, error, 1024)) {
        if (announce) error_message(error[0] ? error : L"Укажите папку портативной KiTTY-ng в настройках плагина.");
        return 0;
    }
    if (error[0]) log_message(MSGTYPE_DETAILS, error);
    error[0] = 0;
    if (!kg_tree_build(&fresh, &fresh_tree, error, 1024)) {
        kg_profiles_free(&fresh);
        error_message(error[0] ? error : L"Не удалось построить группы KiTTY-ng.");
        return 0;
    }
    AcquireSRWLockExclusive(&profile_cache_lock);
    kg_tree_free(&session_tree); kg_profiles_free(&profiles);
    profiles = fresh; session_tree = fresh_tree;
    ReleaseSRWLockExclusive(&profile_cache_lock);
    return 1;
}

static void clear_profile_cache(void)
{
    AcquireSRWLockExclusive(&profile_cache_lock);
    kg_tree_free(&session_tree); kg_profiles_free(&profiles);
    ReleaseSRWLockExclusive(&profile_cache_lock);
}

/* Main operations hold mutex. Only this lookup may refresh local metadata;
 * FsExtractCustomIcon uses kg_tree_resolve directly under a shared cache lock. */
static int lookup_node(const wchar_t *path, KgTreeMatch *match)
{
    if (session_tree.count && kg_tree_resolve(&session_tree, &profiles, path, 1, match)) return 1;
    return refresh_profiles(1) && kg_tree_resolve(&session_tree, &profiles, path, 1, match);
}

static KgProfile *lookup_profile(const wchar_t *path, const wchar_t **relative)
{
    KgTreeMatch match;
    if (!lookup_node(path, &match) || match.kind != KG_TREE_SESSION) return NULL;
    if (relative) *relative = match.relative;
    return &profiles.items[match.profile_index];
}

static void connection_error(Connection *c, const wchar_t *action, int dialog)
{
    wchar_t text[4096];
    wchar_t *detail = kg_wide(c->sf.error);
    wchar_t *transport = c->transport ? kg_wide(kg_transport_error(c->transport)) : NULL;
    swprintf(text, 4096, L"%ls\n%ls\n%ls", action, detail ? detail : L"Ошибка SFTP",
             transport ? transport : L"");
    text[4095] = 0;
    error_message(text);
    if (dialog && !operation.cancelled) {
        size_t len = wcslen(text);
        const wchar_t help[] = L"\n\nПроверьте сеанс в KiTTY-ng. Ключ сервера и пароль запрашиваются самим плагином. Можно использовать сохранённый пароль, ключ PPK или Kageant. Команда terminal в строке TC открывает выбранный сеанс.";
        if (len + wcslen(help) < 4096) wcscat(text, help);
        connection_dialog(text);
    }
    free(detail); free(transport);
}

static Connection *get_connection(KgProfile *profile)
{
    size_t i, slot = KG_MAX_CONNECTIONS;
    ULONGLONG oldest = ULLONG_MAX;
    wchar_t error[1024], logline[KG_PATH];
    const wchar_t *root_path = session_tree.nodes[session_tree.profile_nodes[(size_t)(profile - profiles.items)]].path;
    char *root = NULL;
    Connection *c;
    for (i = 0; i < KG_MAX_CONNECTIONS; ++i) {
        c = &connections[i];
        if (c->load_name && !wcscmp(c->load_name, profile->load_name)) {
            if (c->sf.alive && kg_transport_alive(c->transport) && !wcscmp(c->session_root, root_path)) {
                c->used = GetTickCount64(); return c;
            }
            /* A changed hierarchy root is a new toolbar registration. Retire
             * the old transport before reconnecting under the new root, so
             * a later disconnect of the old entry can never leave it alive. */
            wcscpy(logline, c->session_root);
            connection_close(c);
            log_message(MSGTYPE_DISCONNECT, logline);
        }
        if (!c->transport) slot = i;
    }
    if (slot == KG_MAX_CONNECTIONS) {
        for (i = 0; i < KG_MAX_CONNECTIONS; ++i)
            if (connections[i].used < oldest) { oldest = connections[i].used; slot = i; }
        connection_close(&connections[slot]);
    }
    c = &connections[slot];
    c->session_root = kg_wdup(root_path); c->load_name = kg_wdup(profile->load_name);
    if (!c->session_root || !c->load_name || !(root = kg_utf8(config.remote_root))) goto fail;
    c->transport = kg_transport_start(config.root, profile->load_name, config.timeout_ms,
                                      poll_operation, NULL, error, 1024);
    if (!c->transport) {
        error_message(error);
        if (!operation.cancelled) connection_dialog(error);
        goto fail;
    }
    if (sf_init(&c->sf, kg_transport_io(c->transport)) != SF_OK ||
        sf_realpath(&c->sf, root, &c->base) != SF_OK) {
        connection_error(c, L"Не удалось открыть SFTP-сеанс.", 1); goto fail;
    }
    if (!c->base || c->base[0] != '/' || strlen(c->base) > 16384) {
        error_message(L"Сервер вернул неподдерживаемый начальный путь SFTP."); goto fail;
    }
    c->used = GetTickCount64();
    swprintf(logline, KG_PATH, L"CONNECT %ls", c->session_root);
    log_message(MSGTYPE_CONNECT, logline);
    free(root); return c;
fail:
    free(root); connection_close(c); return NULL;
}

static Connection *resolve_remote(const wchar_t *path, char **remote, int require_child)
{
    const wchar_t *relative;
    KgProfile *profile = lookup_profile(path, &relative);
    Connection *c;
    *remote = NULL;
    if (!profile || (require_child && !*relative)) {
        SetLastError(ERROR_PATH_NOT_FOUND); return NULL;
    }
    if (!(c = get_connection(profile))) { SetLastError(ERROR_CONNECTION_ABORTED); return NULL; }
    *remote = kg_remote_path(c->base, relative);
    if (!*remote) { SetLastError(ERROR_INVALID_NAME); return NULL; }
    return c;
}

static int root_path(const wchar_t *path)
{
    while (*path == L'\\') ++path;
    return !*path;
}

static int settings_path(const wchar_t *path)
{
    while (*path == L'\\') ++path;
    return !wcscmp(path, settings_name);
}

static void find_free(FindList *list)
{
    if (list) { free(list->items); list->magic = 0; free(list); }
}

static int find_add(FindList *list, const WIN32_FIND_DATAW *item)
{
    if (list->count >= KG_MAX_ENTRIES) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
    if (list->count == list->capacity) {
        size_t cap = list->capacity ? list->capacity * 2 : 64;
        WIN32_FIND_DATAW *items;
        if (cap > KG_MAX_ENTRIES) cap = KG_MAX_ENTRIES;
        items = realloc(list->items, cap * sizeof(*items));
        if (!items) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return 0; }
        list->items = items; list->capacity = cap;
    }
    list->items[list->count++] = *item;
    return 1;
}

static void fill_attrs(WIN32_FIND_DATAW *data, const SfAttrs *attrs)
{
    data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    if (attrs->flags & SF_ATTR_PERMISSIONS) {
        if ((attrs->permissions & KG_S_IFMT) == KG_S_IFDIR) data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        data->dwFileAttributes |= KG_UNIX_ATTR;
        data->dwReserved0 = attrs->permissions;
    }
    if (attrs->flags & SF_ATTR_SIZE) { data->nFileSizeLow = (DWORD)attrs->size; data->nFileSizeHigh = (DWORD)(attrs->size >> 32); }
    if (attrs->flags & SF_ATTR_ACMODTIME) {
        data->ftLastAccessTime = kg_filetime(attrs->atime); data->ftLastWriteTime = kg_filetime(attrs->mtime);
    } else { data->ftLastWriteTime.dwHighDateTime = UINT32_MAX; data->ftLastWriteTime.dwLowDateTime = UINT32_MAX - 1; }
}

static int find_group(FindList *list, size_t node_index)
{
    const KgTreeNode *group = &session_tree.nodes[node_index];
    size_t i;
    for (i = 0; i < group->child_count; ++i) {
        const KgTreeNode *child = &session_tree.nodes[group->children[i]];
        WIN32_FIND_DATAW item;
        memset(&item, 0, sizeof(item));
        /* TC sorts the returned records itself. Only local groups are
         * directories; SSH entry points are links opened by FsExecuteFile.
         * This keeps groups ahead of connections in the normal folder-first
         * view, independently of their names or our enumeration order. */
        item.dwFileAttributes = child->kind == KG_TREE_GROUP ?
                                FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        item.ftLastWriteTime.dwHighDateTime = UINT32_MAX;
        item.ftLastWriteTime.dwLowDateTime = UINT32_MAX - 1;
        wcscpy(item.cFileName, child->name);
        if (!find_add(list, &item)) return 0;
    }
    return 1;
}

static HANDLE find_first(const wchar_t *path, WIN32_FIND_DATAW *data)
{
    FindList *list = calloc(1, sizeof(*list));
    WIN32_FIND_DATAW item;
    Connection *c = NULL;
    char *remote = NULL;
    SfHandle directory;
    KgTreeMatch match;
    int opened = 0, status, skipped = 0;
    size_t i;
    DWORD error = ERROR_GEN_FAILURE;
    if (!list) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
    list->magic = KG_FIND_MAGIC;
    if (root_path(path)) {
        memset(&item, 0, sizeof(item));
        wcscpy(item.cFileName, settings_name); item.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (!find_add(list, &item)) { error = GetLastError(); goto fail; }
        if (refresh_profiles(0) && !find_group(list, 0)) { error = GetLastError(); goto fail; }
    } else {
        if (!lookup_node(path, &match)) { error = ERROR_PATH_NOT_FOUND; goto fail; }
        if (match.kind == KG_TREE_GROUP) {
            /* F2 inside a local group rereads its saved profiles. This path
             * intentionally never resolves an SSH connection. */
            if (!refresh_profiles(1) || !kg_tree_resolve(&session_tree, &profiles, path, 1, &match) ||
                match.kind != KG_TREE_GROUP) { error = ERROR_PATH_NOT_FOUND; goto fail; }
            if (!find_group(list, match.node_index)) { error = GetLastError(); goto fail; }
            goto list_complete;
        }
        begin_operation(path, L"");
        if (!progress_now(0)) { error = ERROR_CANCELLED; goto fail; }
        c = resolve_remote(path, &remote, 0);
        if (!c) { error = GetLastError(); goto fail; }
        if (sf_opendir(&c->sf, remote, &directory) != SF_OK) goto sftp_fail;
        opened = 1;
        for (;;) {
            SfEntry *entries = NULL;
            size_t count = 0;
            status = sf_readdir(&c->sf, &directory, &entries, &count);
            if (status == SF_EOF) break;
            if (status != SF_OK) goto sftp_fail;
            for (i = 0; i < count; ++i) {
                if (!strcmp(entries[i].name, ".") || !strcmp(entries[i].name, "..")) continue;
                memset(&item, 0, sizeof(item));
                if (!kg_display_name(entries[i].name, item.cFileName, MAX_PATH)) { ++skipped; continue; }
                fill_attrs(&item, &entries[i].attrs);
                if (!find_add(list, &item)) { sf_entries_free(entries, count); error = ERROR_NOT_ENOUGH_MEMORY; goto fail; }
            }
            sf_entries_free(entries, count);
            if (poll_operation(NULL)) { error = ERROR_CANCELLED; goto fail; }
        }
        opened = 0;
        if (sf_close(&c->sf, &directory) != SF_OK) goto sftp_fail;
        if (skipped) error_message(L"Некоторые имена превышают предел Total Commander и не показаны. Полный каталог доступен через SFTP-клиент KiTTY-ng.");
    }
list_complete:
    free(remote);
    if (!list->count) { find_free(list); SetLastError(ERROR_NO_MORE_FILES); return INVALID_HANDLE_VALUE; }
    *data = list->items[0]; list->next = 1; SetLastError(ERROR_SUCCESS); return (HANDLE)list;
sftp_fail:
    error = operation.cancelled ? ERROR_CANCELLED : c->sf.last_status == SF_FX_NO_SUCH_FILE ? ERROR_PATH_NOT_FOUND : ERROR_ACCESS_DENIED;
    connection_error(c, L"Не удалось прочитать каталог.", 0);
fail:
    if (opened && c && c->sf.alive) sf_close(&c->sf, &directory);
    free(remote); find_free(list); SetLastError(error); return INVALID_HANDLE_VALUE;
}

HANDLE WINAPI FsFindFirstW(wchar_t *path, WIN32_FIND_DATAW *data)
{
    HANDLE h;
    DWORD error;
    if (!enter_plugin()) return INVALID_HANDLE_VALUE;
    h = find_first(path, data); error = GetLastError();
    leave_plugin(); SetLastError(error); return h;
}

BOOL WINAPI FsFindNextW(HANDLE handle, WIN32_FIND_DATAW *data)
{
    FindList *list = (FindList *)handle;
    if (!list || handle == INVALID_HANDLE_VALUE || list->magic != KG_FIND_MAGIC) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
    if (list->next >= list->count) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
    *data = list->items[list->next++]; return TRUE;
}

int WINAPI FsFindClose(HANDLE handle)
{
    FindList *list = (FindList *)handle;
    if (list && handle != INVALID_HANDLE_VALUE && list->magic == KG_FIND_MAGIC) find_free(list);
    return 0;
}

static int transfer_percent(uint64_t done, uint64_t total)
{
    int p = total ? (int)(100.0 * (double)done / (double)total) : 0;
    return p > 99 ? 99 : p;
}

static int local_exists(const wchar_t *path, DWORD *attributes)
{
    DWORD error;
    *attributes = GetFileAttributesW(path);
    if (*attributes != INVALID_FILE_ATTRIBUTES) return 1;
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 0;
    return -1;
}

static wchar_t *local_temporary(const wchar_t *path)
{
    char random[33];
    wchar_t random_w[33];
    wchar_t *result;
    const wchar_t *slash = wcsrchr(path, L'\\');
    size_t n, i;
    if (!slash) return NULL;
    n = (size_t)(slash - path) + 1;
    if (n > 32600 || !kg_random_hex(random)) return NULL;
    for (i = 0; i < 33; ++i) random_w[i] = (wchar_t)random[i];
    result = malloc((n + 60) * sizeof(*result));
    if (result) {
        memcpy(result, path, n * sizeof(*result));
        swprintf(result + n, 60, L".kittyngfs-%ls.part", random_w);
    }
    return result;
}

static char *remote_temporary(const char *path)
{
    char random[33];
    const char *slash = strrchr(path, '/');
    char *result;
    size_t prefix;
    if (!slash || !kg_random_hex(random)) return NULL;
    prefix = (size_t)(slash - path) + 1;
    result = malloc(prefix + 64);
    if (result) { memcpy(result, path, prefix); snprintf(result + prefix, 64, ".kittyngfs-%s.part", random); }
    return result;
}

static void report_partial(const char *path)
{
    wchar_t *wide = kg_wide(path);
    wchar_t message[4096];
    swprintf(message, 4096, L"Передача не завершена. На сервере мог остаться временный файл: %ls", wide ? wide : L".kittyngfs-*.part");
    message[4095] = 0; error_message(message); free(wide);
}

int WINAPI FsGetFileW(wchar_t *remote_name, wchar_t *local_name, int flags, RemoteInfoStruct *info)
{
    Connection *c = NULL;
    char *remote = NULL;
    wchar_t *local = NULL, *temporary = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    SfHandle handle;
    SfAttrs attrs;
    unsigned char buffer[SF_MAX_DATA];
    uint64_t offset = 0;
    size_t got;
    DWORD attributes, written;
    int result = FS_FILE_READERROR, exists, opened = 0, committed = 0, temp_created = 0, status;
    (void)info;
    if (!enter_plugin()) return FS_FILE_NOTSUPPORTED;
    begin_operation(remote_name, local_name);
    if (flags & FS_COPYFLAGS_RESUME) { result = FS_FILE_NOTSUPPORTED; goto done; }
    if (!progress_now(0)) { result = FS_FILE_USERABORT; goto done; }
    local = kg_local_path(local_name);
    if (!local) { result = FS_FILE_WRITEERROR; goto done; }
    exists = local_exists(local, &attributes);
    if (exists < 0 || (exists && (attributes & FILE_ATTRIBUTE_DIRECTORY))) { result = FS_FILE_WRITEERROR; goto done; }
    if (exists && !(flags & FS_COPYFLAGS_OVERWRITE)) { result = FS_FILE_EXISTS; goto done; }
    c = resolve_remote(remote_name, &remote, 1);
    if (!c) goto done;
    if (sf_stat(&c->sf, remote, 1, &attrs) != SF_OK) {
        result = c->sf.last_status == SF_FX_NO_SUCH_FILE ? FS_FILE_NOTFOUND : FS_FILE_READERROR; goto remote_error;
    }
    if ((attrs.flags & SF_ATTR_PERMISSIONS) && (attrs.permissions & KG_S_IFMT) == KG_S_IFDIR) goto done;
    if (sf_open(&c->sf, remote, SF_FXF_READ, NULL, &handle) != SF_OK) goto remote_error;
    opened = 1;
    temporary = local_temporary(local);
    if (!temporary) { result = FS_FILE_WRITEERROR; goto done; }
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { result = FS_FILE_WRITEERROR; goto done; }
    temp_created = 1;
    for (;;) {
        status = sf_read(&c->sf, &handle, offset, buffer, sizeof(buffer), &got);
        if (status == SF_EOF) break;
        if (status != SF_OK) goto remote_error;
        if (!got || offset > UINT64_MAX - got) goto remote_error;
        if ((attrs.flags & SF_ATTR_SIZE) && offset + got > attrs.size) {
            error_message(L"Размер исходного файла изменился во время скачивания. Существующий локальный файл сохранён."); goto done;
        }
        if (!WriteFile(file, buffer, (DWORD)got, &written, NULL) || written != got) { result = FS_FILE_WRITEERROR; goto done; }
        offset += got;
        operation.percent = transfer_percent(offset, (attrs.flags & SF_ATTR_SIZE) ? attrs.size : 0);
        if (poll_operation(NULL)) { result = FS_FILE_USERABORT; goto done; }
    }
    opened = 0;
    if (sf_close(&c->sf, &handle) != SF_OK) goto remote_error;
    if ((attrs.flags & SF_ATTR_SIZE) && offset != attrs.size) {
        error_message(L"Файл изменился во время скачивания. Неполная копия не заменяет существующий файл."); goto done;
    }
    if (attrs.flags & SF_ATTR_ACMODTIME) {
        FILETIME access = kg_filetime(attrs.atime), write = kg_filetime(attrs.mtime);
        if (!SetFileTime(file, NULL, &access, &write)) { result = FS_FILE_WRITEERROR; goto done; }
    }
    if (!FlushFileBuffers(file)) { result = FS_FILE_WRITEERROR; goto done; }
    CloseHandle(file); file = INVALID_HANDLE_VALUE;
    if (!progress_now(100)) { result = FS_FILE_USERABORT; goto done; }
    if (!MoveFileExW(temporary, local, MOVEFILE_WRITE_THROUGH | ((flags & FS_COPYFLAGS_OVERWRITE) ? MOVEFILE_REPLACE_EXISTING : 0))) {
        result = (GetLastError() == ERROR_ALREADY_EXISTS || GetLastError() == ERROR_FILE_EXISTS) ? FS_FILE_EXISTS : FS_FILE_WRITEERROR;
        goto done;
    }
    committed = 1;
    if ((flags & FS_COPYFLAGS_MOVE) && sf_remove(&c->sf, remote) != SF_OK) {
        error_message(L"Файл скачан, но исходник на сервере удалить не удалось. Обе копии сохранены."); goto remote_error;
    }
    result = FS_FILE_OK;
    log_message(MSGTYPE_TRANSFERCOMPLETE, remote_name);
    goto done;
remote_error:
    if (c) connection_error(c, L"Ошибка скачивания файла.", 0);
done:
    if (operation.cancelled) result = FS_FILE_USERABORT;
    if (opened && c && c->sf.alive && !operation.cancelled) sf_close(&c->sf, &handle);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (temp_created && !committed) DeleteFileW(temporary);
    if (c && operation.cancelled) connection_close(c);
    free(remote); free(local); free(temporary);
    leave_plugin(); return result;
}

int WINAPI FsPutFileW(wchar_t *local_name, wchar_t *remote_name, int flags)
{
    Connection *c = NULL;
    char *remote = NULL, *temporary = NULL;
    wchar_t *local = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION local_info, end_info;
    SfAttrs existing, create_attrs = {0}, times = {0};
    SfHandle handle;
    unsigned char buffer[SF_MAX_DATA];
    uint64_t size, offset = 0;
    DWORD got;
    int result = FS_FILE_WRITEERROR, opened = 0, temp_created = 0, committed = 0, exists = 0;
    const wchar_t *local_leaf = wcsrchr(local_name, L'\\');
    const wchar_t *remote_leaf = wcsrchr(remote_name, L'\\');
    if (!enter_plugin()) return FS_FILE_NOTSUPPORTED;
    begin_operation(local_name, remote_name);
    local_leaf = local_leaf ? local_leaf + 1 : local_name;
    remote_leaf = remote_leaf ? remote_leaf + 1 : remote_name;
    /* The alias prefix is reserved. An implicit local import bearing the same
     * prefix is ambiguous (literal filename versus an encoded remote name).
     * Require an explicit differently named local source, never guess. */
    if (!wcsncmp(local_leaf, L"~kg~", 4) && !wcscmp(local_leaf, remote_leaf)) {
        error_message(L"Префикс ~kg~ зарезервирован для имён, несовместимых с Windows. Переименуйте локальную копию и явно укажите целевое имя в диалоге копирования.");
        result = FS_FILE_NOTSUPPORTED; goto done;
    }
    if (flags & FS_COPYFLAGS_RESUME) { result = FS_FILE_NOTSUPPORTED; goto done; }
    if (!progress_now(0)) { result = FS_FILE_USERABORT; goto done; }
    local = kg_local_path(local_name);
    if (!local) { result = FS_FILE_READERROR; goto done; }
    file = CreateFileW(local, GENERIC_READ | ((flags & FS_COPYFLAGS_MOVE) ? DELETE : 0), FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &local_info) ||
        (local_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { result = FS_FILE_READERROR; goto done; }
    size = ((uint64_t)local_info.nFileSizeHigh << 32) | local_info.nFileSizeLow;
    c = resolve_remote(remote_name, &remote, 1);
    if (!c) goto done;
    if (sf_stat(&c->sf, remote, 0, &existing) == SF_OK) exists = 1;
    else if (c->sf.last_status != SF_FX_NO_SUCH_FILE) goto remote_error;
    if (exists && !(flags & FS_COPYFLAGS_OVERWRITE)) { result = FS_FILE_EXISTS; goto done; }
    if (exists && (existing.flags & SF_ATTR_PERMISSIONS) && (existing.permissions & KG_S_IFMT) == KG_S_IFDIR) goto done;
    if (exists && !c->sf.posix_rename) {
        error_message(L"Сервер не поддерживает безопасную замену posix-rename@openssh.com. Загрузите файл под новым именем; существующий файл сохранён.");
        result = FS_FILE_NOTSUPPORTED; goto done;
    }
    temporary = remote_temporary(remote);
    if (!temporary) goto done;
    create_attrs.flags = SF_ATTR_PERMISSIONS;
    create_attrs.permissions = 0600;
    if (sf_open(&c->sf, temporary, SF_FXF_WRITE | SF_FXF_CREAT | SF_FXF_EXCL, &create_attrs, &handle) != SF_OK) goto remote_error;
    opened = temp_created = 1;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &got, NULL)) { result = FS_FILE_READERROR; goto done; }
        if (!got) break;
        if (offset > size || got > size - offset) { result = FS_FILE_READERROR; goto done; }
        if (sf_write(&c->sf, &handle, offset, buffer, got) != SF_OK) goto remote_error;
        offset += got;
        operation.percent = transfer_percent(offset, size);
        if (poll_operation(NULL)) { result = FS_FILE_USERABORT; goto done; }
    }
    if (offset != size || !GetFileInformationByHandle(file, &end_info) ||
        end_info.nFileSizeHigh != local_info.nFileSizeHigh || end_info.nFileSizeLow != local_info.nFileSizeLow ||
        CompareFileTime(&end_info.ftLastWriteTime, &local_info.ftLastWriteTime)) {
        error_message(L"Исходный локальный файл изменился во время загрузки. Файл на сервере сохранён.");
        result = FS_FILE_READERROR; goto done;
    }
    opened = 0;
    if (sf_close(&c->sf, &handle) != SF_OK) goto remote_error;
    /* Preserve the old file's rwx bits. New files deliberately default to 0600;
     * the temporary content is private until the transfer is complete. */
    times.flags = SF_ATTR_PERMISSIONS;
    times.permissions = exists && (existing.flags & SF_ATTR_PERMISSIONS) &&
                        (existing.permissions & KG_S_IFMT) != KG_S_IFLNK ? existing.permissions & 0777u : 0600u;
    if (kg_unix_time(&local_info.ftLastAccessTime, &times.atime) && kg_unix_time(&local_info.ftLastWriteTime, &times.mtime))
        times.flags |= SF_ATTR_ACMODTIME;
    if (sf_setstat(&c->sf, temporary, &times) != SF_OK) goto remote_error;
    if (!progress_now(100)) { result = FS_FILE_USERABORT; goto done; }
    /* A non-overwriting rename must remain non-overwriting even if a target
     * appears after the lstat above. Never implement replacement by unlink. */
    if (sf_rename(&c->sf, temporary, remote, exists && (flags & FS_COPYFLAGS_OVERWRITE)) != SF_OK) goto remote_error;
    committed = 1;
    if (flags & FS_COPYFLAGS_MOVE) {
        FILE_DISPOSITION_INFO disposition;
        disposition.DeleteFile = TRUE;
        if (!SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition))) {
            error_message(L"Файл загружен, но локальный исходник удалить не удалось. Обе копии сохранены.");
            result = FS_FILE_READERROR; goto done;
        }
    }
    CloseHandle(file); file = INVALID_HANDLE_VALUE;
    result = FS_FILE_OK; log_message(MSGTYPE_TRANSFERCOMPLETE, remote_name); goto done;
remote_error:
    if (c) connection_error(c, L"Ошибка загрузки файла.", 0);
done:
    if (operation.cancelled) result = FS_FILE_USERABORT;
    if (opened && c && c->sf.alive && !operation.cancelled) sf_close(&c->sf, &handle);
    if (temp_created && !committed && c) {
        if (operation.cancelled || !c->sf.alive || sf_remove(&c->sf, temporary) != SF_OK) report_partial(temporary);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (c && operation.cancelled) connection_close(c);
    free(remote); free(temporary); free(local);
    leave_plugin(); return result;
}

int WINAPI FsRenMovFileW(wchar_t *old_name, wchar_t *new_name, BOOL move, BOOL overwrite, RemoteInfoStruct *info)
{
    Connection *old_c;
    KgTreeMatch old_match, new_match;
    char *old_path = NULL, *new_path = NULL;
    SfAttrs attrs;
    int result = FS_FILE_WRITEERROR, exists, matched = 0, attempt;
    (void)info;
    if (!move) return FS_FILE_NOTSUPPORTED;
    if (!enter_plugin()) return FS_FILE_NOTSUPPORTED;
    begin_operation(old_name, new_name);
    if (!progress_now(0)) { result = FS_FILE_USERABORT; goto done; }
    /* Resolve both sides against one immutable metadata generation before
     * opening any transport. A common group is not a common SSH session. */
    for (attempt = 0; attempt < 2; ++attempt) {
        if (session_tree.count && kg_tree_resolve(&session_tree, &profiles, old_name, 1, &old_match) &&
            kg_tree_resolve(&session_tree, &profiles, new_name, 1, &new_match)) { matched = 1; break; }
        if (attempt || !refresh_profiles(1)) break;
    }
    if (!matched || old_match.kind != KG_TREE_SESSION || new_match.kind != KG_TREE_SESSION ||
        !*old_match.relative || !*new_match.relative || old_match.profile_index != new_match.profile_index) {
        result = FS_FILE_NOTSUPPORTED; goto done;
    }
    old_c = get_connection(&profiles.items[old_match.profile_index]);
    if (!old_c) goto done;
    old_path = kg_remote_path(old_c->base, old_match.relative);
    new_path = kg_remote_path(old_c->base, new_match.relative);
    if (!old_path || !new_path) goto done;
    if (sf_stat(&old_c->sf, old_path, 0, &attrs) != SF_OK) {
        result = old_c->sf.last_status == SF_FX_NO_SUCH_FILE ? FS_FILE_NOTFOUND : FS_FILE_READERROR; goto done;
    }
    if (!strcmp(old_path, new_path)) { result = FS_FILE_OK; goto done; }
    exists = sf_stat(&old_c->sf, new_path, 0, &attrs) == SF_OK;
    if (!exists && old_c->sf.last_status != SF_FX_NO_SUCH_FILE) goto done;
    if (exists && !overwrite) { result = FS_FILE_EXISTS; goto done; }
    if (exists && !old_c->sf.posix_rename) { result = FS_FILE_NOTSUPPORTED; goto done; }
    if (!progress_now(100)) { result = FS_FILE_USERABORT; goto done; }
    if (sf_rename(&old_c->sf, old_path, new_path, exists && overwrite) == SF_OK) result = FS_FILE_OK;
    else connection_error(old_c, L"Не удалось переименовать файл.", 0);
done:
    if (operation.cancelled) result = FS_FILE_USERABORT;
    free(old_path); free(new_path); leave_plugin(); return result;
}

static BOOL simple_mutation(wchar_t *name, int action)
{
    char *remote = NULL;
    Connection *c;
    int result = SF_ERR;
    if (!enter_plugin()) return FALSE;
    begin_operation(name, L"");
    c = resolve_remote(name, &remote, 1);
    if (c) {
        if (action == 0) result = sf_remove(&c->sf, remote);
        else if (action == 1) result = sf_rmdir(&c->sf, remote);
        else result = sf_mkdir(&c->sf, remote, NULL);
        if (result != SF_OK) connection_error(c, L"Не удалось выполнить операцию с файлом или каталогом.", 0);
    }
    free(remote); leave_plugin(); return result == SF_OK;
}

BOOL WINAPI FsDeleteFileW(wchar_t *name) { return simple_mutation(name, 0); }
BOOL WINAPI FsRemoveDirW(wchar_t *name) { return simple_mutation(name, 1); }
BOOL WINAPI FsMkDirW(wchar_t *name) { return simple_mutation(name, 2); }

BOOL WINAPI FsSetTimeW(wchar_t *name, FILETIME *creation, FILETIME *access, FILETIME *write)
{
    char *remote = NULL;
    Connection *c;
    SfAttrs existing, attrs = {0};
    BOOL result = FALSE;
    (void)creation;
    if (!access && !write) return FALSE;
    if (!enter_plugin()) return FALSE;
    begin_operation(name, L"");
    c = resolve_remote(name, &remote, 1);
    if (!c) goto done;
    if (sf_stat(&c->sf, remote, 1, &existing) != SF_OK) goto done;
    if (!(existing.flags & SF_ATTR_ACMODTIME) && (!access || !write)) goto done;
    attrs.atime = existing.atime; attrs.mtime = existing.mtime;
    if ((access && !kg_unix_time(access, &attrs.atime)) || (write && !kg_unix_time(write, &attrs.mtime))) goto done;
    attrs.flags = SF_ATTR_ACMODTIME;
    result = sf_setstat(&c->sf, remote, &attrs) == SF_OK;
    if (!result) connection_error(c, L"Не удалось изменить время файла.", 0);
done:
    free(remote); leave_plugin(); return result;
}

static int CALLBACK browse_callback(HWND window, UINT message, LPARAM param, LPARAM data)
{
    (void)param;
    if (message == BFFM_INITIALIZED && data) SendMessageW(window, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

static void browse_root(HWND dialog)
{
    BROWSEINFOW bi;
    PIDLIST_ABSOLUTE item;
    wchar_t path[MAX_PATH], relative[MAX_PATH];
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = dialog; bi.lpszTitle = L"Выберите папку портативной KiTTY-ng";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | (SUCCEEDED(hr) ? BIF_NEWDIALOGSTYLE : 0);
    bi.lpfn = browse_callback; bi.lParam = (LPARAM)config.root;
    item = SHBrowseForFolderW(&bi);
    if (item) {
        if (SHGetPathFromIDListW(item, path)) {
            if (wcslen(module_dir) < MAX_PATH && PathRelativePathToW(relative, module_dir, FILE_ATTRIBUTE_DIRECTORY, path, FILE_ATTRIBUTE_DIRECTORY))
                SetDlgItemTextW(dialog, IDC_ROOT, relative);
            else SetDlgItemTextW(dialog, IDC_ROOT, path);
        }
        CoTaskMemFree(item);
    }
    if (SUCCEEDED(hr)) CoUninitialize();
}

static int open_homepage(HWND parent)
{
    HINSTANCE result = ShellExecuteW(parent, L"open", KG_WTEXT(KG_HOMEPAGE_URL),
                                    NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        MessageBoxW(parent, L"Не удалось открыть домашнюю страницу в браузере:\n\n"
                    KG_WTEXT(KG_HOMEPAGE_URL), L"KiTTY-ng FS", MB_OK | MB_ICONERROR);
        return FS_EXEC_ERROR;
    }
    return FS_EXEC_OK;
}

static INT_PTR CALLBACK settings_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(dialog, IDC_ROOT, config.stored_root);
        SetDlgItemTextW(dialog, IDC_REMOTE_ROOT, config.remote_root);
        SetDlgItemInt(dialog, IDC_TIMEOUT, config.timeout_ms / 1000, FALSE);
        SendDlgItemMessageW(dialog, IDC_ROOT, EM_SETLIMITTEXT, KG_PATH - 1, 0);
        SendDlgItemMessageW(dialog, IDC_REMOTE_ROOT, EM_SETLIMITTEXT, 1023, 0);
        SendDlgItemMessageW(dialog, IDC_TIMEOUT, EM_SETLIMITTEXT, 3, 0);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == IDC_BROWSE) { browse_root(dialog); return TRUE; }
        if (LOWORD(wparam) == IDC_HOMEPAGE) { open_homepage(dialog); return TRUE; }
        if (LOWORD(wparam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (LOWORD(wparam) == IDOK) {
            Config candidate = {0};
            KgProfiles test = {0};
            wchar_t error[1024], timeout[16];
            BOOL translated;
            UINT seconds;
            GetDlgItemTextW(dialog, IDC_ROOT, candidate.stored_root, KG_PATH);
            GetDlgItemTextW(dialog, IDC_REMOTE_ROOT, candidate.remote_root, 1024);
            seconds = GetDlgItemInt(dialog, IDC_TIMEOUT, &translated, FALSE);
            if (!translated || seconds < 5 || seconds > 600 || !candidate.remote_root[0]) {
                MessageBoxW(dialog, L"Укажите начальный каталог и тайм-аут от 5 до 600 секунд.", L"KiTTY-ng FS", MB_OK | MB_ICONWARNING); return TRUE;
            }
            candidate.timeout_ms = seconds * 1000;
            if (!resolve_config_root(&candidate)) {
                MessageBoxW(dialog, L"Укажите существующий каталог портативных профилей KiTTY-ng.", L"KiTTY-ng FS", MB_OK | MB_ICONWARNING); return TRUE;
            }
            if (!kg_profiles_load(candidate.root, &test, error, 1024)) {
                MessageBoxW(dialog, error, L"KiTTY-ng FS", MB_OK | MB_ICONWARNING); return TRUE;
            }
            kg_profiles_free(&test);
            swprintf(timeout, 16, L"%u", seconds);
            /* A new UTF-16 INI retains Cyrillic paths even on a non-Russian OS. */
            if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES) {
                HANDLE f = CreateFileW(ini_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                if (f != INVALID_HANDLE_VALUE) { const unsigned char bom[2] = {0xff, 0xfe}; DWORD done; WriteFile(f, bom, 2, &done, NULL); CloseHandle(f); }
            }
            if (!WritePrivateProfileStringW(L"KiTTYngFS", L"PortableDir", candidate.stored_root, ini_path) ||
                !WritePrivateProfileStringW(L"KiTTYngFS", L"RemoteRoot", candidate.remote_root, ini_path) ||
                !WritePrivateProfileStringW(L"KiTTYngFS", L"TimeoutSeconds", timeout, ini_path)) {
                MessageBoxW(dialog, L"Не удалось сохранить kittyngfs.ini рядом с плагином. Проверьте права записи в папку плагина.", L"KiTTY-ng FS", MB_OK | MB_ICONERROR); return TRUE;
            }
            disconnect_all(); clear_profile_cache(); config = candidate;
            EndDialog(dialog, IDOK); return TRUE;
        }
    }
    return FALSE;
}

static int show_settings(HWND parent)
{
    INT_PTR result = DialogBoxParamW(module_instance, MAKEINTRESOURCEW(IDD_SETTINGS), parent, settings_dialog, 0);
    return result == -1 ? FS_EXEC_ERROR : FS_EXEC_OK;
}

static int launch_terminal(KgProfile *profile, HWND parent)
{
    wchar_t exe[KG_PATH], command[32768];
    size_t used = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (!kg_find_tool(config.root, 1, exe, KG_PATH)) {
        MessageBoxW(parent, L"В выбранной папке не найдена kitty-ng нужной архитектуры.", L"KiTTY-ng FS", MB_OK | MB_ICONERROR); return FS_EXEC_ERROR;
    }
    command[0] = 0;
    if (!kg_quote_arg(command, 32768, &used, exe) ||
        !kg_quote_arg(command, 32768, &used, L"--portable-dir") ||
        !kg_quote_arg(command, 32768, &used, config.root) ||
        !kg_quote_arg(command, 32768, &used, L"-load") ||
        !kg_quote_arg(command, 32768, &used, profile->load_name)) return FS_EXEC_ERROR;
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(exe, command, NULL, NULL, FALSE, 0, NULL, config.root, &si, &pi)) return FS_EXEC_ERROR;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return FS_EXEC_OK;
}

static int symlink_navigation(Connection *c, const char *target, wchar_t *name)
{
    const char *relative = target;
    size_t base_len = strlen(c->base), used;
    wchar_t result[MAX_PATH];
    if (strcmp(c->base, "/")) {
        while (base_len > 1 && c->base[base_len - 1] == '/') --base_len;
        if (strncmp(target, c->base, base_len) || (target[base_len] && target[base_len] != '/')) {
            error_message(L"Ссылка ведёт за пределы начального каталога. Для доступа укажите / в настройках плагина."); return FS_EXEC_ERROR;
        }
        relative = target + base_len;
    }
    if (swprintf(result, MAX_PATH, L"%ls", c->session_root) < 0) return FS_EXEC_ERROR;
    used = wcslen(result);
    while (*relative) {
        const char *end;
        char *component;
        wchar_t display[MAX_PATH];
        size_t len;
        while (*relative == '/') ++relative;
        if (!*relative) break;
        end = strchr(relative, '/'); if (!end) end = relative + strlen(relative);
        len = (size_t)(end - relative);
        component = malloc(len + 1); if (!component) return FS_EXEC_ERROR;
        memcpy(component, relative, len); component[len] = 0;
        if (!kg_display_name(component, display, MAX_PATH)) { free(component); return FS_EXEC_ERROR; }
        free(component); len = wcslen(display);
        if (used + 1 + len >= MAX_PATH) { error_message(L"Путь ссылки превышает предел перехода Total Commander (259 символов)."); return FS_EXEC_ERROR; }
        result[used++] = L'\\'; memcpy(result + used, display, (len + 1) * sizeof(wchar_t)); used += len;
        relative = end;
    }
    wcscpy(name, result); return FS_EXEC_SYMLINK;
}

int WINAPI FsExecuteFileW(HWND parent, wchar_t *name, wchar_t *verb)
{
    const wchar_t *relative;
    KgProfile *profile;
    Connection *c = NULL;
    char *remote = NULL;
    KgTreeMatch match;
    int result = FS_EXEC_ERROR;
    if (!enter_plugin()) return FS_EXEC_ERROR;
    begin_operation(name, L"");
    if ((!_wcsicmp(verb, L"properties") && root_path(name)) ||
        (!_wcsicmp(verb, L"open") && settings_path(name)) ||
        !_wcsicmp(verb, L"quote settings")) { result = show_settings(parent); goto done; }
    if (!_wcsicmp(verb, L"quote help")) {
        MessageBoxW(parent, L"KiTTY-ng FS\nАвтор: " KG_WTEXT(KG_AUTHOR)
                    L"\nДомашняя страница: " KG_WTEXT(KG_HOMEPAGE_URL)
                    L"\n\nКоманды в строке Total Commander:\nterminal — открыть сеанс в KiTTY-ng\ndisconnect — закрыть SSH-сеанс\nrefresh — перечитать список сессий\nsettings — настройки плагина\nhomepage — открыть домашнюю страницу\n\nФайлы передаются по SFTP. SSH и SFTP встроены в плагин. Используются профили KiTTY-ng; внешний Klink не нужен.", L"KiTTY-ng FS", MB_OK | MB_ICONINFORMATION);
        result = FS_EXEC_OK; goto done;
    }
    if (!_wcsicmp(verb, L"quote homepage")) { result = open_homepage(parent); goto done; }
    if (!_wcsicmp(verb, L"quote refresh")) {
        disconnect_all(); clear_profile_cache(); result = refresh_profiles(1) ? FS_EXEC_OK : FS_EXEC_ERROR; goto done;
    }
    if (lookup_node(name, &match) && match.kind == KG_TREE_GROUP) {
        if (!_wcsicmp(verb, L"open")) result = FS_EXEC_OK;
        else if (!_wcsicmp(verb, L"properties")) {
            wchar_t text[2048];
            swprintf(text, 2048, L"Группа KiTTY-ng: %ls\n\nВыберите вложенную папку или SSH-подключение. Группы читаются из портативной папки KiTTY-ng.", name);
            text[2047] = 0;
            MessageBoxW(parent, text, L"KiTTY-ng FS", MB_OK | MB_ICONINFORMATION); result = FS_EXEC_OK;
        }
        goto done;
    }
    profile = lookup_profile(name, &relative);
    if (!profile) goto done;
    if (!_wcsicmp(verb, L"open") && !*relative) {
        const wchar_t *target = session_tree.nodes[session_tree.profile_nodes[
            (size_t)(profile - profiles.items)]].path;
        size_t length = wcslen(target);
        /* A trailing separator identifies the directory behind the session
         * link. The documented FsExecuteFile output is MAX_PATH, even when
         * TC passes a longer input. Do not touch the buffer on failure.
         * SSH starts later, when TC enumerates the destination directory. */
        if (length >= MAX_PATH - 1) {
            error_message(L"Путь подключения превышает предел перехода Total Commander (259 символов). Сократите имена групп или сеанса в KiTTY-ng.");
            SetLastError(ERROR_FILENAME_EXCED_RANGE); goto done;
        }
        memcpy(name, target, length * sizeof(wchar_t));
        name[length] = L'\\'; name[length + 1] = 0;
        result = FS_EXEC_SYMLINK; goto done;
    }
    if (!_wcsicmp(verb, L"quote terminal")) { result = launch_terminal(profile, parent); goto done; }
    if (!_wcsicmp(verb, L"quote disconnect")) {
        size_t i;
        for (i = 0; i < KG_MAX_CONNECTIONS; ++i)
            if (connections[i].load_name && !wcscmp(connections[i].load_name, profile->load_name)) connection_close(&connections[i]);
        result = FS_EXEC_OK; goto done;
    }
    if (!_wcsicmp(verb, L"properties") && !*relative) {
        wchar_t text[2048];
        swprintf(text, 2048, L"Сеанс: %ls\nСервер: %ls\nПорт: %d\n\nОткрыть этот сеанс в KiTTY-ng?", profile->load_name, profile->host, profile->port);
        text[2047] = 0;
        result = MessageBoxW(parent, text, L"KiTTY-ng FS", MB_YESNO | MB_ICONINFORMATION) == IDYES ? launch_terminal(profile, parent) : FS_EXEC_OK;
        goto done;
    }
    c = resolve_remote(name, &remote, 0);
    if (!c) goto done;
    if (!_wcsnicmp(verb, L"chmod ", 6)) {
        const wchar_t *p = verb + 6;
        uint32_t mode = 0;
        size_t n = 0;
        SfAttrs attrs = {0};
        while (*p >= L'0' && *p <= L'7' && n < 4) { mode = mode * 8 + (uint32_t)(*p++ - L'0'); ++n; }
        if (*p || n < 3) goto done;
        attrs.flags = SF_ATTR_PERMISSIONS; attrs.permissions = mode;
        result = sf_setstat(&c->sf, remote, &attrs) == SF_OK ? FS_EXEC_OK : FS_EXEC_ERROR;
        if (result != FS_EXEC_OK) connection_error(c, L"Не удалось изменить права Unix.", 0);
    } else if (!_wcsicmp(verb, L"open")) {
        SfAttrs attrs;
        result = FS_EXEC_YOURSELF;
        if (sf_stat(&c->sf, remote, 0, &attrs) != SF_OK) { result = FS_EXEC_ERROR; goto done; }
        if ((attrs.flags & SF_ATTR_PERMISSIONS) && (attrs.permissions & KG_S_IFMT) == KG_S_IFLNK) {
            char *target = NULL;
            if (sf_stat(&c->sf, remote, 1, &attrs) != SF_OK) { result = FS_EXEC_ERROR; goto done; }
            if ((attrs.flags & SF_ATTR_PERMISSIONS) && (attrs.permissions & KG_S_IFMT) == KG_S_IFDIR) {
                if (sf_realpath(&c->sf, remote, &target) == SF_OK) result = symlink_navigation(c, target, name);
                else result = FS_EXEC_ERROR;
                free(target);
            }
        }
    } else if (!_wcsicmp(verb, L"properties")) {
        SfAttrs attrs;
        wchar_t text[2048];
        if (sf_stat(&c->sf, remote, 0, &attrs) != SF_OK) goto done;
        swprintf(text, 2048, L"%ls\n\nПрава Unix: %04o\nРазмер: %llu байт\n\nИзменение прав: Файлы → Изменить атрибуты.", name,
                 (unsigned)(attrs.permissions & 07777u), (unsigned long long)attrs.size);
        text[2047] = 0;
        MessageBoxW(parent, text, L"KiTTY-ng FS", MB_OK | MB_ICONINFORMATION); result = FS_EXEC_OK;
    }
done:
    free(remote); leave_plugin(); return result;
}

BOOL WINAPI FsDisconnectW(wchar_t *root)
{
    size_t i;
    KgTreeMatch match;
    const wchar_t *load_name = NULL;
    if (!enter_plugin()) return FALSE;
    begin_operation(root, L"");
    if (session_tree.count && kg_tree_resolve(&session_tree, &profiles, root, 1, &match) &&
        match.kind == KG_TREE_SESSION && !*match.relative) load_name = profiles.items[match.profile_index].load_name;
    for (i = 0; i < KG_MAX_CONNECTIONS; ++i)
        if (connections[i].session_root && (root_path(root) || !wcscmp(connections[i].session_root, root) ||
            (load_name && !wcscmp(connections[i].load_name, load_name)))) connection_close(&connections[i]);
    log_message(MSGTYPE_DISCONNECT, root);
    leave_plugin(); return TRUE;
}

int WINAPI FsExtractCustomIconW(wchar_t *name, int flags, HICON *icon)
{
    int resource = 0;
    int size = (flags & FS_ICONFLAG_SMALL) ? 16 : 32;
    KgTreeMatch match;
    if (!icon) return FS_ICON_USEDEFAULT;
    *icon = NULL;
    if (!name) return FS_ICON_USEDEFAULT;
    if (root_path(name)) resource = IDI_PLUGIN;
    else if (settings_path(name)) resource = IDI_SETTINGS;
    else {
        /* Group names and file paths remain ordinary TC folders/files. No
         * host lookup, directory scan, prompt or process creation occurs here. */
        AcquireSRWLockShared(&profile_cache_lock);
        if (session_tree.count && kg_tree_resolve(&session_tree, &profiles, name, 1, &match) &&
            match.kind == KG_TREE_SESSION && !*match.relative) resource = IDI_SSH;
        ReleaseSRWLockShared(&profile_cache_lock);
    }
    if (!resource) return FS_ICON_USEDEFAULT;
    *icon = (HICON)LoadImageW(module_instance, MAKEINTRESOURCEW(resource), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    return *icon ? FS_ICON_EXTRACTED_DESTROY : FS_ICON_USEDEFAULT;
}

int WINAPI FsInit(int number, tProgressProc progress, tLogProc log, tRequestProc request)
{
    request_a = request; kg_transport_set_prompt(request_input, NULL);
    plugin_number = number; progress_a = progress; log_a = log; return 0;
}

int WINAPI FsInitW(int number, tProgressProcW progress, tLogProcW log, tRequestProcW request)
{
    request_w = request; kg_transport_set_prompt(request_input, NULL);
    plugin_number = number; progress_w = progress; log_w = log; return 0;
}

void WINAPI FsGetDefRootName(char *name, int maxlen)
{
    static const char label[] = "KiTTY-ng FS";
    if (maxlen > 0) { size_t n = strlen(label); if (n >= (size_t)maxlen) n = (size_t)maxlen - 1; memcpy(name, label, n); name[n] = 0; }
}

void WINAPI FsSetDefaultParams(FsDefaultParamStruct *params)
{
    /* Configuration intentionally travels beside this portable plugin. */
    (void)params;
}

static wchar_t *from_ansi(const char *text)
{
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    wchar_t *wide;
    if (!n || !(wide = malloc((size_t)n * sizeof(*wide)))) return NULL;
    if (!MultiByteToWideChar(CP_ACP, 0, text, -1, wide, n)) { free(wide); return NULL; }
    return wide;
}

int WINAPI FsExtractCustomIcon(char *name, int flags, HICON *icon)
{
    wchar_t *wide;
    int result;
    if (!icon) return FS_ICON_USEDEFAULT;
    *icon = NULL;
    if (!name || !(wide = from_ansi(name))) return FS_ICON_USEDEFAULT;
    result = FsExtractCustomIconW(wide, flags, icon);
    free(wide); return result;
}

static int find_to_ansi(const WIN32_FIND_DATAW *wide, WIN32_FIND_DATAA *ansi)
{
    BOOL used_default = FALSE;
    memset(ansi, 0, sizeof(*ansi));
    ansi->dwFileAttributes = wide->dwFileAttributes;
    ansi->ftCreationTime = wide->ftCreationTime; ansi->ftLastAccessTime = wide->ftLastAccessTime; ansi->ftLastWriteTime = wide->ftLastWriteTime;
    ansi->nFileSizeHigh = wide->nFileSizeHigh; ansi->nFileSizeLow = wide->nFileSizeLow;
    ansi->dwReserved0 = wide->dwReserved0;
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide->cFileName, -1, ansi->cFileName, MAX_PATH, NULL, &used_default) || used_default) return 0;
    return 1;
}

HANDLE WINAPI FsFindFirst(char *path, WIN32_FIND_DATAA *data)
{
    wchar_t *wide = from_ansi(path);
    WIN32_FIND_DATAW item;
    HANDLE h;
    DWORD error;
    if (!wide) { SetLastError(ERROR_INVALID_NAME); return INVALID_HANDLE_VALUE; }
    h = FsFindFirstW(wide, &item); error = GetLastError(); free(wide);
    if (h == INVALID_HANDLE_VALUE) { SetLastError(error); return h; }
    do { if (find_to_ansi(&item, data)) return h; } while (FsFindNextW(h, &item));
    FsFindClose(h); SetLastError(ERROR_NO_MORE_FILES); return INVALID_HANDLE_VALUE;
}

BOOL WINAPI FsFindNext(HANDLE handle, WIN32_FIND_DATAA *data)
{
    WIN32_FIND_DATAW item;
    while (FsFindNextW(handle, &item)) if (find_to_ansi(&item, data)) return TRUE;
    return FALSE;
}

int WINAPI FsGetFile(char *remote, char *local, int flags, RemoteInfoStruct *info)
{
    wchar_t *r = from_ansi(remote), *l = from_ansi(local);
    int result = r && l ? FsGetFileW(r, l, flags, info) : FS_FILE_NOTSUPPORTED;
    free(r); free(l); return result;
}
int WINAPI FsPutFile(char *local, char *remote, int flags)
{
    wchar_t *r = from_ansi(remote), *l = from_ansi(local);
    int result = r && l ? FsPutFileW(l, r, flags) : FS_FILE_NOTSUPPORTED;
    free(r); free(l); return result;
}
int WINAPI FsRenMovFile(char *old_name, char *new_name, BOOL move, BOOL overwrite, RemoteInfoStruct *info)
{
    wchar_t *a = from_ansi(old_name), *b = from_ansi(new_name);
    int result = a && b ? FsRenMovFileW(a, b, move, overwrite, info) : FS_FILE_NOTSUPPORTED;
    free(a); free(b); return result;
}
BOOL WINAPI FsDeleteFile(char *name) { wchar_t *w = from_ansi(name); BOOL r = w ? FsDeleteFileW(w) : FALSE; free(w); return r; }
BOOL WINAPI FsRemoveDir(char *name) { wchar_t *w = from_ansi(name); BOOL r = w ? FsRemoveDirW(w) : FALSE; free(w); return r; }
BOOL WINAPI FsMkDir(char *name) { wchar_t *w = from_ansi(name); BOOL r = w ? FsMkDirW(w) : FALSE; free(w); return r; }
BOOL WINAPI FsDisconnect(char *name) { wchar_t *w = from_ansi(name); BOOL r = w ? FsDisconnectW(w) : FALSE; free(w); return r; }
BOOL WINAPI FsSetTime(char *name, FILETIME *creation, FILETIME *access, FILETIME *write)
{
    wchar_t *w = from_ansi(name); BOOL result = w ? FsSetTimeW(w, creation, access, write) : FALSE; free(w); return result;
}
int WINAPI FsExecuteFile(HWND parent, char *name, char *verb)
{
    wchar_t *converted = from_ansi(name), *v = from_ansi(verb), buffer[1024];
    int result = FS_EXEC_ERROR;
    if (converted && v && wcslen(converted) < 1024) {
        wcscpy(buffer, converted); result = FsExecuteFileW(parent, buffer, v);
        if (result == FS_EXEC_SYMLINK) {
            char target[MAX_PATH];
            BOOL fallback = FALSE;
            if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, buffer, -1, target, MAX_PATH, NULL, &fallback) || fallback) result = FS_EXEC_ERROR;
            else strcpy(name, target);
        }
    }
    free(converted); free(v); return result;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    size_t i;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        module_instance = instance; InitializeCriticalSection(&mutex); DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        /* No user callbacks or worker threads survive unloading the embedded engine. */
        for (i = 0; i < KG_MAX_CONNECTIONS; ++i) {
            if (connections[i].transport) kg_transport_abandon(connections[i].transport);
            free(connections[i].session_root); free(connections[i].load_name); free(connections[i].base);
        }
        kg_tree_free(&session_tree); kg_profiles_free(&profiles); DeleteCriticalSection(&mutex);
    }
    return TRUE;
}
