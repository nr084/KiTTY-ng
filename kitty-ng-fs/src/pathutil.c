#include "pathutil.h"
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>

wchar_t *kg_wdup(const wchar_t *s)
{
    size_t n = wcslen(s) + 1;
    wchar_t *p = malloc(n * sizeof(*p));
    if (p) memcpy(p, s, n * sizeof(*p));
    return p;
}

char *kg_utf8(const wchar_t *s)
{
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL, 0, NULL, NULL);
    char *p;
    if (!n || !(p = malloc((size_t)n))) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, p, n, NULL, NULL)) { free(p); return NULL; }
    return p;
}

wchar_t *kg_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *p;
    if (!n || !(p = malloc((size_t)n * sizeof(*p)))) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n)) { free(p); return NULL; }
    return p;
}

wchar_t *kg_local_path(const wchar_t *s)
{
    DWORD n;
    wchar_t *full, *result;
    size_t len;
    if (!wcsncmp(s, L"\\\\?\\", 4)) {
        int drive = ((s[4] >= L'A' && s[4] <= L'Z') || (s[4] >= L'a' && s[4] <= L'z')) &&
                    s[5] == L':' && s[6] == L'\\';
        if (!drive && _wcsnicmp(s + 4, L"UNC\\", 4)) return NULL;
        return kg_wdup(s);
    }
    /* Device namespaces are never a transfer target. */
    if (!wcsncmp(s, L"\\\\.\\", 4)) return NULL;
    n = GetFullPathNameW(s, 0, NULL, NULL);
    if (!n || n > 32750) return NULL;
    full = malloc(((size_t)n + 1) * sizeof(*full));
    if (!full) return NULL;
    if (!GetFullPathNameW(s, n + 1, full, NULL)) { free(full); return NULL; }
    len = wcslen(full);
    result = malloc((len + 9) * sizeof(*result));
    if (result) {
        if (!wcsncmp(full, L"\\\\", 2)) { wcscpy(result, L"\\\\?\\UNC\\"); wcscat(result, full + 2); }
        else { wcscpy(result, L"\\\\?\\"); wcscat(result, full); }
    }
    free(full);
    return result;
}

static size_t utf8_unit(const unsigned char *s, size_t left)
{
    size_t n, i;
    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xc2 && s[0] <= 0xdf) n = 2;
    else if (s[0] >= 0xe0 && s[0] <= 0xef) n = 3;
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) n = 4;
    else return 0;
    if (left < n) return 0;
    for (i = 1; i < n; ++i) if ((s[i] & 0xc0) != 0x80) return 0;
    if (n == 3 && ((s[0] == 0xe0 && s[1] < 0xa0) || (s[0] == 0xed && s[1] >= 0xa0))) return 0;
    if (n == 4 && ((s[0] == 0xf0 && s[1] < 0x90) || (s[0] == 0xf4 && s[1] >= 0x90))) return 0;
    return n;
}

static int dos_reserved(const char *s)
{
    char b[5] = {0};
    size_t n = strcspn(s, ".");
    size_t i;
    if (n < 3 || n > 4) return 0;
    for (i = 0; i < n; ++i) b[i] = (char)toupper((unsigned char)s[i]);
    return !strcmp(b, "CON") || !strcmp(b, "PRN") || !strcmp(b, "AUX") || !strcmp(b, "NUL") ||
           (n == 4 && (!memcmp(b, "COM", 3) || !memcmp(b, "LPT", 3)) && b[3] >= '1' && b[3] <= '9');
}

int kg_display_name(const char *raw, wchar_t *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *s = (const unsigned char *)raw;
    size_t n = strlen(raw), i = 0;
    wchar_t *wide;
    int escape = dos_reserved(raw) || !strncmp(raw, "~kg~", 4);
    if (!n || n > 16384 || !cap) return 0;
    if (s[n - 1] == '.' || s[n - 1] == ' ') escape = 1;
    while (i < n) {
        size_t u = utf8_unit(s + i, n - i);
        if (!u || s[i] < 32 || s[i] == 127 || strchr("\\/:*?\"<>|", s[i])) escape = 1;
        i += u ? u : 1;
    }
    if (escape) {
        if (n * 2 + 4 >= cap) return 0;
        wcscpy(out, L"~kg~");
        for (i = 0; i < n; ++i) { out[4 + 2*i] = hex[s[i] >> 4]; out[5 + 2*i] = hex[s[i] & 15]; }
        out[4 + 2*n] = 0; return 1;
    }
    wide = kg_wide(raw);
    if (!wide) return 0;
    if (wcslen(wide) >= cap) { free(wide); return 0; }
    wcscpy(out, wide); free(wide); return 1;
}

static int unhex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char *kg_remote_path(const char *base, const wchar_t *relative)
{
    char *utf = kg_utf8(relative), *out;
    size_t cap, at, i = 0, component;
    if (!utf) return NULL;
    cap = strlen(base) + strlen(utf) + 3;
    if (cap > 32768 || !(out = malloc(cap))) { free(utf); return NULL; }
    strcpy(out, base); at = strlen(base);
    while (at > 1 && out[at - 1] == '/') --at;
    while (utf[i] == '\\') ++i;
    while (utf[i]) {
        size_t input_start, input_len;
        int encoded;
        if (!at || out[at - 1] != '/') out[at++] = '/';
        component = at;
        input_start = i;
        encoded = !strncmp(utf + i, "~kg~", 4);
        if (encoded) i += 4;
        while (utf[i] && utf[i] != '\\') {
            unsigned char c = (unsigned char)utf[i++];
            if (encoded) {
                int a, b;
                if (!utf[i] || (a = unhex(c)) < 0 || (b = unhex((unsigned char)utf[i])) < 0) goto fail;
                c = (unsigned char)((a << 4) | b); ++i;
            }
            if (!c || c == '/') goto fail;
            out[at++] = (char)c;
        }
        if (at == component || (at == component + 1 && out[component] == '.') ||
            (at == component + 2 && out[component] == '.' && out[component + 1] == '.')) goto fail;
        input_len = i - input_start;
        out[at] = 0;
        {
            wchar_t canonical[1024];
            char *canonical_utf8;
            int valid;
            if (!kg_display_name(out + component, canonical, 1024)) goto fail;
            canonical_utf8 = kg_utf8(canonical);
            if (!canonical_utf8) goto fail;
            valid = strlen(canonical_utf8) == input_len && !memcmp(canonical_utf8, utf + input_start, input_len);
            free(canonical_utf8);
            if (!valid) goto fail;
        }
        if (utf[i] == '\\') { ++i; if (utf[i] == '\\') goto fail; }
    }
    out[at] = 0; free(utf); return out;
fail:
    free(utf); free(out); return NULL;
}

int kg_random_hex(char out[33])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char data[16];
    size_t i;
    if (BCryptGenRandom(NULL, data, sizeof(data), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 0;
    for (i = 0; i < sizeof(data); ++i) { out[2*i] = hex[data[i] >> 4]; out[2*i+1] = hex[data[i] & 15]; }
    out[32] = 0; return 1;
}

FILETIME kg_filetime(uint32_t seconds)
{
    ULARGE_INTEGER u;
    FILETIME ft;
    u.QuadPart = ((uint64_t)seconds + UINT64_C(11644473600)) * UINT64_C(10000000);
    ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    return ft;
}

int kg_unix_time(const FILETIME *ft, uint32_t *seconds)
{
    ULARGE_INTEGER u;
    uint64_t t;
    u.LowPart = ft->dwLowDateTime; u.HighPart = ft->dwHighDateTime;
    t = u.QuadPart / UINT64_C(10000000);
    if (t < UINT64_C(11644473600) || t - UINT64_C(11644473600) > UINT32_MAX) return 0;
    *seconds = (uint32_t)(t - UINT64_C(11644473600)); return 1;
}

static int append(wchar_t *out, size_t cap, size_t *at, wchar_t c)
{
    if (*at + 1 >= cap) return 0;
    out[(*at)++] = c; out[*at] = 0; return 1;
}

int kg_quote_arg(wchar_t *out, size_t cap, size_t *at, const wchar_t *arg)
{
    size_t i = 0, slash;
    if (*at && !append(out, cap, at, L' ')) return 0;
    if (!append(out, cap, at, L'"')) return 0;
    for (;;) {
        slash = 0;
        while (arg[i] == L'\\') { ++slash; ++i; }
        if (arg[i] == 0 || arg[i] == L'"') slash *= 2;
        while (slash--) if (!append(out, cap, at, L'\\')) return 0;
        if (!arg[i]) break;
        if (arg[i] == L'"' && !append(out, cap, at, L'\\')) return 0;
        if (!append(out, cap, at, arg[i++])) return 0;
    }
    return append(out, cap, at, L'"');
}
