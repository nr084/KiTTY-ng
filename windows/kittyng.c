/* kitty-ng UI extensions. See LICENCE and LICENCE-KiTTY.txt. */
#include "putty.h"
#include "kittyng.h"
#include "kittyng-portable.h"
#include "kittyng-legacy-crypt.h"
#include <bcrypt.h>
#include <shellapi.h>

static int ui_language = -1;
static const struct { const char *en, *ru; } translations[] = {
#include "kittyng-translations.h"
};

int kr_language(void)
{
    if (ui_language < 0) {
        ui_language = 0; /* Registry helpers may format paths recursively. */
        DWORD language = 0;
        HKEY key = open_regkey_ro(HKEY_CURRENT_USER, PUTTY_REG_POS);
        if (key) {
            get_reg_dword(key, "UILanguage", &language);
            close_regkey(key);
        }
        ui_language = language == 1 ? 1 : 0;
        int argc;
        wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; i++)
            if (!wcscmp(argv[i], L"--language")) {
                if (!wcscmp(argv[i+1], L"ru")) ui_language = 1;
                if (!wcscmp(argv[i+1], L"en")) ui_language = 0;
            }
        if (argv) LocalFree(argv);
    }
    return ui_language;
}

bool kr_set_language(int language)
{
    if (language != 0 && language != 1)
        return false;
    HKEY key = create_regkey(HKEY_CURRENT_USER, PUTTY_REG_POS);
    if (!key)
        return false;
    DWORD value = language;
    bool ok = put_reg_dword(key, "UILanguage", value);
    close_regkey(key);
    if (!ok)
        return false;
    ui_language = language;
    return true;
}

const char *kr_translate(const char *english)
{
    if (english && kr_language()) {
        /* Controls insert accelerator '&' markers before reaching Win32. */
        char plain[1024];
        size_t n = 0;
        for (const char *p = english; *p; p++) {
            if (*p == '&')
                continue;
            if (n + 1 >= sizeof(plain))
                return english;
            plain[n++] = *p;
        }
        plain[n] = '\0';
        for (size_t i = 0; i < lenof(translations); i++)
            if (!strcmp(plain, translations[i].en))
                return translations[i].ru;
    }
    return english;
}

char *kr_printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char *result = dupvprintf(kr_translate(format), ap);
    va_end(ap);
    return result;
}

BOOL kr_set_text(HWND hwnd, const char *text)
{
    if (!text) text = "";
    const char *translated = kr_translate(text);
    wchar_t *wide = dup_mb_to_wc(kr_language() ? CP_UTF8 : CP_ACP, translated);
    BOOL result = SetWindowTextW(hwnd, wide);
    sfree(wide);
    return result;
}

BOOL kr_set_item_text(HWND hwnd, int id, const char *text)
{
    return kr_set_text(GetDlgItem(hwnd, id), text);
}

int kr_messagebox(HWND hwnd, const char *text, const char *caption, UINT flags)
{
    if (!text) text = "";
    if (!caption) caption = "";
    wchar_t *body = dup_mb_to_wc(kr_language() ? CP_UTF8 : CP_ACP, kr_translate(text));
    wchar_t *title = dup_mb_to_wc(kr_language() ? CP_UTF8 : CP_ACP, kr_translate(caption));
    int result = MessageBoxExW(hwnd, body, title, flags,
        MAKELANGID(kr_language() ? LANG_RUSSIAN : LANG_ENGLISH, SUBLANG_DEFAULT));
    sfree(body); sfree(title);
    return result;
}

static BOOL CALLBACK localize_child(HWND hwnd, LPARAM unused)
{
    char text[2048];
    GetWindowTextA(hwnd, text, sizeof(text));
    kr_localize_window(hwnd, text);
    return TRUE;
}

void kr_localize_dialog(HWND hwnd)
{
    EnumChildWindows(hwnd, localize_child, 0);
}

void kr_localize_window(HWND hwnd, const char *english)
{
    const char *text = kr_translate(english);
    if (text && text != english) {
        wchar_t *wide = dup_mb_to_wc(CP_UTF8, text);
        SetWindowTextW(hwnd, wide);
        sfree(wide);
    }
}

BOOL kr_append_menu(HMENU menu, UINT flags, UINT_PTR id, const char *english)
{
    if (!english || flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR))
        return AppendMenuA(menu, flags, id, english);
    const char *text = kr_translate(english);
    wchar_t *wide = dup_mb_to_wc(kr_language() ? CP_UTF8 : CP_ACP, text);
    BOOL result = AppendMenuW(menu, flags, id, wide);
    sfree(wide);
    return result;
}

BOOL kr_insert_menu(HMENU menu, UINT position, UINT flags, UINT_PTR id, const char *english)
{
    if (!english || flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR))
        return InsertMenuA(menu, position, flags, id, english);
    wchar_t *wide = dup_mb_to_wc(kr_language() ? CP_UTF8 : CP_ACP, kr_translate(english));
    BOOL result = InsertMenuW(menu, position, flags, id, wide);
    sfree(wide);
    return result;
}

void kr_apply_transparency(HWND hwnd, Conf *conf)
{
    int value = conf_get_int(conf, CONF_transparencynumber);
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (value < 0) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    } else {
        /* Keep a visible minimum opacity, even with malformed saved values. */
        value = min(value, 240);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)(255 - value), LWA_ALPHA);
    }
}

static int password_mode(void)
{
    wchar_t path[32768];
    const wchar_t *portable = kr_portable_root();
    if (portable) {
        if (wcslen(portable) + 12 >= lenof(path)) return 0;
        wcscpy(path, portable);
    } else {
        DWORD n = GetModuleFileNameW(NULL, path, lenof(path));
        if (!n || n >= lenof(path)) return 0;
        wchar_t *slash = wcsrchr(path, L'\\');
        if (!slash) return 0;
        *slash = 0;
    }
    wcscat(path, L"\\kitty.ini");
    return GetPrivateProfileIntW(L"KiTTY", L"cryptsalt", 0, path);
}

static char *password_key(Conf *conf, int mode)
{
    return mode ? dupstr("KiTTY") :
        dupcat(conf_get_str(conf, CONF_host), conf_get_str(conf, CONF_termtype), "KiTTY");
}

char *kr_prepare_password(Conf *conf)
{
    const char *plain = conf_get_str(conf, CONF_password);
    /* Preserve an opaque legacy value if its format could not be decoded. */
    if (!conf_get_bool(conf, CONF_kr_password_decoded) && !*plain &&
        *conf_get_str(conf, CONF_kr_password_encoded)) return NULL;
    if (!*plain) {
        conf_set_str(conf, CONF_kr_password_encoded, "");
        return NULL;
    }
    int mode = password_mode();
    if (mode > 1) {
        conf_set_str(conf, CONF_kr_password_encoded, plain);
        return NULL;
    }
    if (!*conf_get_str(conf, CONF_host))
        return dupstr("A saved password requires a host name");
    unsigned char random[5];
    if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return dupstr("Unable to generate password encoding seed");
    const char *alphabet = "AZERTYUIOPQSDFGHJKLMWXCVBNazertyuiopqsdfghjklmwxcvbn0123456789+/";
    char seed[5];
    for (size_t i = 0; i < sizeof(seed); i++) seed[i] = alphabet[random[i] % 64];
    char *masked = dupstr(plain), *key = password_key(conf, mode);
    if (!mode) kr_legacy_mask(masked);
    char *encoded = kr_legacy_encode(masked, key, seed);
    smemclr(masked, strlen(masked)); sfree(masked);
    smemclr(key, strlen(key)); sfree(key);
    if (!encoded) return dupstr("Unable to encode saved password");
    conf_set_str(conf, CONF_kr_password_encoded, encoded);
    smemclr(encoded, strlen(encoded)); free(encoded);
    conf_set_bool(conf, CONF_kr_password_decoded, true);
    return NULL;
}

void kr_load_password(Conf *conf)
{
    const char *encoded = conf_get_str(conf, CONF_kr_password_encoded);
    conf_set_str(conf, CONF_password, "");
    conf_set_bool(conf, CONF_kr_password_decoded, true);
    int mode = password_mode();
    if (!*encoded || mode > 1) {
        conf_set_str(conf, CONF_password, encoded);
        return;
    }
    char *key = password_key(conf, mode);
    char *plain = kr_legacy_decode(encoded, key);
    smemclr(key, strlen(key)); sfree(key);
    if (plain) {
        if (!mode) kr_legacy_mask(plain);
        conf_set_str(conf, CONF_password, plain);
        smemclr(plain, strlen(plain)); free(plain);
    } else {
        conf_set_bool(conf, CONF_kr_password_decoded, false);
    }
}
