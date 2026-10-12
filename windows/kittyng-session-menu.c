/* Hierarchical menu; command IDs still address the original session list. */
#include "putty.h"
#include "kittyng-session-menu.h"
#include "kittyng.h"

typedef struct SessionMenuEntry {
    char *name;
    wchar_t *wide;
    bool folder;
    int index;
} SessionMenuEntry;

static int compare_entry(const void *a, const void *b)
{
    const SessionMenuEntry *x = a, *y = b;
    if (x->folder != y->folder) return x->folder ? -1 : 1;
    int order = CompareStringOrdinal(x->wide, -1, y->wide, -1, TRUE);
    if (order == CSTR_EQUAL) order = CompareStringOrdinal(x->wide, -1, y->wide, -1, FALSE);
    return order == CSTR_LESS_THAN ? -1 : order == CSTR_GREATER_THAN ? 1 : 0;
}

static wchar_t *menu_label(const wchar_t *name)
{
    wchar_t *label = snewn(2 * wcslen(name) + 1, wchar_t), *out = label;
    while (*name) { if (*name == L'&') *out++ = L'&'; *out++ = *name++; }
    *out = 0; return label;
}

static void populate(HMENU menu, const struct sesslist *sessions,
                     const char *prefix, UINT first, UINT step, int limit, int depth)
{
    SessionMenuEntry *entries = snewn(limit, SessionMenuEntry);
    int count = 0; size_t length = strlen(prefix);
    for (int i = 1; i < sessions->nsessions && i <= limit; i++) {
        const char *session = sessions->sessions[i];
        if (strncmp(session, prefix, length)) continue;
        const char *tail = session + length;
        if (!*tail) continue;
        const char *slash = depth < 128 ? strchr(tail, '/') : NULL;
        size_t size = slash ? (size_t)(slash - tail) : strlen(tail);
        if (!size) continue;
        char *name = snewn(size + 1, char); memcpy(name, tail, size); name[size] = 0;
        bool duplicate = false;
        if (slash) for (int j = 0; j < count; j++)
            if (entries[j].folder && !strcmp(entries[j].name, name)) { duplicate = true; break; }
        if (duplicate) { sfree(name); continue; }
        entries[count++] = (SessionMenuEntry){name, dup_mb_to_wc(CP_UTF8, name), slash != NULL, i};
    }
    qsort(entries, count, sizeof(*entries), compare_entry);
    for (int i = 0; i < count; i++) {
        SessionMenuEntry *entry = &entries[i];
        wchar_t *label = menu_label(entry->wide);
        if (entry->folder) {
            HMENU child = CreatePopupMenu();
            char *next = dupcat(prefix, entry->name, "/");
            if (child) {
                populate(child, sessions, next, first, step, limit, depth + 1);
                if (!AppendMenuW(menu, MF_POPUP, (UINT_PTR)child, label)) DestroyMenu(child);
            }
            sfree(next);
        } else AppendMenuW(menu, MF_STRING, first + (entry->index - 1) * step, label);
        sfree(label); sfree(entry->wide); sfree(entry->name);
    }
    sfree(entries);
}

void kr_saved_sessions_menu(HMENU menu, const struct sesslist *sessions,
                            UINT first, UINT step, int limit)
{
    /* RemoveMenu detaches submenus; explicitly release every old tree. */
    while (GetMenuItemCount(menu) > 0) {
        HMENU child = GetSubMenu(menu, 0);
        if (!RemoveMenu(menu, 0, MF_BYPOSITION)) break;
        if (child) DestroyMenu(child);
    }
    populate(menu, sessions, "", first, step, limit, 0);
    if (!GetMenuItemCount(menu)) kr_append_menu(menu, MF_GRAYED, first, "(No sessions)");
}
