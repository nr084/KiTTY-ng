/* Portable KiTTY-ng profile discovery. No SSH secrets are interpreted here.
 * Compatible with KiTTY-ng 0.85-6 / f0e383e7f3373126843d942f173689daacdcbf05.
 * Storage reference: nr084/KiTTY-ng/windows/kittyng-portable.c. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "profiles.h"

#define KG_PATH_LIMIT 32768u
#define KG_FILE_LIMIT (1024u * 1024u)
#define KG_PROFILE_LIMIT 10000u
#define KG_ENTRY_LIMIT 100000u
#define KG_DEPTH_LIMIT 16u

typedef struct KgScan {
    KgProfiles *out;
    size_t capacity;
    size_t skipped;
    size_t folder_encoding_skipped;
    size_t entries;
    wchar_t suffix[64];
    wchar_t *error;
    size_t error_cap;
} KgScan;

static void set_error(wchar_t *out, size_t cap, const wchar_t *message)
{
    size_t n;
    if (!out || !cap) return;
    n = wcslen(message);
    if (n >= cap) n = cap - 1;
    if (n) memcpy(out, message, n * sizeof(*out));
    out[n] = 0;
}

static wchar_t *dup_wide(const wchar_t *s)
{
    size_t n = wcslen(s);
    wchar_t *r;
    if (n >= KG_PATH_LIMIT) return NULL;
    r = (wchar_t *)malloc((n + 1) * sizeof(*r));
    if (r) memcpy(r, s, (n + 1) * sizeof(*r));
    return r;
}

static wchar_t *join_path(const wchar_t *a, const wchar_t *b, wchar_t separator)
{
    size_t na = wcslen(a), nb = wcslen(b);
    int sep = na && a[na - 1] != separator;
    wchar_t *r;
    if (na >= KG_PATH_LIMIT || nb >= KG_PATH_LIMIT ||
        na + nb + (size_t)sep >= KG_PATH_LIMIT) return NULL;
    r = (wchar_t *)malloc((na + nb + (size_t)sep + 1) * sizeof(*r));
    if (!r) return NULL;
    memcpy(r, a, na * sizeof(*r));
    if (sep) r[na++] = separator;
    memcpy(r + na, b, (nb + 1) * sizeof(*r));
    return r;
}

static wchar_t *absolute_path(const wchar_t *path)
{
    DWORD n = GetFullPathNameW(path, 0, NULL, NULL), got;
    wchar_t *r;
    if (!n || n > KG_PATH_LIMIT) return NULL;
    r = (wchar_t *)malloc((size_t)n * sizeof(*r));
    if (!r) return NULL;
    got = GetFullPathNameW(path, n, r, NULL);
    if (!got || got >= n) { free(r); return NULL; }
    return r;
}

/* 1 = data; 0 = missing/unreadable/unsupported file; -1 = out of memory.
 * OPEN_REPARSE_POINT prevents opening the target of a file symlink. */
static int read_bounded(const wchar_t *path, char **data, size_t *length)
{
    HANDLE f;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    char *buffer;
    size_t used = 0;
    *data = NULL;
    *length = 0;
    f = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileInformationByHandle(f, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(f, &size) || size.QuadPart < 0 ||
        size.QuadPart > KG_FILE_LIMIT) {
        CloseHandle(f);
        return 0;
    }
    buffer = (char *)malloc((size_t)size.QuadPart + 1);
    if (!buffer) { CloseHandle(f); return -1; }
    while (used < (size_t)size.QuadPart) {
        DWORD got = 0;
        if (!ReadFile(f, buffer + used, (DWORD)((size_t)size.QuadPart - used), &got, NULL) || !got) {
            SecureZeroMemory(buffer, used);
            free(buffer);
            CloseHandle(f);
            return 0;
        }
        used += got;
    }
    CloseHandle(f);
    if (memchr(buffer, 0, used)) {
        SecureZeroMemory(buffer, used);
        free(buffer);
        return 0;
    }
    buffer[used] = 0;
    *data = buffer;
    *length = used;
    return 1;
}

static int hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Percent decoding is strict: malformed escapes and embedded NUL are rejected. */
static char *percent_decode(const char *input, size_t length)
{
    size_t i, out = 0;
    char *decoded = (char *)malloc(length + 1);
    if (!decoded) return NULL;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c == '%') {
            int hi, lo;
            if (i + 2 >= length || (hi = hex_digit((unsigned char)input[i + 1])) < 0 ||
                (lo = hex_digit((unsigned char)input[i + 2])) < 0) {
                free(decoded);
                return NULL;
            }
            c = (unsigned char)((hi << 4) | lo);
            i += 2;
        }
        if (!c) { free(decoded); return NULL; }
        decoded[out++] = (char)c;
    }
    decoded[out] = 0;
    return decoded;
}

static wchar_t *bytes_to_wide(const char *s, UINT codepage, DWORD flags)
{
    int n = MultiByteToWideChar(codepage, flags, s, -1, NULL, 0);
    wchar_t *r;
    if (!n || n > (int)KG_PATH_LIMIT) return NULL;
    r = (wchar_t *)malloc((size_t)n * sizeof(*r));
    if (!r) return NULL;
    if (!MultiByteToWideChar(codepage, flags, s, -1, r, n)) {
        free(r);
        return NULL;
    }
    return r;
}

static wchar_t *decode_component(const wchar_t *name, size_t n)
{
    int bytes;
    char *utf8, *decoded;
    wchar_t *r;
    if (!n || n > 32767) return NULL;
    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, (int)n,
                                NULL, 0, NULL, NULL);
    if (!bytes) return NULL;
    utf8 = (char *)malloc((size_t)bytes + 1);
    if (!utf8) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, (int)n,
                             utf8, bytes, NULL, NULL)) {
        free(utf8);
        return NULL;
    }
    utf8[bytes] = 0;
    decoded = percent_decode(utf8, (size_t)bytes);
    free(utf8);
    if (!decoded) return NULL;
    /* A percent-encoded slash inside one filename cannot round-trip through
     * Klink's folder/name syntax: accepting it would select another profile. */
    if (strchr(decoded, '/')) { free(decoded); return NULL; }
    r = bytes_to_wide(decoded, CP_UTF8, MB_ERR_INVALID_CHARS);
    free(decoded);
    return r;
}

static char *wide_to_utf8(const wchar_t *s)
{
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1,
                                    NULL, 0, NULL, NULL);
    char *r;
    if (!bytes) return NULL;
    r = (char *)malloc((size_t)bytes);
    if (!r) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, r, bytes, NULL, NULL)) {
        free(r);
        return NULL;
    }
    return r;
}

/* mode=0 is escape_registry_key followed by legacy_component for a final
 * session basename. mode=1 is legacy_component alone (the raw-name fallback).
 * The two stages must not be replaced by a generic URL encoder. */
static wchar_t *encode_load_basename(const char *s, int fallback)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t length = strlen(s), i, used = 0;
    char *bytes;
    wchar_t *out;
    if (length > KG_PATH_LIMIT * 4u) return NULL;
    bytes = (char *)malloc(length * 3 + 1);
    if (!bytes) return NULL;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)s[i];
        int escape = c < 32 || strchr("<>:\"/\\|", c) ||
            (c == '.' && (!i || i + 1 == length));
        if (!fallback && (c == ' ' || c > 126 || strchr("*?%", c))) escape = 1;
        if (escape) {
            bytes[used++] = '%';
            bytes[used++] = hex[c >> 4];
            bytes[used++] = hex[c & 15];
        } else bytes[used++] = (char)c;
    }
    bytes[used] = 0;
    out = bytes_to_wide(bytes, CP_UTF8, MB_ERR_INVALID_CHARS);
    free(bytes);
    return out;
}

static wchar_t *final_handle_path(HANDLE h)
{
    DWORD length = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED), got;
    wchar_t *out;
    if (!length || length >= KG_PATH_LIMIT) return NULL;
    out = (wchar_t *)malloc(((size_t)length + 1) * sizeof(*out));
    if (!out) return NULL;
    got = GetFinalPathNameByHandleW(h, out, length + 1, FILE_NAME_NORMALIZED);
    if (!got || got > length) { free(out); return NULL; }
    return out;
}

static int same_regular_file(const wchar_t *a, const wchar_t *b)
{
    HANDLE ha, hb;
    BY_HANDLE_FILE_INFORMATION ia, ib;
    int same = 0;
    ha = CreateFileW(a, FILE_READ_ATTRIBUTES,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (ha == INVALID_HANDLE_VALUE) return 0;
    hb = CreateFileW(b, FILE_READ_ATTRIBUTES,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (hb != INVALID_HANDLE_VALUE) {
        if (GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib) &&
            !((ia.dwFileAttributes | ib.dwFileAttributes) &
              (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
            ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow) {
            if (ia.nFileIndexHigh || ia.nFileIndexLow) same = 1;
            else {
                /* Some network filesystems do not provide a meaningful ID. */
                wchar_t *pa = final_handle_path(ha), *pb = final_handle_path(hb);
                if (pa && pb && CompareStringOrdinal(pa, -1, pb, -1, TRUE) == CSTR_EQUAL) same = 1;
                free(pa); free(pb);
            }
        }
        CloseHandle(hb);
    }
    CloseHandle(ha);
    return same;
}

static int matches_klink_file(const wchar_t *directory, const wchar_t *logical_leaf,
                              const wchar_t *physical_file, const wchar_t *suffix)
{
    char *utf8 = wide_to_utf8(logical_leaf), *encoded = NULL, *decoded = NULL;
    wchar_t *basename = NULL, *candidate = NULL, *fallback = NULL;
    DWORD attributes, error;
    size_t n, suffix_length;
    int matches = 0;
    if (!utf8) goto done;
    basename = encode_load_basename(utf8, 0);
    if (!basename) goto done;
    n = wcslen(basename);
    suffix_length = wcslen(suffix);
    if (suffix_length && (n < suffix_length || wcscmp(basename + n - suffix_length, suffix))) {
        wchar_t *with_suffix;
        if (n + suffix_length >= KG_PATH_LIMIT) goto done;
        with_suffix = (wchar_t *)realloc(basename, (n + suffix_length + 1) * sizeof(*basename));
        if (!with_suffix) goto done;
        basename = with_suffix;
        wcscat(basename, suffix);
    }
    candidate = join_path(directory, basename, L'\\');
    if (!candidate) goto done;
    attributes = GetFileAttributesW(candidate);
    error = GetLastError();
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        /* Existing canonical filename always wins, even if its contents are
         * not an SSH profile. Never accidentally load it through a raw alias. */
        matches = same_regular_file(candidate, physical_file);
        goto done;
    }
    if ((error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) ||
        !wcschr(basename, L'%')) goto done;
    encoded = wide_to_utf8(basename);
    if (!encoded) goto done;
    decoded = percent_decode(encoded, strlen(encoded));
    if (!decoded) goto done;
    fallback = encode_load_basename(decoded, 1);
    if (!fallback) goto done;
    free(candidate);
    candidate = join_path(directory, fallback, L'\\');
    if (candidate) matches = same_regular_file(candidate, physical_file);
done:
    free(utf8); free(encoded); free(decoded);
    free(basename); free(candidate); free(fallback);
    return matches;
}

static int reserved_name(const wchar_t *s)
{
    size_t n = wcscspn(s, L".");
    if (n == 3 &&
        (CompareStringOrdinal(s, 3, L"CON", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"PRN", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"AUX", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"NUL", 3, TRUE) == CSTR_EQUAL)) return 1;
    if (n == 4 && s[3] >= L'1' && s[3] <= L'9' &&
        (CompareStringOrdinal(s, 3, L"COM", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(s, 3, L"LPT", 3, TRUE) == CSTR_EQUAL)) return 1;
    return 0;
}

static wchar_t *display_label(const wchar_t *name)
{
    static const wchar_t hex[] = L"0123456789ABCDEF";
    /* This is a legacy bookmark alias, no longer one visible directory name.
     * Nested sessions may have a combined path longer than 255 characters. */
    wchar_t buffer[4096];
    size_t n = wcslen(name), tail = n, i, used = 0;
    int reserved = reserved_name(name) ||
        CompareStringOrdinal(name, -1, KG_SETTINGS_NAME, -1, TRUE) == CSTR_EQUAL;
    if (!n) return NULL;
    while (tail && (name[tail - 1] == L' ' || name[tail - 1] == L'.')) tail--;
    for (i = 0; i < n; i++) {
        wchar_t c = name[i];
        int escape = c < 32 || (c >= 127 && c <= 159) ||
            wcschr(L"%/\\:?*\"<>|", c) != NULL || i >= tail || (!i && reserved);
        if (escape) {
            unsigned char bytes[2];
            size_t k, count = 1;
            if (c <= 127) bytes[0] = (unsigned char)c;
            else { bytes[0] = 0xc0u | (unsigned char)(c >> 6);
                   bytes[1] = 0x80u | (unsigned char)(c & 63); count = 2; }
            if (used + count * 3 >= sizeof(buffer) / sizeof(buffer[0])) return NULL;
            for (k = 0; k < count; k++) {
                buffer[used++] = L'%';
                buffer[used++] = hex[bytes[k] >> 4];
                buffer[used++] = hex[bytes[k] & 15];
            }
        } else {
            if (used + 1 >= sizeof(buffer) / sizeof(buffer[0])) return NULL;
            buffer[used++] = c;
        }
    }
    buffer[used] = 0;
    return dup_wide(buffer);
}

static wchar_t *configured_path(const wchar_t *root, const char *value)
{
    wchar_t *wide = bytes_to_wide(value, CP_ACP, 0), *expanded, *joined, *full;
    DWORD n;
    if (!wide) return NULL;
    n = ExpandEnvironmentStringsW(wide, NULL, 0);
    if (!n || n > KG_PATH_LIMIT) { free(wide); return NULL; }
    expanded = (wchar_t *)malloc((size_t)n * sizeof(*expanded));
    if (!expanded) { free(wide); return NULL; }
    if (!ExpandEnvironmentStringsW(wide, expanded, n)) {
        free(wide); free(expanded); return NULL;
    }
    free(wide);
    /* Match KiTTY-ng configpath(): a leading backslash or drive prefix is
     * absolute; all other paths are relative to the portable root. */
    joined = expanded[0] == L'\\' || (expanded[0] && expanded[1] == L':') ?
        dup_wide(expanded) : join_path(root, expanded, L'\\');
    free(expanded);
    if (!joined) return NULL;
    full = absolute_path(joined);
    free(joined);
    return full;
}

static int resolve_sessions(const wchar_t *root, KgScan *scan, wchar_t **directory)
{
    wchar_t *ini = join_path(root, L"kitty.ini", L'\\');
    wchar_t *conf = join_path(root, L"putty.conf", L'\\');
    char *data = NULL, *line;
    size_t length = 0;
    int result;
    *directory = join_path(root, L"Sessions", L'\\');
    if (!ini || !conf || !*directory) {
        free(ini); free(conf);
        set_error(scan->error, scan->error_cap, L"Cannot allocate portable configuration paths.");
        return 0;
    }
    {
        DWORD attributes = GetFileAttributesW(ini);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            free(ini); free(conf);
            set_error(scan->error, scan->error_cap,
                      L"kitty.ini must be a regular file, not a directory or reparse point.");
            return 0;
        }
    }
    GetPrivateProfileStringW(L"KiTTY", L"fileextension", L"", scan->suffix,
                              64, ini);
    free(ini);
    result = read_bounded(conf, &data, &length);
    if (result < 0) {
        free(conf);
        set_error(scan->error, scan->error_cap, L"Out of memory reading putty.conf.");
        return 0;
    }
    if (!result) {
        DWORD attributes = GetFileAttributesW(conf);
        free(conf);
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            set_error(scan->error, scan->error_cap,
                      L"putty.conf is unreadable, too large, or a reparse point.");
            return 0;
        }
    } else {
        free(conf);
        line = data;
        while (line < data + length) {
            char *next = strchr(line, '\n'), *end, *equal;
            if (next) *next++ = 0;
            else next = data + length;
            end = line + strlen(line);
            while (end > line && (unsigned char)end[-1] <= 32) *--end = 0;
            equal = strchr(line, '=');
            if (equal && line[0] != ';') {
                *equal++ = 0;
                if (!strcmp(line, "Sessions")) {
                    wchar_t *newdir = configured_path(root, equal);
                    if (!newdir) {
                        SecureZeroMemory(data, length); free(data);
                        set_error(scan->error, scan->error_cap, L"Invalid Sessions path in putty.conf.");
                        return 0;
                    }
                    free(*directory);
                    *directory = newdir;
                } else if (!strcmp(line, "sessionsuffix")) {
                    wchar_t *suffix = bytes_to_wide(equal, CP_ACP, 0);
                    if (!suffix) {
                        SecureZeroMemory(data, length); free(data);
                        set_error(scan->error, scan->error_cap, L"Invalid sessionsuffix in putty.conf.");
                        return 0;
                    }
                    wcsncpy(scan->suffix, suffix, 63);
                    scan->suffix[63] = 0;
                    free(suffix);
                }
            }
            line = next;
        }
        SecureZeroMemory(data, length);
        free(data);
    }
    if (wcspbrk(scan->suffix, L"\\/:<>\"|")) scan->suffix[0] = 0;
    return 1;
}

/* Only these three non-secret values are interpreted. Password, proxy secrets,
 * private keys and other settings remain exclusively Klink's responsibility. */
static int read_profile(const wchar_t *path, wchar_t **host, int *port)
{
    char *data = NULL, *line, *hostname = NULL, *protocol = NULL, *portstr = NULL;
    size_t length;
    int r = read_bounded(path, &data, &length), valid = 1;
    *host = NULL;
    *port = 22;
    if (r != 1) return r;
    line = data;
    while (line < data + length) {
        char *next = strchr(line, '\n'), *split, *end, **target = NULL;
        if (next) *next++ = 0;
        else next = data + length;
        end = line + strlen(line);
        while (end > line && (end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
        split = strchr(line, '\\');
        if (split && end > split && end[-1] == '\\') {
            *split++ = 0;
            end[-1] = 0;
            if (!strcmp(line, "HostName")) target = &hostname;
            else if (!strcmp(line, "Protocol")) target = &protocol;
            else if (!strcmp(line, "PortNumber")) target = &portstr;
            if (target) {
                char *value = percent_decode(split, strlen(split));
                free(*target);
                *target = value;
                if (!value) valid = 0;
            }
        }
        line = next;
    }
    /* Missing protocol would be resolved through Default Settings by Klink;
     * only an explicit SSH profile is safe to list as a known SSH target. */
    if (!valid || !hostname || !*hostname || !protocol || strcmp(protocol, "ssh")) r = 0;
    else {
        const char *p;
        unsigned number = 0;
        if (portstr && *portstr) {
            for (p = portstr; *p; p++) {
                if (*p < '0' || *p > '9' || number > 6553) { valid = 0; break; }
                number = number * 10 + (unsigned)(*p - '0');
            }
            if (!number || number > 65535) valid = 0;
            if (valid) *port = (int)number;
        }
        /* Hostnames are metadata only; support the legacy ACP fallback used
         * by old KiTTY profiles without changing the -load session identity. */
        if (valid) {
            *host = bytes_to_wide(hostname, CP_UTF8, MB_ERR_INVALID_CHARS);
            if (!*host) *host = bytes_to_wide(hostname, CP_ACP, 0);
        }
        r = valid && *host ? 1 : 0;
    }
    free(hostname); free(protocol); free(portstr);
    SecureZeroMemory(data, length);
    free(data);
    return r;
}

static void free_profile(KgProfile *p)
{
    free(p->load_name);
    free(p->display_name);
    free(p->host);
    memset(p, 0, sizeof(*p));
}

static int append_profile(KgScan *scan, KgProfile *profile)
{
    if (scan->out->count == KG_PROFILE_LIMIT) {
        set_error(scan->error, scan->error_cap, L"More than 10000 SSH profiles; reduce the Sessions directory.");
        return 0;
    }
    if (scan->out->count == scan->capacity) {
        size_t cap = scan->capacity ? scan->capacity * 2 : 32;
        KgProfile *items;
        if (cap > KG_PROFILE_LIMIT) cap = KG_PROFILE_LIMIT;
        items = (KgProfile *)realloc(scan->out->items, cap * sizeof(*items));
        if (!items) {
            set_error(scan->error, scan->error_cap, L"Out of memory enumerating profiles.");
            return 0;
        }
        scan->out->items = items;
        scan->capacity = cap;
    }
    scan->out->items[scan->out->count++] = *profile;
    memset(profile, 0, sizeof(*profile));
    return 1;
}

static int scan_directory(KgScan *scan, const wchar_t *directory,
                           const wchar_t *prefix, unsigned depth)
{
    wchar_t *pattern;
    WIN32_FIND_DATAW entry;
    HANDLE find;
    DWORD error, attributes = GetFileAttributesW(directory);
    int ok = 1;
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        scan->skipped++;
        return 1;
    }
    pattern = join_path(directory, L"*", L'\\');
    if (!pattern) {
        set_error(scan->error, scan->error_cap, L"Sessions path is too long or memory is exhausted.");
        return 0;
    }
    find = FindFirstFileW(pattern, &entry);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES) scan->skipped++;
        return 1;
    }
    do {
        size_t n, suffixlen;
        wchar_t *component, *logical, *path;
        KgProfile profile = {0};
        int parsed;
        if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
        if (++scan->entries > KG_ENTRY_LIMIT) {
            set_error(scan->error, scan->error_cap, L"More than 100000 directory entries; reduce the Sessions directory.");
            ok = 0;
            break;
        }
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { scan->skipped++; continue; }
        n = wcslen(entry.cFileName);
        suffixlen = wcslen(scan->suffix);
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (n >= 4 && !wcscmp(entry.cFileName + n - 4, L".tmp")) continue;
            if (suffixlen && (n < suffixlen || wcscmp(entry.cFileName + n - suffixlen, scan->suffix))) continue;
            n -= suffixlen;
        }
        component = decode_component(entry.cFileName, n);
        if (!component) {
            scan->skipped++;
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcschr(entry.cFileName, L'%'))
                scan->folder_encoding_skipped++;
            continue;
        }
        /* KiTTY-ng unescapes intermediate -load components before resolving
         * them on disk, but keeps the encoded final filename. Thus a physical
         * folder "Group%20A" cannot be addressed by the decoded "Group A".
         * Never let enumeration silently select a different saved server. */
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(component, entry.cFileName)) {
            scan->skipped++;
            scan->folder_encoding_skipped++;
            free(component);
            continue;
        }
        logical = join_path(prefix, component, L'/');
        free(component);
        path = join_path(directory, entry.cFileName, L'\\');
        if (!logical || !path) {
            free(logical); free(path);
            set_error(scan->error, scan->error_cap, L"Session path is too long or memory is exhausted.");
            ok = 0;
            break;
        }
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth >= KG_DEPTH_LIMIT) scan->skipped++;
            else ok = scan_directory(scan, path, logical, depth + 1);
            free(logical); free(path);
            if (!ok) break;
            continue;
        }
        {
            const wchar_t *leaf = wcsrchr(logical, L'/');
            leaf = leaf ? leaf + 1 : logical;
            if (CompareStringOrdinal(leaf, -1, L"Default Settings", -1, TRUE) == CSTR_EQUAL) {
                free(logical); free(path); continue;
            }
            if (!matches_klink_file(directory, leaf, path, scan->suffix)) {
                scan->skipped++;
                free(logical); free(path); continue;
            }
        }
        profile.load_name = logical;
        profile.display_name = display_label(logical);
        if (!profile.display_name) { scan->skipped++; free_profile(&profile); free(path); continue; }
        parsed = read_profile(path, &profile.host, &profile.port);
        free(path);
        if (parsed < 0) {
            free_profile(&profile);
            set_error(scan->error, scan->error_cap, L"Out of memory reading a session profile.");
            ok = 0;
            break;
        }
        if (!parsed) { scan->skipped++; free_profile(&profile); continue; }
        ok = append_profile(scan, &profile);
        free_profile(&profile);
        if (!ok) break;
    } while (FindNextFileW(find, &entry));
    error = GetLastError();
    FindClose(find);
    if (ok && error != ERROR_NO_MORE_FILES) scan->skipped++;
    return ok;
}

static int compare_profiles(const void *va, const void *vb)
{
    const KgProfile *a = (const KgProfile *)va, *b = (const KgProfile *)vb;
    int cmp = CompareStringOrdinal(a->display_name, -1, b->display_name, -1, TRUE);
    if (cmp == CSTR_LESS_THAN) return -1;
    if (cmp == CSTR_GREATER_THAN) return 1;
    return wcscmp(a->display_name, b->display_name);
}

static void remove_ambiguous(KgScan *scan)
{
    size_t i = 0, used = 0;
    if (scan->out->count > 1)
        qsort(scan->out->items, scan->out->count, sizeof(KgProfile), compare_profiles);
    while (i < scan->out->count) {
        size_t end = i + 1;
        while (end < scan->out->count &&
               CompareStringOrdinal(scan->out->items[i].display_name, -1,
                                    scan->out->items[end].display_name, -1, TRUE) == CSTR_EQUAL) end++;
        if (end - i > 1) {
            while (i < end) { free_profile(&scan->out->items[i++]); scan->skipped++; }
        } else {
            if (used != i) {
                scan->out->items[used] = scan->out->items[i];
                memset(&scan->out->items[i], 0, sizeof(KgProfile));
            }
            used++;
            i++;
        }
    }
    scan->out->count = used;
}

int kg_profiles_load(const wchar_t *portable_root, KgProfiles *out,
                     wchar_t *error, size_t error_cap)
{
    KgScan scan;
    wchar_t *root, *directory = NULL;
    DWORD attributes;
    int ok;
    set_error(error, error_cap, L"");
    if (!out || !portable_root || !*portable_root) {
        set_error(error, error_cap, L"Specify the portable KiTTY-ng directory.");
        return 0;
    }
    memset(out, 0, sizeof(*out));
    memset(&scan, 0, sizeof(scan));
    scan.out = out;
    scan.error = error;
    scan.error_cap = error_cap;
    root = absolute_path(portable_root);
    if (!root) { set_error(error, error_cap, L"Invalid portable KiTTY-ng path."); return 0; }
    attributes = GetFileAttributesW(root);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        free(root);
        set_error(error, error_cap, L"The portable root must be an existing regular directory.");
        return 0;
    }
    ok = resolve_sessions(root, &scan, &directory);
    free(root);
    if (ok) ok = scan_directory(&scan, directory, L"", 0);
    free(directory);
    if (!ok) { kg_profiles_free(out); return 0; }
    remove_ambiguous(&scan);
    if (scan.skipped && error && error_cap) {
        int n = swprintf(error, error_cap,
            L"Skipped %lu entries (%lu folders use unsupported %%XX directory names). "
            L"Check canonical storage names, readable SSH profiles, UTF-8, unique session paths and nesting <=16.",
            (unsigned long)scan.skipped, (unsigned long)scan.folder_encoding_skipped);
        if (n < 0 || (size_t)n >= error_cap) error[error_cap - 1] = 0;
    }
    return 1;
}

void kg_profiles_free(KgProfiles *profiles)
{
    size_t i;
    if (!profiles) return;
    for (i = 0; i < profiles->count; i++) free_profile(&profiles->items[i]);
    free(profiles->items);
    memset(profiles, 0, sizeof(*profiles));
}

int kg_find_tool(const wchar_t *root, int gui, wchar_t *out, size_t cap)
{
    HANDLE file;
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER header;
    LARGE_INTEGER size, offset;
    BY_HANDLE_FILE_INFORMATION info;
    DWORD got, signature;
    wchar_t *path;
    size_t length;
    int valid;
#if defined(_WIN64)
    const WORD machine = IMAGE_FILE_MACHINE_AMD64;
    const wchar_t *name = gui ? L"kitty-ng-x64.exe" : L"klink-x64.exe";
#else
    const WORD machine = IMAGE_FILE_MACHINE_I386;
    const wchar_t *name = gui ? L"kitty-ng-x86.exe" : L"klink-x86.exe";
#endif
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!root || !*root) return 0;
    path = join_path(root, name, L'\\');
    if (!path) return 0;
    length = wcslen(path);
    if (length >= cap) { free(path); return 0; }
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(path); return 0; }
    valid = GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        GetFileSizeEx(file, &size) &&
        ReadFile(file, &dos, sizeof(dos), &got, NULL) && got == sizeof(dos) &&
        dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= (LONG)sizeof(dos) &&
        dos.e_lfanew <= 1024 * 1024 &&
        (LONGLONG)dos.e_lfanew + 4 + (LONGLONG)sizeof(header) <= size.QuadPart;
    offset.QuadPart = valid ? dos.e_lfanew : 0;
    valid = valid && SetFilePointerEx(file, offset, NULL, FILE_BEGIN) &&
        ReadFile(file, &signature, sizeof(signature), &got, NULL) && got == sizeof(signature) &&
        signature == IMAGE_NT_SIGNATURE &&
        ReadFile(file, &header, sizeof(header), &got, NULL) && got == sizeof(header) &&
        header.Machine == machine &&
        (header.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) &&
        !(header.Characteristics & IMAGE_FILE_DLL);
    CloseHandle(file);
    if (valid) memcpy(out, path, (length + 1) * sizeof(*out));
    free(path);
    return valid;
}
