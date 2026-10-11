#include "putty.h"
#include "storage.h"
#include "kittyng-extensions.h"
#include "kittyng-portable.h"
#include "kittyng-keypaths.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
const char *const appname = "kitty-ng-extension-test";
const struct BackendVtable *const backends[] = { NULL };
void nonfatal(const char *format, ...) { abort(); }
void modalfatalbox(const char *format, ...) { abort(); }
void add_session_to_jumplist(const char *s) {}
void remove_session_from_jumplist(const char *s) {}
void clear_jumplist(void) {}

int main(void)
{
    CHECK(kr_portable());
    CHECK(!open_settings_r("unsupported"));
    char *unsupported_error = NULL;
    CHECK(!open_settings_w("unsupported", &unsupported_error) && unsupported_error);
    sfree(unsupported_error);
    wchar_t *text = kr_command_text("echo one\\necho two\\tOK", true, true);
    CHECK(!wcscmp(text, L"echo one\recho two\tOK\r")); sfree(text);
    text = kr_command_text("one\\r\\ntwo\\n\\nthree", true, true);
    CHECK(!wcscmp(text, L"one\rtwo\r\rthree\r")); sfree(text);
    text = kr_command_text("printf '\\n'\r\nexit\n", false, true);
    CHECK(!wcscmp(text, L"printf '\\n'\rexit\r")); sfree(text);
    text = kr_command_text("literal\\p\\s", true, false);
    CHECK(!wcscmp(text, L"literal\\p\\s")); sfree(text);
    text = kr_command_text("\xF0\x9F\x98\x80", true, true);
    CHECK(text[0] == 0xD83D && text[1] == 0xDE00 && text[2] == '\r'); sfree(text);
    CHECK(kr_parse_shortcut("{CONTROL}{SHIFT}{F12}") == (VK_F12 | ((MOD_CONTROL | MOD_SHIFT) << 16)));
    CHECK(kr_parse_shortcut("Ctrl+F3") == (VK_F3 | (MOD_CONTROL << 16)));
    CHECK(kr_parse_shortcut("1120") == (VK_F9 | (MOD_CONTROL << 16)));
    CHECK(kr_parse_shortcut("65") == 'A' && !kr_parse_shortcut("999999"));
    CHECK(kr_parse_shortcut("{ALTGR}{OEM_COMMA}") == (VK_OEM_COMMA | (16 << 16)));
    CHECK(!kr_parse_shortcut("{CONTROL}{F25}") && !kr_parse_shortcut("{F3") && !kr_parse_shortcut("{F3}{F4}"));
    char *url = kr_url_at("see (https://example.invalid/path).", 12);
    CHECK(url && !strcmp(url, "https://example.invalid/path")); sfree(url);
    CHECK(!kr_url_at("file:///C:/x.exe", 2)); CHECK(!kr_url_at("javascript:alert(1)", 2));
    CHECK(!kr_url_at("xhttps://example.invalid", 8)); CHECK(!kr_url_at("https://", 3));
    CHECK(!kr_url_at("https://example.invalid", 999));
    text = kr_clipboard_text("c;0J/RgNC40LLQtdGC");
    CHECK(text && !wcscmp(text, L"\x041F\x0440\x0438\x0432\x0435\x0442")); sfree(text);
    CHECK(!kr_clipboard_text("c;?")); CHECK(!kr_clipboard_text("c;!!!!"));
    CHECK(!kr_clipboard_text("c;AA==")); CHECK(!kr_clipboard_text("c;/w=="));
    CHECK(!kr_clipboard_text("x;YWJj")); CHECK(!kr_clipboard_text("c;YWJ"));
    char *large = snewn(70000, char); memset(large, 'A', 69999); large[0]='c'; large[1]=';'; large[69999]=0;
    CHECK(!kr_clipboard_text(large)); sfree(large);
    CHECK(kr_scroll_lines(-1, 25) == 12 && kr_scroll_lines(-2, 25) == 25 && kr_scroll_lines(5, 25) == 5);
    CHECK(kr_scroll_lines(0, 0) == 3 && kr_scroll_lines(-1, 0) == 1);
    Conf *proxy = conf_new(); settings_set_default_protocol(PROT_SSH); load_open_settings(NULL, proxy);
    conf_set_str(proxy, CONF_proxyselection, "corp"); kr_load_proxy(proxy);
    CHECK(!*conf_get_str(proxy, CONF_kr_proxy_error));
    CHECK(conf_get_int(proxy, CONF_proxy_type) == PROXY_SOCKS5);
    CHECK(conf_get_int(proxy, CONF_proxy_port) == 1080);
    CHECK(!strcmp(conf_get_str(proxy, CONF_proxy_username), "proxy-user"));
    CHECK(!strcmp(conf_get_str(proxy, CONF_proxy_password), "proxy-pass"));
    conf_set_str(proxy, CONF_proxyselection, "missing"); kr_load_proxy(proxy);
    CHECK(*conf_get_str(proxy, CONF_kr_proxy_error));
    conf_set_str(proxy, CONF_proxyselection, "- No proxy -"); kr_load_proxy(proxy);
    CHECK(!*conf_get_str(proxy, CONF_kr_proxy_error) && conf_get_int(proxy, CONF_proxy_type) == PROXY_NONE);
    conf_free(proxy);

    char *error = NULL;
    settings_w *writer = open_settings_w("legacy", &error); CHECK(writer && !error);
    write_setting_s(writer, "HostName", "example.invalid");
    write_setting_s(writer, "Autocommand", "echo migrated");
    write_setting_s(writer, "ScriptfileContent", "echo stored\n");
    write_setting_i(writer, "FailureReconnect", 1);
    write_setting_i(writer, "WakeupReconnect", 1);
    write_setting_i(writer, "SendToTray", 1);
    write_setting_i(writer, "Maximize", 1);
    write_setting_i(writer, "SaveWindowPos", 1);
    write_setting_i(writer, "TermXPos", -800);
    write_setting_s(writer, "Comment", "synthetic note");
    write_setting_s(writer, "UnknownLegacyOption", "retain this");
    const wchar_t *root = kr_portable_root();
    wchar_t *path = snewn(wcslen(root)+64, wchar_t); swprintf(path, wcslen(root)+64, L"%ls\\scripts\\startup.sh", root);
    Filename *script = filename_from_wstr(path); sfree(path);
    write_setting_filename(writer, "Scriptfile", script); close_settings_w(writer);
    settings_set_default_protocol(PROT_SSH); settings_set_default_port(22);
    Conf *conf = conf_new(); CHECK(load_settings("legacy", conf));
    CHECK(!strcmp(conf_get_str(conf, CONF_autocommand), "echo migrated"));
    CHECK(!strcmp(conf_get_str(conf, CONF_scriptfilecontent), "echo stored\n"));
    CHECK(!strcmp(conf_get_str(conf, CONF_kr_session_name), "legacy"));
    CHECK(conf_get_int(conf, CONF_kr_reconnect_delay) == 7);
    CHECK(conf_get_int(conf, CONF_initdelay) == 250 && conf_get_int(conf, CONF_kr_command_delay) == 50);
    CHECK(conf_get_int(conf, CONF_bcdelay) == 20 && conf_get_bool(conf, CONF_kr_shortcuts));
    CHECK(conf_get_bool(conf, CONF_wakeup_reconnect) && conf_get_bool(conf, CONF_sendtotray));
    CHECK(conf_get_bool(conf, CONF_maximize) && conf_get_int(conf, CONF_xpos) == -800);
    CHECK(filename_equal(conf_get_filename(conf, CONF_kr_script_file), script)); filename_free(script);
    CHECK(!conf_get_bool(conf, CONF_kr_remote_clipboard));
    error = save_settings("legacy", conf); CHECK(!error);
    error = save_settings("copy", conf); CHECK(!error);
    CHECK(!strcmp(conf_get_str(conf, CONF_kr_session_name), "copy")); conf_free(conf);
    settings_r *reader = open_settings_r("legacy"); CHECK(reader);
    char *value = read_setting_s(reader, "UnknownLegacyOption"); CHECK(value && !strcmp(value, "retain this")); sfree(value);
    value = read_setting_s(reader, "Scriptfile"); CHECK(value && !strcmp(value, "scripts\\startup.sh")); sfree(value);
    CHECK(read_setting_i(reader, "ReconnectDelay", 0) == 7); close_settings_r(reader);
    puts("PASS: legacy settings, command parsing, Unicode, shortcuts, safe URLs, OSC 52 bounds and portable script references");
    return 0;
}
