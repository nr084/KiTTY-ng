/* Session identifiers stay unchanged; only the visible path is shortened. */
#ifndef SESSION_BROWSER_H
#define SESSION_BROWSER_H
#include <ctype.h>

static char *session_browser_parent(const char *folder)
{
    char *parent = dupstr(folder);
    char *slash = strrchr(parent, '/');
    if (slash) *slash = 0; else *parent = 0;
    return parent;
}

static const char *session_browser_relative(const char *folder, const char *name)
{
    size_t n = strlen(folder);
    if (!n) return name;
    if (strncmp(folder, name, n) || name[n] != '/') return NULL;
    return name + n + 1;
}

static bool session_browser_match(const char *name, const char *query)
{
    if (!*query) return true;
#ifdef _WIN32
    /* Portable identifiers and new edit-box text are UTF-8; retain support
     * for existing registry identifiers in the Windows legacy code page. */
    UINT name_cp = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0) ? CP_UTF8 : CP_ACP;
    UINT query_cp = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, query, -1, NULL, 0) ? CP_UTF8 : CP_ACP;
    wchar_t *haystack = dup_mb_to_wc(name_cp, name);
    wchar_t *needle = dup_mb_to_wc(query_cp, query);
    size_t hn = wcslen(haystack), nn = wcslen(needle);
    bool found = false;
    for (size_t i = 0; nn <= hn && i <= hn - nn; i++) {
        if (CompareStringOrdinal(haystack + i, nn, needle, nn, TRUE) == CSTR_EQUAL) {
            found = true;
            break;
        }
    }
    sfree(haystack); sfree(needle);
    return found;
#else
    size_t n = strlen(query);
    for (; *name; name++) {
        size_t i = 0;
        while (i < n && name[i] &&
               tolower((unsigned char)name[i]) == tolower((unsigned char)query[i])) i++;
        if (i == n) return true;
    }
    return false;
#endif
}
#endif
