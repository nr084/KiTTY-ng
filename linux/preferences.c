#include "putty.h"
#include <gtk/gtk.h>
#include "cli-language.h"
#include "linux/language.h"
#include "names.h"
#include "portable.h"
#include "storage.h"
/* Persist only the language key, keeping other preferences and comments. */
bool kr_linux_save_language(const char *name, char **error) {
    *error = NULL;
    const char *base = kr_mac_root();
    if (!base) {
        *error = dupstr("Portable directory is unavailable");
        return false;
    }
    char *path = dupcat(base, "/values.ini");
    GKeyFile *ini = g_key_file_new();
    char *raw = NULL;
    gsize length = 0;
    GError *failure = NULL;
    if (g_file_get_contents(path, &raw, &length, &failure)) {
        char *text = kr_text_from_bytes((unsigned char *)raw, length);
        g_free(raw);
        bool ok = text &&
                  g_key_file_load_from_data(ini, text, -1,
                                            G_KEY_FILE_KEEP_COMMENTS, &failure);
        free(text);
        if (!ok) {
            *error =
                dupstr(failure ? failure->message : "Invalid preference file");
            if (failure)
                g_error_free(failure);
            g_key_file_unref(ini);
            sfree(path);
            return false;
        }
    } else if (failure && failure->code != G_FILE_ERROR_NOENT) {
        *error = dupstr(failure->message);
        g_error_free(failure);
        g_key_file_unref(ini);
        sfree(path);
        return false;
    }
    if (failure) {
        g_error_free(failure);
        failure = NULL;
    }
    g_key_file_set_integer(ini, "values", "UILanguage", !strcmp(name, "ru"));
    char *data = g_key_file_to_data(ini, &length, NULL);
    bool ok = g_file_set_contents(path, data, length, &failure);
    if (!ok)
        *error = dupstr(failure ? failure->message : "Unable to save language");
    if (failure)
        g_error_free(failure);
    g_free(data);
    g_key_file_unref(ini);
    sfree(path);
    if (ok)
        kr_mac_cli_set_language(name);
    return ok;
}
