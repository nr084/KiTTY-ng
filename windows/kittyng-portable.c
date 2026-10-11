/* Portable backend for PuTTY's registry storage interface. */
#include "putty.h"
#include "kittyng-portable.h"
#include "kittyng.h"
#include <shellapi.h>

typedef struct FileKey FileKey;
typedef struct FileValue FileValue;
struct FileValue { FileValue *next; char *name, *value; };
struct FileKey {
    FileKey *next; wchar_t *path; bool writable;
    bool session, hostkeys, sessions;
    FileValue *values;
    bool dirty;
};
static FileKey *filekeys;
static wchar_t *portable_root;
static bool initialized;
static wchar_t *sessions_directory, *hostkeys_directory, *seed_path;
static wchar_t session_suffix[64], hostkey_suffix[64];
static wchar_t *join(const wchar_t *parent, const wchar_t *child);

static wchar_t *configpath(const char *value)
{
    wchar_t *wide = dup_mb_to_wc(CP_ACP, value);
    DWORD n = ExpandEnvironmentStringsW(wide, NULL, 0);
    wchar_t *expanded = snewn(n ? n : 1, wchar_t);
    if (!n || !ExpandEnvironmentStringsW(wide, expanded, n)) expanded[0] = 0;
    wchar_t *result;
    if (expanded[0] == L'\\' || (expanded[0] && expanded[1] == L':'))
        result = dupwcs(expanded);
    else result = join(portable_root, expanded);
    sfree(wide); sfree(expanded);
    return result;
}

static void load_legacy_paths(void)
{
    sessions_directory = join(portable_root, L"Sessions");
    hostkeys_directory = join(portable_root, L"SshHostKeys");
    seed_path = join(portable_root, L"putty.rnd");
    wchar_t *ini = join(portable_root, L"kitty.ini");
    GetPrivateProfileStringW(L"KiTTY", L"fileextension", L"", session_suffix, lenof(session_suffix), ini);
    GetPrivateProfileStringW(L"KiTTY", L"hostkeyextension", L"", hostkey_suffix, lenof(hostkey_suffix), ini);
    sfree(ini);
    wchar_t *config = join(portable_root, L"putty.conf");
    FILE *fp = _wfopen(config, L"rb");
    sfree(config);
    char *line;
    while (fp && (line = fgetline(fp)) != NULL) {
        size_t n = strlen(line);
        while (n && (unsigned char)line[n-1] <= 32) line[--n] = 0;
        char *value = strchr(line, '=');
        if (value && line[0] != ';') {
            *value++ = 0;
            wchar_t **destination = !strcmp(line, "Sessions") ? &sessions_directory :
                !strcmp(line, "SshHostKeys") ? &hostkeys_directory :
                !strcmp(line, "seedfile") ? &seed_path : NULL;
            if (destination) { sfree(*destination); *destination = configpath(value); }
            else if (!strcmp(line, "sessionsuffix") || !strcmp(line, "keysuffix")) {
                wchar_t *suffix = dup_mb_to_wc(CP_ACP, value);
                wchar_t *dest = !strcmp(line, "sessionsuffix") ? session_suffix : hostkey_suffix;
                wcsncpy(dest, suffix, 63); dest[63] = 0; sfree(suffix);
            }
        }
        sfree(line);
    }
    if (fp) fclose(fp);
    if (wcspbrk(session_suffix, L"\\/:<>\"|")) session_suffix[0] = 0;
    if (wcspbrk(hostkey_suffix, L"\\/:<>\"|")) hostkey_suffix[0] = 0;
}

bool kr_portable(void)
{
    if (!initialized) {
        initialized = true;
        wchar_t executable[32768];
        DWORD n = GetModuleFileNameW(NULL, executable, lenof(executable));
        if (!n || n >= lenof(executable))
            return false;
        wchar_t *slash = wcsrchr(executable, L'\\');
        if (!slash)
            return false;
        *slash = 0;
        wchar_t *flag = dupwcs(executable);
        size_t dirlen = wcslen(executable);
        flag = sresize(flag, dirlen + 32, wchar_t);
        wcscat(flag, L"\\kitty-ng.portable");
        bool enabled = GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES;
        sfree(flag);
        wchar_t *ini = snewn(dirlen + 32, wchar_t);
        wcscpy(ini, executable); wcscat(ini, L"\\kitty.ini");
        wchar_t mode[32];
        GetPrivateProfileStringW(L"KiTTY", L"savemode", L"", mode, lenof(mode), ini);
        bool legacy = !wcscmp(mode, L"dir");
        enabled |= legacy;
        sfree(ini);
        int argc;
        wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        const wchar_t *chosen = NULL;
        wchar_t inherited[32768];
        DWORD inherited_len = GetEnvironmentVariableW(L"KITTY_NG_PORTABLE_DIR", inherited, lenof(inherited));
        if (inherited_len && inherited_len < lenof(inherited)) {
            chosen = inherited; enabled = true;
        }
        for (int i = 1; argv && i < argc; i++) {
            if (!wcscmp(argv[i], L"--portable"))
                enabled = true;
            if (!wcscmp(argv[i], L"--portable-dir") && i + 1 < argc) {
                enabled = true;
                chosen = argv[++i];
            }
        }
        if (enabled) {
            if (chosen) {
                DWORD size = GetFullPathNameW(chosen, 0, NULL, NULL);
                if (size) {
                    portable_root = snewn(size, wchar_t);
                    if (!GetFullPathNameW(chosen, size, portable_root, NULL)) {
                        sfree(portable_root);
                        portable_root = NULL;
                    }
                }
            } else {
                portable_root = snewn(dirlen + 32, wchar_t);
                wcscpy(portable_root, executable);
                /* Match KiTTY: Sessions and SshHostKeys beside the EXE. */
            }
        }
        if (argv)
            LocalFree(argv);
        if (portable_root) {
            load_legacy_paths();
            SetEnvironmentVariableW(L"KITTY_NG_PORTABLE_DIR", portable_root);
        }
    }
    return portable_root != NULL;
}

const wchar_t *kr_portable_root(void)
{
    kr_portable();
    return portable_root;
}

const wchar_t *kr_portable_seedpath(void) { kr_portable(); return seed_path; }

static FileKey *findkey(HKEY key)
{
    for (FileKey *p = filekeys; p; p = p->next)
        if ((HKEY)p == key)
            return p;
    return NULL;
}

bool kr_file_key(HKEY key) { return findkey(key) != NULL; }
bool kr_plain_key(HKEY key) {
    FileKey *k = findkey(key);
    return k && (k->session || k->hostkeys);
}

/* Encode components so no saved name can escape the portable directory.
 * Escape dots and '%' too, including Windows device names and trailing dots. */
static wchar_t *component(const char *name)
{
    strbuf *sb = strbuf_new();
    put_byte(sb, '_'); /* avoids reserved device basenames such as CON */
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')
            put_byte(sb, *p);
        else
            put_fmt(sb, "%%%02X", *p);
    }
    wchar_t *out = dup_mb_to_wc(CP_UTF8, sb->s);
    strbuf_free(sb);
    return out;
}

static wchar_t *join(const wchar_t *parent, const wchar_t *child)
{
    size_t n = wcslen(parent) + wcslen(child) + 2;
    wchar_t *out = snewn(n, wchar_t);
    swprintf(out, n, L"%ls\\%ls", parent, child);
    return out;
}

/* Match KiTTY's packstr without double-escaping its %-escaped filenames. */
static wchar_t *legacy_component(const char *name)
{
    strbuf *sb = strbuf_new();
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (strchr("<>:\"/\\|", *p) || *p < 32 ||
            (*p == '.' && (p == (const unsigned char *)name || !p[1])))
            put_fmt(sb, "%%%02X", *p);
        else
            put_byte(sb, *p);
    }
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, sb->s);
    strbuf_free(sb);
    return wide;
}

static void setvalue(FileKey *key, const char *name, const char *value)
{
    FileValue **p = &key->values;
    while (*p && strcmp((*p)->name, name)) p = &(*p)->next;
    if (!*p) {
        *p = snew(FileValue);
        (*p)->next = NULL;
        (*p)->name = dupstr(name);
        (*p)->value = NULL;
    }
    sfree((*p)->value);
    (*p)->value = dupstr(value);
}

static void loadsession(FileKey *key)
{
    FILE *fp = _wfopen(key->path, L"rb");
    if (!fp) return;
    char *line;
    while ((line = fgetline(fp)) != NULL) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = 0;
        char *split = strchr(line, '\\');
        if (split && n && line[n-1] == '\\') {
            *split++ = 0; line[n-1] = 0;
            strbuf *value = percent_decode_sb(ptrlen_from_asciz(split));
            if (!memchr(value->s, 0, value->len))
                setvalue(key, line, value->s);
            strbuf_free(value);
        }
        sfree(line);
    }
    fclose(fp);
}

static bool savesession(FileKey *key)
{
    wchar_t *parent = dupwcs(key->path), tmp[MAX_PATH];
    wchar_t *slash = wcsrchr(parent, L'\\');
    if (!slash) { sfree(parent); return false; }
    *slash = 0;
    if (!GetTempFileNameW(parent, L"krs", 0, tmp)) { sfree(parent); return false; }
    sfree(parent);
    FILE *fp = _wfopen(tmp, L"wb");
    bool ok = fp != NULL;
    for (FileValue *p = key->values; fp && ok && p; p = p->next) {
        /* KiTTY mungestr: preserve printable bytes except reserved characters. */
        strbuf *encoded = strbuf_new();
        for (const unsigned char *s = (const unsigned char *)p->value; *s; s++) {
            if (*s <= 32 || *s > 126 || strchr("\\*?:/\"<>|%", *s) ||
                (*s == '.' && s == (const unsigned char *)p->value))
                put_fmt(encoded, "%%%02X", *s);
            else put_byte(encoded, *s);
        }
        ok = fprintf(fp, "%s\\%s\\\n", p->name, encoded->s) >= 0;
        strbuf_free(encoded);
    }
    if (fp && fclose(fp)) ok = false;
    if (ok) ok = MoveFileExW(tmp, key->path,
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(tmp);
    return ok;
}

HKEY kr_file_open(bool create, HKEY parent, const char *path)
{
    FileKey *pk = findkey(parent);
    wchar_t *directory;
    bool session = pk && pk->sessions, sessions = false, hostkeys = false;
    if (pk) {
        directory = dupwcs(pk->path);
    } else {
        size_t len = strlen(PUTTY_REG_POS);
        if (parent != HKEY_CURRENT_USER || !kr_portable() ||
            strncmp(path, PUTTY_REG_POS, len) || (path[len] && path[len] != '\\'))
            return NULL;
        directory = dupwcs(portable_root);
        path += len;
    }
    if (create && !CreateDirectoryW(directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        sfree(directory);
        return NULL;
    }
    if (!pk && *path == '\\') path++;
    if (!pk && !strncmp(path, "Sessions", 8) && (!path[8] || path[8] == '\\')) {
        wchar_t *next = dupwcs(sessions_directory);
        sfree(directory); directory = next;
        path += 8;
        sessions = !*path;
        session = *path != 0;
        if (*path == '\\') path++;
        if (create) CreateDirectoryW(directory, NULL);
    } else if (!pk && !strcmp(path, "SshHostKeys")) {
        wchar_t *next = dupwcs(hostkeys_directory);
        sfree(directory); directory = next;
        path += strlen(path); hostkeys = true;
        if (create) CreateDirectoryW(directory, NULL);
    }
    while (*path) {
        if (*path == '\\' || *path == '/') { path++; continue; }
        size_t len = strcspn(path, "\\/");
        char *part = dupprintf("%.*s", (int)len, path);
        bool final = !path[len];
        if (session && !final) {
            strbuf *decoded = percent_decode_sb(ptrlen_from_asciz(part));
            sfree(part); part = strbuf_to_str(decoded);
        }
        wchar_t *encoded = session ? legacy_component(part) : component(part);
        wchar_t *next = join(directory, encoded);
        if (session && final && *session_suffix) {
            size_t a = wcslen(next), b = wcslen(session_suffix);
            if (a < b || wcscmp(next + a - b, session_suffix)) {
                next = sresize(next, a + b + 1, wchar_t);
                wcscat(next, session_suffix);
            }
        }
        sfree(part); sfree(encoded); sfree(directory);
        directory = next;
        if (create && !(session && final) && !CreateDirectoryW(directory, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            sfree(directory);
            return NULL;
        }
        path += len;
    }
    DWORD attr = GetFileAttributesW(directory);
    if ((!session && (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))) ||
        (session && !create && attr == INVALID_FILE_ATTRIBUTES) ||
        (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT))) {
        sfree(directory);
        return NULL;
    }
    FileKey *key = snew(FileKey);
    key->path = directory;
    key->writable = create; /* open_regkey_fn sets writable below for rw */
    key->session = session; key->sessions = sessions; key->hostkeys = hostkeys;
    key->values = NULL; key->dirty = false;
    key->next = filekeys;
    filekeys = key;
    if (session) loadsession(key);
    return (HKEY)key;
}

void kr_file_close(HKEY key)
{
    FileKey **p = &filekeys;
    while (*p && (HKEY)*p != key) p = &(*p)->next;
    if (*p) {
        FileKey *old = *p;
        if (old->session && old->dirty && !savesession(old))
            kr_messagebox(NULL, "Unable to save portable session", "KiTTY-NG", MB_OK | MB_ICONERROR);
        while (old->values) {
            FileValue *v = old->values;
            old->values = v->next;
            sfree(v->name); sfree(v->value); sfree(v);
        }
        *p = old->next;
        sfree(old->path);
        sfree(old);
    }
}

static wchar_t *valuesfile(HKEY key)
{
    FileKey *k = findkey(key);
    return k ? join(k->path, L"values.ini") : NULL;
}

static wchar_t *hostkeyfile(FileKey *key, const char *name)
{
    wchar_t *leaf = legacy_component(name), *path = join(key->path, leaf);
    sfree(leaf);
    path = sresize(path, wcslen(path) + wcslen(hostkey_suffix) + 1, wchar_t);
    wcscat(path, hostkey_suffix);
    return path;
}

char *kr_file_get(HKEY key, const char *name)
{
    FileKey *k = findkey(key);
    if (k->session) {
        for (FileValue *p = k->values; p; p = p->next)
            if (!strcmp(p->name, name)) return dupstr(p->value);
        return NULL;
    }
    if (k->hostkeys) {
        wchar_t *path = hostkeyfile(k, name);
        FILE *fp = _wfopen(path, L"rb");
        char *value = fp ? fgetline(fp) : NULL;
        if (fp) fclose(fp);
        sfree(path);
        return value;
    }
    wchar_t *file = valuesfile(key), *entry = component(name);
    size_t size = 256;
    wchar_t *value = snewn(size, wchar_t);
    DWORD got;
    while ((got = GetPrivateProfileStringW(L"values", entry, L"", value,
                                            (DWORD)size, file)) >= size - 1) {
        if (size >= 16 * 1024 * 1024) break;
        size *= 2;
        value = sresize(value, size, wchar_t);
    }
    char *result = got && got < size - 1 ? dup_wc_to_mb(CP_UTF8, value, "") : NULL;
    sfree(value); sfree(entry); sfree(file);
    return result;
}

bool kr_file_put(HKEY key, const char *name, const char *value)
{
    FileKey *k = findkey(key);
    if (k->session) {
        setvalue(k, name, value); k->dirty = true; return true;
    }
    if (k->hostkeys) {
        wchar_t *path = hostkeyfile(k, name);
        FILE *fp = _wfopen(path, L"wb");
        bool ok = fp && fputs(value, fp) >= 0;
        if (fp && fclose(fp)) ok = false;
        sfree(path);
        return ok;
    }
    wchar_t *file = valuesfile(key), *entry = component(name);
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, value);
    bool ok = WritePrivateProfileStringW(L"values", entry, wide, file) != FALSE;
    sfree(wide); sfree(entry); sfree(file);
    return ok;
}

static void collect(const wchar_t *directory, const char *prefix,
                    bool recursive, char **result, int *index)
{
    wchar_t *pattern = join(directory, recursive ? L"*" : L"_*");
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    sfree(pattern);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..") ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (!recursive && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        char *encoded = dup_wc_to_mb(CP_UTF8, data.cFileName + (recursive ? 0 : 1), "");
        char *name = dupprintf("%s%s%s", prefix, *prefix ? "/" : "", encoded);
        wchar_t *subdir = join(directory, data.cFileName);
        bool isdir = data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        size_t suffixlen = wcslen(session_suffix), namelen = wcslen(data.cFileName);
        bool suffixmatch = !suffixlen || (namelen >= suffixlen &&
            !wcscmp(data.cFileName + namelen - suffixlen, session_suffix));
        if (recursive && !isdir && suffixlen && suffixmatch) {
            size_t n = strlen(name);
            char *suffix = dup_wc_to_mb(CP_UTF8, session_suffix, "");
            name[n - strlen(suffix)] = 0; sfree(suffix);
        }
        bool istemp = wcslen(data.cFileName) >= 4 &&
            !wcscmp(data.cFileName + wcslen(data.cFileName) - 4, L".tmp");
        if (!recursive || (!isdir && !istemp && suffixmatch)) {
            if ((*index)-- == 0) *result = dupstr(name);
        }
        if (recursive && isdir && !*result)
            collect(subdir, name, true, result, index);
        sfree(encoded); sfree(name); sfree(subdir);
    } while (!*result && FindNextFileW(search, &data));
    FindClose(search);
}

char *kr_file_enum(HKEY key, int index)
{
    FileKey *k = findkey(key);
    char *result = NULL;
    bool recursive = k->sessions;
    collect(k->path, "", recursive, &result, &index);
    return result;
}

void kr_file_delete(HKEY key, const char *name)
{
    HKEY sub = kr_file_open(false, key, name);
    if (!sub) return;
    FileKey *k = findkey(sub);
    if (k->session) {
        DeleteFileW(k->path); kr_file_close(sub); return;
    }
    wchar_t *file = valuesfile(sub);
    DeleteFileW(file);
    RemoveDirectoryW(k->path); /* Never recursively deletes a session's siblings. */
    sfree(file);
    kr_file_close(sub);
}
