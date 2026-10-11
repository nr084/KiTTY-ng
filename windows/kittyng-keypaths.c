/* Portable session key references use the package root, never process CWD. */
#include "putty.h"
#include "kittyng-portable.h"
#include "kittyng-keypaths.h"

bool kr_key_path_setting(const char *name)
{
    return !strcmp(name, "PublicKeyFile") || !strcmp(name, "DetachedCertificate");
}

static wchar_t *full_path(const wchar_t *path)
{
    wchar_t *input = dupwcs(path);
    for (wchar_t *p = input; *p; p++) if (*p == L'/') *p = L'\\';
    DWORD size = GetFullPathNameW(input, 0, NULL, NULL);
    wchar_t *result = size ? snewn(size, wchar_t) : NULL;
    DWORD written = result ? GetFullPathNameW(input, size, result, NULL) : 0;
    sfree(input);
    if (!written || written >= size) { sfree(result); return NULL; }
    return result;
}

char *kr_portable_save_key_path(const Filename *filename)
{
    const wchar_t *root = kr_portable_root();
    if (!root || filename_is_null(filename)) return dupstr(filename->cpath);
    wchar_t *base = full_path(root), *path = full_path(filename->wpath);
    if (!base || !path) {
        sfree(base); sfree(path); return dupstr(filename->cpath);
    }
    size_t n = wcslen(base), length = wcslen(path);
    while (n && base[n - 1] == L'\\') n--;
    const wchar_t *stored = path;
    if (length > n + 1 && path[n] == L'\\' &&
        CompareStringOrdinal(base, n, path, n, TRUE) == CSTR_EQUAL)
        stored = path + n + 1;
    /* Keep the original KiTTY/PuTTY filename value encoding. */
    Filename *value = filename_from_wstr(stored);
    char *result = dupstr(value->cpath);
    filename_free(value); sfree(base); sfree(path);
    return result;
}

Filename *kr_portable_load_key_path(const char *stored)
{
    Filename *value = filename_from_str(stored);
    const wchar_t *root = kr_portable_root(), *path = value->wpath;
    if (!root || !*path || path[0] == L'\\' || path[0] == L'/' ||
        (path[0] && path[1] == L':')) return value;
    size_t n = wcslen(root);
    wchar_t *joined = snewn(n + wcslen(path) + 2, wchar_t);
    wcscpy(joined, root);
    if (n && joined[n - 1] != L'\\' && joined[n - 1] != L'/') wcscat(joined, L"\\");
    wcscat(joined, path);
    wchar_t *absolute = full_path(joined);
    Filename *result = filename_from_wstr(absolute ? absolute : joined);
    sfree(absolute); sfree(joined); filename_free(value);
    return result;
}
