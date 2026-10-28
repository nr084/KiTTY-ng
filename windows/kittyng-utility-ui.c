/* Localised utility presentation. Key data and network streams stay raw. */
#include "putty.h"
#include "kittyng.h"
#include "kittyng-theme.h"
#include <io.h>

#define LOCALIZE_MESSAGE (WM_APP + 110)
#define LABEL_PROPERTY L"KiTTYNG.UtilityLabel"
#define RAW_PROPERTY L"KiTTYNG.UtilityRaw"

static void remember_label(HWND hwnd, const char *text)
{
    char *old = (char *)RemovePropW(hwnd, LABEL_PROPERTY);
    sfree(old);
    char *copy = dupstr(text);
    if (!SetPropW(hwnd, LABEL_PROPERTY, copy)) sfree(copy);
}

static BOOL CALLBACK forget_label(HWND child, LPARAM unused)
{
    sfree((char *)RemovePropW(child, LABEL_PROPERTY));
    return TRUE;
}

static UINT text_codepage(const char *text)
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0)
        ? CP_UTF8 : CP_ACP;
}

static char *english_label(HWND hwnd)
{
    int length = GetWindowTextLengthW(hwnd);
    wchar_t *wide = snewn(length + 1, wchar_t);
    GetWindowTextW(hwnd, wide, length + 1);
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    char *text = snewn(size, char);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, size, NULL, NULL);
    sfree(wide);
    const char *english = kr_untranslate(text);
    if (english != text) { char *copy = dupstr(english); sfree(text); return copy; }
    return text;
}

static BOOL CALLBACK translate_control(HWND child, LPARAM unused)
{
    if (GetPropW(child, RAW_PROPERTY)) return TRUE;
    wchar_t classname[32] = {0};
    GetClassNameW(child, classname, lenof(classname));
    /* Never rewrite comments, passphrases, fingerprints, or displayed keys. */
    if (_wcsicmp(classname, L"Static") && _wcsicmp(classname, L"Button")) return TRUE;
    char *saved = (char *)GetPropW(child, LABEL_PROPERTY);
    char *english = saved ? dupstr(saved) : english_label(child);
    if (!saved) remember_label(child, english);
    const char *text = kr_translate(english);
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, text);
    SetWindowTextW(child, wide);
    sfree(wide); sfree(english);
    return TRUE;
}

void kr_utility_localize(HWND hwnd)
{
    char *saved = (char *)GetPropW(hwnd, LABEL_PROPERTY);
    char *title = saved ? dupstr(saved) : english_label(hwnd);
    if (!saved) remember_label(hwnd, title);
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, kr_translate(title));
    SetWindowTextW(hwnd, wide);
    sfree(wide); sfree(title);
    EnumChildWindows(hwnd, translate_control, 0);
    kr_relocalize_menu(GetMenu(hwnd));
    DrawMenuBar(hwnd);
    kr_theme_attach(hwnd);
}

bool kr_utility_dialog_event(HWND hwnd, UINT message)
{
    if (message == WM_DESTROY) {
        EnumChildWindows(hwnd, forget_label, 0);
        forget_label(hwnd, 0);
        kr_utility_free_menu(GetMenu(hwnd));
    }
    if (message == WM_INITDIALOG) { kr_theme_attach(hwnd); PostMessage(hwnd, LOCALIZE_MESSAGE, 0, 0); }
    if (message != LOCALIZE_MESSAGE) return false;
    kr_utility_localize(hwnd);
    return true;
}

BOOL kr_utility_set_item_text(HWND hwnd, int id, const char *text)
{
    if (!text) text = "";
    HWND child = GetDlgItem(hwnd, id);
    if (!child) return FALSE;
    wchar_t classname[32] = {0};
    GetClassNameW(child, classname, lenof(classname));
    if (!_wcsicmp(classname, L"Edit") && !(GetWindowLongPtr(child, GWL_STYLE) & ES_READONLY))
        return SetDlgItemTextA(hwnd, id, text);
    if (!_wcsicmp(classname, L"Static") || !_wcsicmp(classname, L"Button"))
        remember_label(child, text);
    const char *translated = kr_translate(text);
    wchar_t *wide = dup_mb_to_wc(text_codepage(translated), translated);
    BOOL ok = SetWindowTextW(child, wide);
    sfree(wide);
    return ok;
}

BOOL kr_utility_set_raw_item_text(HWND hwnd, int id, const char *text)
{
    HWND child = GetDlgItem(hwnd, id);
    if (!child || !SetPropW(child, RAW_PROPERTY, (HANDLE)1)) return FALSE;
    return SetDlgItemTextA(hwnd, id, text);
}

int kr_utility_combo_add(HWND hwnd, int id, const char *text)
{
    const char *translated = kr_translate(text);
    wchar_t *wide = dup_mb_to_wc(text_codepage(translated), translated);
    int result = SendDlgItemMessageW(hwnd, id, CB_ADDSTRING, 0, (LPARAM)wide);
    sfree(wide);
    return result;
}

void kr_relocalize_menu(HMENU menu)
{
    for (int i = 0; menu && i < GetMenuItemCount(menu); i++) {
        wchar_t label[2048] = {0};
        MENUITEMINFOW item = {0};
        item.cbSize = sizeof(item); item.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_DATA;
        item.dwTypeData = label; item.cch = lenof(label);
        if (!GetMenuItemInfoW(menu, i, TRUE, &item)) continue;
        if (item.hSubMenu) kr_relocalize_menu(item.hSubMenu);
        char *utf8 = dup_wc_to_mb(CP_UTF8, label, "");
        const char *text = kr_translate(item.dwItemData ? (const char *)item.dwItemData : kr_untranslate(utf8));
        wchar_t *wide = dup_mb_to_wc(CP_UTF8, text);
        item.fMask = MIIM_STRING; item.dwTypeData = wide;
        SetMenuItemInfoW(menu, i, TRUE, &item);
        sfree(wide); sfree(utf8);
    }
}

/* Only fixed utility menu items use dwItemData; preserve their English text. */
BOOL kr_utility_append_menu(HMENU menu, UINT flags, UINT_PTR id, const char *text)
{
    if (!kr_append_menu(menu, flags, id, text)) return FALSE;
    if (!text || flags & MF_SEPARATOR) return TRUE;
    MENUITEMINFOW item = {0}; item.cbSize = sizeof(item); item.fMask = MIIM_DATA;
    char *copy = dupstr(text); item.dwItemData = (ULONG_PTR)copy;
    if (!SetMenuItemInfoW(menu, GetMenuItemCount(menu) - 1, TRUE, &item)) sfree(copy);
    return TRUE;
}

void kr_utility_free_menu(HMENU menu)
{
    for (int i = 0; menu && i < GetMenuItemCount(menu); i++) {
        MENUITEMINFOW item = {0}; item.cbSize = sizeof(item); item.fMask = MIIM_DATA | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(menu, i, TRUE, &item)) continue;
        if (item.hSubMenu) kr_utility_free_menu(item.hSubMenu);
        sfree((char *)item.dwItemData);
        item.dwItemData = 0; item.fMask = MIIM_DATA;
        SetMenuItemInfoW(menu, i, TRUE, &item);
    }
}

static HMENU language_menu(HMENU menu, int base)
{
    for (int i = 0; menu && i < GetMenuItemCount(menu); i++) {
        if (GetMenuItemID(menu, i) == (UINT)base) return menu;
        HMENU found = language_menu(GetSubMenu(menu, i), base);
        if (found) return found;
    }
    return NULL;
}

void kr_add_utility_language_menu(HWND hwnd, int base)
{
    HMENU language = CreatePopupMenu();
    AppendMenuW(language, MF_STRING, base, L"English");
    AppendMenuW(language, MF_STRING, base + 1, L"\x0420\x0443\x0441\x0441\x043a\x0438\x0439");
    kr_utility_append_menu(GetMenu(hwnd), MF_POPUP, (UINT_PTR)language, "Language");
    CheckMenuRadioItem(language_menu(GetMenu(hwnd), base), base, base + 1, base + kr_language(), MF_BYCOMMAND);
}

bool kr_utility_language_command(HWND hwnd, int base, int command)
{
    if (command != base && command != base + 1) return false;
    if (!kr_set_language(command - base)) {
        kr_messagebox(hwnd, "Unable to save the interface language.", "Error", MB_OK | MB_ICONERROR);
        return true;
    }
    kr_utility_localize(hwnd);
    CheckMenuRadioItem(language_menu(GetMenu(hwnd), base), base, base + 1, base + kr_language(), MF_BYCOMMAND);
    return true;
}

bool kr_language_arg(const char *option, const char *value)
{
    return !strcmp(option, "--language") && value &&
        (!strcmp(value, "en") || !strcmp(value, "ru"));
}

static int console_write(FILE *stream, const char *text, bool utf8)
{
    size_t len = strlen(text);
    HANDLE output = (HANDLE)_get_osfhandle(_fileno(stream));
    DWORD mode;
    if ((utf8 || text_codepage(text) == CP_UTF8) && GetConsoleMode(output, &mode)) {
        fflush(stream);
        wchar_t *wide = dup_mb_to_wc(CP_UTF8, text);
        DWORD written;
        size_t pos = 0, count = wcslen(wide);
        while (pos < count && WriteConsoleW(output, wide + pos, count - pos, &written, NULL) && written)
            pos += written;
        sfree(wide);
        if (pos < count) return -1;
    } else if (fwrite(text, 1, len, stream) != len) return -1;
    return len;
}

static int console_vprintf(FILE *stream, const char *format, va_list ap)
{
    const char *translated = kr_translate(format);
    char *text = dupvprintf(translated, ap);
    int result = console_write(stream, text, translated != format);
    sfree(text);
    return result;
}

int kr_console_fprintf(FILE *stream, const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int result = console_vprintf(stream, format, ap);
    va_end(ap); return result;
}
int kr_console_vfprintf(FILE *stream, const char *format, va_list ap)
{
    return console_vprintf(stream, format, ap);
}
char *kr_utility_vprintf(const char *format, va_list ap)
{
    return dupvprintf(kr_translate(format), ap);
}
int kr_console_printf(const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int result = console_vprintf(stdout, format, ap);
    va_end(ap); return result;
}
int kr_console_fputs(const char *text, FILE *stream)
{
    const char *translated = kr_translate(text);
    return console_write(stream, translated, translated != text);
}
