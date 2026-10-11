/* All values are synthetic test credentials, never real account data. */
#include "putty.h"
#include "storage.h"
#include "kittyrus.h"
#include "kittyrus-portable.h"
#include "kittyrus-legacy-crypt.h"

const char *const appname = "KiTTY-RUS-test";
void nonfatal(const char *fmt, ...) { fprintf(stderr, "Storage failure: %s\n", fmt); abort(); }
void modalfatalbox(const char *fmt, ...) { fprintf(stderr, "Fatal failure: %s\n", fmt); abort(); }
void add_session_to_jumplist(const char *s) {}
void remove_session_from_jumplist(const char *s) {}
void clear_jumplist(void) {}

int main(void)
{
    assert(kr_portable());
    Conf *conf = conf_new();
    conf_set_str(conf, CONF_host, "demo.example.com");
    conf_set_str(conf, CONF_termtype, "xterm");
    conf_set_str(conf, CONF_password, "synthetic-root-password");
    conf_set_bool(conf, CONF_kr_password_decoded, true);
    assert(!kr_prepare_password(conf));
    assert(strcmp(conf_get_str(conf, CONF_kr_password_encoded), "synthetic-root-password"));
    conf_set_str(conf, CONF_password, "");
    kr_load_password(conf);
    assert(!strcmp(conf_get_str(conf, CONF_password), "synthetic-root-password"));

    char *err = NULL;
    settings_r *r = open_settings_r("Production%20East/root-session");
    /* Raw API names have not yet been escaped; callers normally pass real names. */
    if (r) close_settings_r(r);
    r = open_settings_r("Production East/root-session");
    assert(r);
    char *host = read_setting_s(r, "HostName");
    assert(host && !strcmp(host, "legacy.example.com"));
    sfree(host);
    assert(read_setting_i(r, "PortNumber", 0) == 2222);
    close_settings_r(r);

    settings_w *w = open_settings_w("Production East/root-session", &err);
    assert(w && !err);
    write_setting_s(w, "UserName", "root");
    write_setting_s(w, "TestMultiline", "first\nsecond\\third%fourth");
    write_setting_s(w, "Password", conf_get_str(conf, CONF_kr_password_encoded));
    write_setting_i(w, "SignedNumber", -1);
    close_settings_w(w);
    r = open_settings_r("Production East/root-session");
    assert(r);
    char *value = read_setting_s(r, "LegacyUnknownOption");
    assert(value && !strcmp(value, "preserve this")); sfree(value);
    value = read_setting_s(r, "TestMultiline");
    assert(value && !strcmp(value, "first\nsecond\\third%fourth")); sfree(value);
    assert(read_setting_i(r, "SignedNumber", 0) == -1);
    close_settings_r(r);

    w = open_settings_w("New Folder/new-root", &err);
    assert(w && !err);
    write_setting_s(w, "HostName", "new.example.com");
    close_settings_w(w);
    settings_e *e = enum_settings_start();
    assert(e);
    strbuf *name = strbuf_new();
    bool saw_old = false, saw_new = false;
    while (enum_settings_next(e, name)) {
        saw_old |= !strcmp(name->s, "Production East/root-session");
        saw_new |= !strcmp(name->s, "New Folder/new-root");
        strbuf_clear(name);
    }
    assert(saw_old && saw_new);
    enum_settings_finish(e); strbuf_free(name);
    assert(check_stored_host_key("legacy.example.com", 2222, "rsa", "0x10001,0xabcdef") == 0);
    assert(check_stored_host_key("legacy.example.com", 2222, "rsa", "0x10001,0x123456") == 2);
    assert(check_stored_host_key("missing.example.com", 22, "rsa", "anything") == 1);
    store_host_key(NULL, "new.example.com", 22, "rsa", "0x10001,0x112233");
    assert(check_stored_host_key("new.example.com", 22, "rsa", "0x10001,0x112233") == 0);
    del_settings("New Folder/new-root");
    assert(!open_settings_r("New Folder/new-root"));
    conf_set_str(conf, CONF_kr_password_encoded, "invalid-legacy-value");
    conf_set_str(conf, CONF_password, "previous-session-secret");
    kr_load_password(conf);
    assert(!*conf_get_str(conf, CONF_password));
    assert(!conf_get_bool(conf, CONF_kr_password_decoded));
    assert(!kr_prepare_password(conf));
    assert(!strcmp(conf_get_str(conf, CONF_kr_password_encoded), "invalid-legacy-value"));
    conf_free(conf);
    puts("PASS: KiTTY portable folders, percent encoding, unknown values, passwords and host keys");
    return 0;
}
