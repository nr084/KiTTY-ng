/* All files and profiles are synthetic, owned by the Python runner. */
#include "putty.h"
#include "storage.h"
#include "kittyng-portable.h"
#include "kittyng-keypaths.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
const char *const appname = "portable-key-test";
void nonfatal(const char *fmt, ...) { abort(); }
void modalfatalbox(const char *fmt, ...) { abort(); }
void add_session_to_jumplist(const char *s) {}
void remove_session_from_jumplist(const char *s) {}
void clear_jumplist(void) {}

static Filename *root_file(const wchar_t *suffix)
{
    const wchar_t *root = kr_portable_root();
    wchar_t *path = snewn(wcslen(root) + wcslen(suffix) + 1, wchar_t);
    wcscpy(path, root); wcscat(path, suffix);
    Filename *result = filename_from_wstr(path); sfree(path); return result;
}

static bool same_path(const Filename *a, const Filename *b)
{
    return CompareStringOrdinal(a->wpath, -1, b->wpath, -1, TRUE) == CSTR_EQUAL;
}

static int registry_test(const char *path)
{
    const char *prefix = "Software\\kitty-ng-keypaths-test-";
    CHECK(!strncmp(path, prefix, strlen(prefix)));
    CHECK(!kr_portable());
    HKEY isolated; CHECK(RegOpenKeyExA(HKEY_CURRENT_USER, path, 0, KEY_ALL_ACCESS, &isolated) == ERROR_SUCCESS);
    CHECK(RegOverridePredefKey(HKEY_CURRENT_USER, isolated) == ERROR_SUCCESS);
    char *error = NULL;
    settings_w *w = open_settings_w("keys", &error); CHECK(w && !error);
    Filename *relative = filename_from_str("SSH-Keys\\client.ppk");
    write_setting_filename(w, "PublicKeyFile", relative); close_settings_w(w);
    settings_r *r = open_settings_r("keys"); CHECK(r);
    Filename *loaded = read_setting_filename(r, "PublicKeyFile"); CHECK(loaded && filename_equal(relative, loaded));
    filename_free(loaded); filename_free(relative); close_settings_r(r);
    CHECK(RegOverridePredefKey(HKEY_CURRENT_USER, NULL) == ERROR_SUCCESS);
    RegCloseKey(isolated);
    puts("PASS: registry filename semantics unchanged"); return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "update-registry")) {
        CHECK(!kr_portable());
        kr_profile_update result = kr_update_portable_profiles();
        CHECK(!result.updated && !result.unchanged && !result.failed && !result.first_error);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--registry")) return registry_test(argv[2]);
    CHECK(kr_portable());
    const char *mode = argv[argc - 1];
    if (!strcmp(mode, "update-profiles")) {
        kr_profile_update result = kr_update_portable_profiles();
        printf("%u %u %u\n", result.updated, result.unchanged, result.failed);
        sfree(result.first_error);
        return 0;
    }
    if (!strcmp(mode, "transfer-key")) {
        char *error = NULL; settings_w *w = open_settings_w("transfer", &error); CHECK(w && !error);
        Filename *key = root_file(L"\\SSH-Keys\\client.ppk");
        write_setting_filename(w, "PublicKeyFile", key); filename_free(key); close_settings_w(w);
        return 0;
    }
    if (!strcmp(mode, "save")) {
        char *error = NULL; settings_w *w = open_settings_w("Folder/keys", &error); CHECK(w && !error);
        Filename *key = root_file(L"/SSH-Keys/../SSH-Keys/client.ppk");
        Filename *cert = root_file(L"\\SSH-Keys\\certificate.pub");
        write_setting_filename(w, "PublicKeyFile", key);
        write_setting_filename(w, "DetachedCertificate", cert);
        Filename *log = filename_from_str("logs\\trace.log");
        write_setting_filename(w, "LogFileName", log);
        write_setting_s(w, "LegacyUnknownOption", "preserve this");
        close_settings_w(w);
        filename_free(key); filename_free(cert); filename_free(log);

        w = open_settings_w("outside", &error); CHECK(w && !error);
        key = root_file(L"-sibling\\client.ppk");
        char *stored = kr_portable_save_key_path(key); CHECK(!strcmp(stored, key->cpath)); sfree(stored);
        write_setting_filename(w, "PublicKeyFile", key); filename_free(key); close_settings_w(w);

        key = filename_from_str("..\\outside\\client.ppk");
        stored = kr_portable_save_key_path(key);
        CHECK(strlen(stored) > 2 && stored[1] == ':');
        filename_free(key); sfree(stored);
        key = root_file(L"\\SSH-Keys\\client.ppk"); CharLowerBuffW(key->wpath, wcslen(key->wpath));
        stored = kr_portable_save_key_path(key); CHECK(!_stricmp(stored, "SSH-Keys\\client.ppk"));
        filename_free(key); sfree(stored);
        key = filename_from_str("\\\\synthetic-server\\share\\client.ppk");
        stored = kr_portable_save_key_path(key); CHECK(!strcmp(stored, key->cpath)); filename_free(key); sfree(stored);
        key = filename_from_str(""); stored = kr_portable_save_key_path(key); CHECK(!*stored); filename_free(key); sfree(stored);
    }
    settings_r *r = open_settings_r("Folder/keys"); CHECK(r);
    char *stored = read_setting_s(r, "PublicKeyFile"); CHECK(stored && !strcmp(stored, "SSH-Keys\\client.ppk")); sfree(stored);
    stored = read_setting_s(r, "DetachedCertificate"); CHECK(stored && !strcmp(stored, "SSH-Keys\\certificate.pub")); sfree(stored);
    const char *names[] = {"PublicKeyFile", "DetachedCertificate"};
    const wchar_t *files[] = {L"\\SSH-Keys\\client.ppk", L"\\SSH-Keys\\certificate.pub"};
    for (int i = 0; i < 2; i++) {
        Filename *loaded = read_setting_filename(r, names[i]), *expected = root_file(files[i]);
        CHECK(loaded && same_path(loaded, expected));
        FILE *file = f_open(loaded, "rb", false); CHECK(file); fclose(file);
        filename_free(loaded); filename_free(expected);
    }
    Filename *log = read_setting_filename(r, "LogFileName"); CHECK(log && !strcmp(log->cpath, "logs\\trace.log")); filename_free(log);
    stored = read_setting_s(r, "LegacyUnknownOption"); CHECK(stored && !strcmp(stored, "preserve this")); sfree(stored);
    close_settings_r(r);
    Filename *empty = kr_portable_load_key_path(""); CHECK(filename_is_null(empty)); filename_free(empty);
    puts("PASS: portable key/certificate save, absolute resolution, relocation, boundaries, CWD independence and empty paths");
    return 0;
}
