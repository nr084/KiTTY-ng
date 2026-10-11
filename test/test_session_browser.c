#include "putty.h"
#include "session-browser.h"
#include "kittyng-portable.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
const char *const appname = "session-browser-test";
void modalfatalbox(const char *format, ...) { abort(); }

int main(void)
{
    char *parent = session_browser_parent("prod/sub");
    CHECK(!strcmp(parent, "prod")); sfree(parent);
    parent = session_browser_parent("prod");
    CHECK(!*parent); sfree(parent);
    parent = session_browser_parent("");
    CHECK(!*parent); sfree(parent);
    CHECK(!strcmp(session_browser_relative("", "alpha"), "alpha"));
    CHECK(!strcmp(session_browser_relative("prod", "prod/root"), "root"));
    CHECK(!strcmp(session_browser_relative("prod", "prod/sub/root"), "sub/root"));
    CHECK(session_browser_relative("prod", "production/root") == NULL);
    CHECK(session_browser_relative("prod", "prod") == NULL);
    CHECK(session_browser_relative("prod/sub", "prod/root") == NULL);
    CHECK(session_browser_match("Server-ROOT", "server-root"));
    CHECK(session_browser_match("Server-ROOT", "rOoT"));
    CHECK(!session_browser_match("Server-ROOT", "root-longer-than-name"));
    CHECK(!session_browser_match("Server-ROOT", "router"));
    CHECK(session_browser_match("Server-ROOT", ""));
    char *upper = dup_wc_to_mb(CP_ACP, L"\x0421\x0415\x0420\x0412\x0415\x0420", "");
    char *lower = dup_wc_to_mb(CP_ACP, L"\x0441\x0435\x0440\x0432\x0435\x0440", "");
    CHECK(session_browser_match(upper, lower));
    sfree(upper); sfree(lower);
    upper = dup_wc_to_mb(CP_UTF8, L"\x0421\x0415\x0420\x0412\x0415\x0420", "");
    lower = dup_wc_to_mb(CP_UTF8, L"\x0441\x0435\x0440\x0432\x0435\x0440", "");
    CHECK(session_browser_match(upper, lower));
    sfree(upper); sfree(lower);
    CHECK(kr_portable());
    bool empty = false, nested = false, encoded = false;
    for (int i = 0; ; i++) {
        char *folder = kr_session_folder_enum(i);
        if (!folder) break;
        empty |= !strcmp(folder, "empty");
        nested |= !strcmp(folder, "prod/sub");
        encoded |= !strcmp(folder, "space folder");
        CHECK(strcmp(folder, "session-file"));
        sfree(folder);
    }
    CHECK(empty && nested && encoded);
    puts("PASS: root scope, folder boundaries, nested paths, case-insensitive search, empty portable directories");
    return 0;
}
