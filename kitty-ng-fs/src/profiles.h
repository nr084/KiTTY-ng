#ifndef KG_PROFILES_H
#define KG_PROFILES_H

#include <stddef.h>
#include <wchar.h>

/* Reserved WFX root entry; matching profile labels escape the opening '['. */
#define KG_SETTINGS_NAME L"[\x041D\x0430\x0441\x0442\x0440\x043E\x0439\x043A\x0438 KiTTY-ng]"

/* All strings are owned by the array and released by kg_profiles_free(). */
typedef struct KgProfile {
    wchar_t *load_name;    /* UTF-16 logical name passed unchanged to Klink -load. */
    wchar_t *display_name; /* Legacy flat bookmark alias; the tree displays components. */
    wchar_t *host;
    int port;
} KgProfile;

typedef struct KgProfiles {
    KgProfile *items;
    size_t count;
} KgProfiles;

/* Returns 1 on success (including an empty list), 0 on failure.
 * On success, error may contain a warning about skipped profiles.
 * `out` must be zero-initialized or previously freed. No global state is used. */
int kg_profiles_load(const wchar_t *portable_root, KgProfiles *out,
                     wchar_t *error, size_t error_cap);
void kg_profiles_free(KgProfiles *profiles);

/* gui=0: klink-{x86,x64}.exe; gui!=0: kitty-ng-{x86,x64}.exe.
 * Exact filename only. Validates a regular PE executable of this build's
 * architecture. Never searches PATH or the registry. */
int kg_find_tool(const wchar_t *root, int gui, wchar_t *out, size_t cap);

#endif
