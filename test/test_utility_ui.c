#include "putty.h"
#include "kittyng.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: %s\n", #x); return 1; } } while (0)
const char *const appname = "utility-language-test";
void modalfatalbox(const char *format, ...) { abort(); }

int main(int argc, char **argv)
{
    CHECK(kr_windows_language(MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)) == 1);
    CHECK(kr_windows_language(MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT)) == 0);
    CHECK(kr_windows_language(MAKELANGID(LANG_GERMAN, SUBLANG_DEFAULT)) == 0);
    CHECK(kr_windows_language(MAKELANGID(LANG_UKRAINIAN, SUBLANG_DEFAULT)) == 0);
    if (!strcmp(argv[argc - 1], "default")) {
        CHECK(kr_language() == kr_windows_language(GetUserDefaultUILanguage()));
        puts("PASS: Windows display-language default"); return 0;
    }
    if (!strcmp(argv[argc - 1], "read0")) { CHECK(kr_language() == 0); return 0; }
    if (!strcmp(argv[argc - 1], "read1")) { CHECK(kr_language() == 1); return 0; }
    CHECK(kr_set_language(0));
    HWND window = CreateWindowW(L"STATIC", L"Test", WS_OVERLAPPED, 0, 0, 500, 200, NULL, NULL, NULL, NULL);
    CHECK(window);
    HWND label = CreateWindowW(L"BUTTON", L"&Generate", WS_CHILD, 0, 0, 100, 30, window, (HMENU)1, NULL, NULL);
    HWND edit = CreateWindowW(L"EDIT", L"Generate %s / key comment", WS_CHILD, 0, 30, 300, 30, window, (HMENU)2, NULL, NULL);
    HWND raw = CreateWindowW(L"STATIC", L"", WS_CHILD, 0, 60, 300, 30, window, (HMENU)3, NULL, NULL);
    CHECK(raw && kr_utility_set_raw_item_text(window, 3, "Generate"));
    CHECK(label && edit);
    HMENU menu = CreateMenu(); CHECK(menu);
    CHECK(kr_utility_append_menu(menu, MF_STRING | MF_GRAYED, 10, "&Generate key pair"));
    CHECK(SetMenu(window, menu));
    kr_add_utility_language_menu(window, 30000);
    kr_utility_localize(window);
    wchar_t text[256];
    CHECK(kr_utility_language_command(window, 30000, 30001));
    CHECK(kr_language() == 1);
    CHECK(GetMenuState(GetSubMenu(menu, 1), 30001, MF_BYCOMMAND) & MF_CHECKED);
    GetWindowTextW(label, text, lenof(text)); CHECK(!wcscmp(text, L"\x0421\x043e\x0437\x0434\x0430\x0442\x044c"));
    GetWindowTextW(edit, text, lenof(text)); CHECK(!wcscmp(text, L"Generate %s / key comment"));
    GetWindowTextW(raw, text, lenof(text)); CHECK(!wcscmp(text, L"Generate"));
    CHECK(GetMenuState(menu, 10, MF_BYCOMMAND) & MF_GRAYED);
    CHECK(kr_utility_language_command(window, 30000, 30000));
    CHECK(GetMenuState(GetSubMenu(menu, 1), 30000, MF_BYCOMMAND) & MF_CHECKED);
    GetWindowTextW(label, text, lenof(text)); CHECK(!wcscmp(text, L"&Generate"));
    GetMenuStringW(menu, 10, text, lenof(text), MF_BYCOMMAND); CHECK(!wcscmp(text, L"&Generate key pair"));
    CHECK(!kr_utility_language_command(window, 30000, 123));
    CHECK(!kr_set_language(7));
    kr_utility_dialog_event(window, WM_DESTROY);
    DestroyWindow(window);
    FILE *out = tmpfile(); CHECK(out);
    CHECK(kr_console_fputs("100% literal %s %n", out) > 0);
    rewind(out); char bytes[128] = {0}; fread(bytes, 1, sizeof(bytes) - 1, out);
    CHECK(!strcmp(bytes, "100% literal %s %n")); fclose(out);
    puts("PASS: utility menu round-trip, edit/key data preserved, enabled states, literal percent output");
    return 0;
}
