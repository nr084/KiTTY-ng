/* Native GTK utilities use the existing bundled PuTTY keygen and agent. */
#define _GNU_SOURCE
#include "putty.h"
#include <gtk/gtk.h>
#include "tools-gui.h"
#include "licence.h"
#include "cli-language.h"
#include "icons.h"
#include "linux/language.h"
#include "portable.h"
#include <glib-unix.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#define L(en, ru) (!strcmp(kr_mac_translate("Cancel"), "Cancel") ? (en) : (ru))
typedef struct Keygen {
    GtkWidget *window, *public, *comment, *algorithm, *bits, *language, *format,
        *buttons[5], *about;
    char *directory, *key;
    guint signals[2];
} Keygen;
static char *program(const char *name) {
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n < 0)
        return NULL;
    path[n] = 0;
    char *slash = strrchr(path, '/');
    if (!slash)
        return NULL;
    *slash = 0;
    return g_build_filename(path, name, NULL);
}
static void error(GtkWidget *window, const char *message) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                          "%s", message);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}
static bool tool(GtkWidget *window, const char *name, char **parameters,
                 char **result) {
    char *exe = program(name);
    GPtrArray *args = g_ptr_array_new();
    g_ptr_array_add(args, exe);
    for (int i = 0; parameters && parameters[i]; i++)
        g_ptr_array_add(args, parameters[i]);
    g_ptr_array_add(args, NULL);
    char **env = g_get_environ();
    env = g_environ_setenv(env, "KITTY_NG_GUI_PROMPTS", "1", TRUE);
    char *output = NULL, *errors = NULL;
    int status;
    GError *e = NULL;
    bool ok = g_spawn_sync(NULL, (char **)args->pdata, env, 0, NULL, NULL,
                           &output, &errors, &status, &e);
    g_strfreev(env);
    g_ptr_array_free(args, TRUE);
    g_free(exe);
    if (!ok || status) {
        error(window,
              e ? e->message
                : (errors && *errors ? errors
                                     : L("Utility failed", "Ошибка утилиты")));
        if (e)
            g_error_free(e);
        g_free(output);
        g_free(errors);
        return false;
    }
    g_free(errors);
    if (result)
        *result = output;
    else
        g_free(output);
    return true;
}
static void refresh(Keygen *k) {
    char *parameters[] = {k->key, "-L", NULL}, *public = NULL;
    if (tool(k->window, "kittygen", parameters, &public)) {
        char *valid = g_utf8_make_valid(public, -1);
        gtk_text_buffer_set_text(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(k->public)), valid, -1);
        char *first = strchr(public, ' '),
             *second = first ? strchr(first + 1, ' ') : NULL;
        if (second) {
            g_strchomp(second + 1);
            gtk_entry_set_text(GTK_ENTRY(k->comment), second + 1);
        } else
            gtk_entry_set_text(GTK_ENTRY(k->comment), "");
        g_free(valid);
        g_free(public);
    }
}
static char *choose(GtkWidget *window, bool save) {
    GtkWidget *d = gtk_file_chooser_dialog_new(
        save ? L("Save key", "Сохранить ключ")
             : L("Load key", "Загрузить ключ"),
        GTK_WINDOW(window),
        save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
        L("Cancel", "Отмена"), GTK_RESPONSE_CANCEL,
        save ? L("Save", "Сохранить") : L("Open", "Открыть"), GTK_RESPONSE_OK,
        NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), g_get_home_dir());
    if (save) {
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d),
                                                       TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "private.ppk");
    }
    char *path = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK
                     ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d))
                     : NULL;
    gtk_widget_destroy(d);
    return path;
}
static void key_action(GtkButton *button, gpointer data) {
    Keygen *k = data;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    if (action == 0) {
        char *path = choose(k->window, false);
        if (path) {
            char *args[] = {path, "-O", "private", "-o", k->key, NULL};
            if (tool(k->window, "kittygen", args, NULL))
                refresh(k);
            g_free(path);
        }
    } else if (action == 1) {
        int a = gtk_combo_box_get_active(GTK_COMBO_BOX(k->algorithm));
        char *types[] = {"ed25519", "rsa", "ecdsa"};
        const char *bits = gtk_entry_get_text(GTK_ENTRY(k->bits)),
                   *comment = gtk_entry_get_text(GTK_ENTRY(k->comment));
        char *args[] = {"-t",
                        types[a],
                        "-b",
                        (char *)(a == 0 ? "255" : bits),
                        "-C",
                        (char *)comment,
                        "--new-passphrase",
                        "/dev/null",
                        "-o",
                        k->key,
                        NULL};
        if (tool(k->window, "kittygen", args, NULL))
            refresh(k);
    } else if (action == 2) {
        if (!g_file_test(k->key, G_FILE_TEST_EXISTS))
            return;
        char *args[] = {k->key, "-P", "-o", k->key, NULL};
        tool(k->window, "kittygen", args, NULL);
    } else if (action == 3) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(k->public));
        GtkTextIter a, b;
        gtk_text_buffer_get_bounds(buffer, &a, &b);
        char *text = gtk_text_buffer_get_text(buffer, &a, &b, FALSE);
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text,
                               -1);
        g_free(text);
    } else if (action == 4) {
        if (!g_file_test(k->key, G_FILE_TEST_EXISTS))
            return;
        char *path = choose(k->window, true);
        if (path) {
            char *formats[] = {"private", "private-openssh-new", "public"};
            char *args[] = {
                k->key,
                "-O",
                formats[gtk_combo_box_get_active(GTK_COMBO_BOX(k->format))],
                "-C",
                (char *)gtk_entry_get_text(GTK_ENTRY(k->comment)),
                "-o",
                path,
                NULL};
            tool(k->window, "kittygen", args, NULL);
            g_free(path);
        }
    }
}
static void key_free(gpointer data) {
    Keygen *k = data;
    for (int i = 0; i < 2; i++)
        if (k->signals[i])
            g_source_remove(k->signals[i]);
    unlink(k->key);
    rmdir(k->directory);
    g_free(k->directory);
    g_free(k->key);
    g_free(k);
}
static gboolean key_shutdown(gpointer data) {
    Keygen *k = data;
    guint id = g_source_get_id(g_main_current_source());
    for (int i = 0; i < 2; i++)
        if (k->signals[i] == id)
            k->signals[i] = 0;
    gtk_widget_destroy(k->window);
    return G_SOURCE_REMOVE;
}
static void algorithm_changed(GtkComboBox *combo, gpointer data) {
    Keygen *k = data;
    int index = gtk_combo_box_get_active(combo);
    gtk_entry_set_text(GTK_ENTRY(k->bits), index == 0   ? "255"
                                           : index == 1 ? "3072"
                                                        : "256");
    gtk_widget_set_sensitive(k->bits, index != 0);
}

static void utility_about(GtkButton *button, gpointer window) {
    char *info = buildinfo("\n");
    gtk_show_about_dialog(GTK_WINDOW(window),
        "program-name", g_object_get_data(G_OBJECT(window), "utility-name"),
        "version", ver,
        "comments", info,
        "copyright", "PuTTY: " SHORT_COPYRIGHT_DETAILS "\nKiTTY: Cyril Dupont",
        "website", "https://github.com/nr084/kitty-ng",
        "website-label", "KiTTY-ng", NULL);
    sfree(info);
}
static GtkWidget *utility_about_button(GtkWidget *window, const char *name) {
    g_object_set_data(G_OBJECT(window), "utility-name", (gpointer)name);
    GtkWidget *b = gtk_button_new_with_label(kr_mac_translate("About"));
    g_signal_connect(b, "clicked", G_CALLBACK(utility_about), window);
    return b;
}
static void key_language(GtkComboBox *combo, gpointer data) {
    Keygen *k = data;
    char *failure = NULL;
    const char *name = gtk_combo_box_get_active(combo) == 1 ? "ru" : "en";
    if (!kr_linux_save_language(name, &failure)) {
        error(k->window, failure);
        sfree(failure);
        return;
    }
    gtk_button_set_label(GTK_BUTTON(k->about), kr_mac_translate("About"));
    const char *labels[] = {L("Load…", "Загрузить…"),
                            L("Generate…", "Создать…"),
                            L("Change passphrase…", "Парольная фраза…"),
                            L("Copy public key", "Копировать публичный ключ"),
                            L("Export…", "Экспорт…")};
    for (int i = 0; i < 5; i++)
        gtk_button_set_label(GTK_BUTTON(k->buttons[i]), labels[i]);
    gtk_entry_set_placeholder_text(GTK_ENTRY(k->comment),
                                   L("Key comment", "Комментарий ключа"));
}

static void keygen(void) {
    Keygen *k = g_new0(Keygen, 1);
    k->directory = g_dir_make_tmp("kitty-ng-keygen-XXXXXX", NULL);
    if (!k->directory) {
        g_free(k);
        return;
    }
    chmod(k->directory, 0700);
    k->key = g_build_filename(k->directory, "private.ppk", NULL);
    k->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(k->window), "KiTTYgen — KiTTY-ng");
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "KiTTYgen");
    k->about = utility_about_button(k->window, "KiTTYgen");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), k->about);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(k->window), header);
    gtk_window_set_default_size(GTK_WINDOW(k->window), 800, 520);
    kr_linux_icon(GTK_WINDOW(k->window));
    g_object_set_data_full(G_OBJECT(k->window), "keygen", k, key_free);
    k->signals[0] = g_unix_signal_add(SIGTERM, key_shutdown, k);
    k->signals[1] = g_unix_signal_add(SIGINT, key_shutdown, k);
    g_signal_connect(k->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    gtk_container_add(GTK_CONTAINER(k->window), box);
    k->public = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(k->public), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(k->public), TRUE);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), k->public);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    k->algorithm = gtk_combo_box_text_new();
    for (int i = 0; i < 3; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(k->algorithm),
                                       i == 0   ? "Ed25519"
                                       : i == 1 ? "RSA"
                                                : "ECDSA");
    gtk_combo_box_set_active(GTK_COMBO_BOX(k->algorithm), 0);
    k->bits = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(k->bits), "3072");
    gtk_entry_set_width_chars(GTK_ENTRY(k->bits), 6);
    g_signal_connect(k->algorithm, "changed", G_CALLBACK(algorithm_changed), k);
    algorithm_changed(GTK_COMBO_BOX(k->algorithm), k);
    atk_object_set_name(gtk_widget_get_accessible(k->algorithm),
                        "Key algorithm");
    atk_object_set_name(gtk_widget_get_accessible(k->bits), "Key size");
    k->comment = gtk_entry_new();
    atk_object_set_name(gtk_widget_get_accessible(k->comment), "Key comment");
    gtk_entry_set_placeholder_text(GTK_ENTRY(k->comment),
                                   L("Key comment", "Комментарий ключа"));
    gtk_box_pack_start(GTK_BOX(row), k->algorithm, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), k->bits, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), k->comment, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const char *labels[] = {L("Load…", "Загрузить…"),
                            L("Generate…", "Создать…"),
                            L("Change passphrase…", "Парольная фраза…"),
                            L("Copy public key", "Копировать публичный ключ"),
                            L("Export…", "Экспорт…")};
    for (int i = 0; i < 5; i++) {
        GtkWidget *b = gtk_button_new_with_label(labels[i]);
        k->buttons[i] = b;
        g_object_set_data(G_OBJECT(b), "action", GINT_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(key_action), k);
        gtk_box_pack_start(GTK_BOX(row), b, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    GtkWidget *extras = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    k->format = gtk_combo_box_text_new();
    for (int i = 0; i < 3; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(k->format),
                                       i == 0   ? "PuTTY PPK"
                                       : i == 1 ? "OpenSSH private"
                                                : "RFC 4716 public");
    gtk_combo_box_set_active(GTK_COMBO_BOX(k->format), 0);
    k->language = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(k->language), "English");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(k->language), "Русский");
    gtk_combo_box_set_active(GTK_COMBO_BOX(k->language),
                             strcmp(kr_mac_translate("Cancel"), "Cancel") ? 1
                                                                          : 0);
    g_signal_connect(k->language, "changed", G_CALLBACK(key_language), k);
    gtk_box_pack_start(GTK_BOX(extras), k->format, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(extras), k->language, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), extras, FALSE, FALSE, 0);
    gtk_box_pack_start(
        GTK_BOX(box),
        gtk_label_new(
            "PuTTY: Simon Tatham and contributors · KiTTY: Cyril Dupont"),
        FALSE, FALSE, 0);
    gtk_widget_show_all(k->window);
    gtk_main();
}
static void agent_action(GtkButton *button, gpointer window) {
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    char *output = NULL;
    if (action == 0) {
        char *path = choose(window, false);
        if (path) {
            char *args[] = {"-a", path, NULL};
            tool(window, "kageant", args, &output);
            g_free(path);
        }
    } else if (action == 3) {
        GtkWidget *d = gtk_dialog_new_with_buttons(
            L("Remove key…", "Удалить ключ…"), GTK_WINDOW(window),
            GTK_DIALOG_MODAL, L("Cancel", "Отмена"), GTK_RESPONSE_CANCEL,
            L("Remove", "Удалить"), GTK_RESPONSE_OK, NULL);
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(
            GTK_ENTRY(entry),
            L("Key fingerprint or comment", "Отпечаток или комментарий ключа"));
        gtk_container_add(
            GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), entry);
        gtk_widget_show_all(d);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK &&
            *gtk_entry_get_text(GTK_ENTRY(entry))) {
            char *args[] = {"-d", (char *)gtk_entry_get_text(GTK_ENTRY(entry)),
                            NULL};
            tool(window, "kageant", args, &output);
        }
        gtk_widget_destroy(d);
    } else {
        char *args[] = {action == 1 ? "-l" : "-D", NULL};
        tool(window, "kageant", args, &output);
    }
    GtkWidget *view = g_object_get_data(G_OBJECT(window), "view");
    if (output) {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),
                                 output, -1);
        g_free(output);
    }
}
static void agent_reaped(GPid pid, gint status, gpointer unused) {
    g_spawn_close_pid(pid);
}
void kr_linux_agent_ensure(void) {
    const char *existing = getenv("KITTY_NG_AGENT_SOCKET");
    struct stat st;
    if (existing && !stat(existing, &st) && S_ISSOCK(st.st_mode)) {
        setenv("SSH_AUTH_SOCK", existing, 1);
        return;
    }
    char *directory = g_dir_make_tmp("kitty-ng-agent-XXXXXX", NULL);
    if (!directory)
        return;
    chmod(directory, 0700);
    char *socketpath = g_build_filename(directory, "socket", NULL),
         *exe = program("kageant");
    const char *image = getenv("APPIMAGE");
    char *native[] = {exe, "--foreground-x11", "--symlink", socketpath, NULL};
    char *packaged[] = {
        (char *)image, "--tool",   "kageant", "--foreground-x11",
        "--symlink",   socketpath, NULL};
    GError *error = NULL;
    GPid pid = 0;
    bool ok =
        g_spawn_async(NULL, image && *image ? packaged : native, NULL,
                      G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL |
                          G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, &pid, &error);
    if (ok) {
        g_child_watch_add(pid, agent_reaped, NULL);
        for (int i = 0; i < 100; i++) {
            if (!stat(socketpath, &st) && S_ISSOCK(st.st_mode))
                break;
            g_usleep(50000);
        }
        ok = !stat(socketpath, &st) && S_ISSOCK(st.st_mode);
    }
    if (ok) {
        setenv("KITTY_NG_AGENT_SOCKET", socketpath, 1);
        setenv("SSH_AUTH_SOCK", socketpath, 1);
        char value[32];
        snprintf(value, sizeof(value), "%ld", (long)pid);
        setenv("KITTY_NG_AGENT_PID", value, 1);
    } else {
        if (pid > 0)
            kill(pid, SIGTERM);
        unlink(socketpath);
        rmdir(directory);
    }
    if (error)
        g_error_free(error);
    g_free(exe);
    g_free(socketpath);
    g_free(directory);
}

static void agent(void) {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Kageant — KiTTY-ng");
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "Kageant");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), utility_about_button(window, "Kageant"));
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 360);
    kr_linux_icon(GTK_WINDOW(window));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    gtk_container_add(GTK_CONTAINER(window), box);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    g_object_set_data(G_OBJECT(window), "view", view);
    gtk_box_pack_start(GTK_BOX(box), view, TRUE, TRUE, 0);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    const char *labels[] = {L("Add key…", "Добавить ключ…"),
                            L("List keys", "Список ключей"),
                            L("Remove all keys", "Удалить все ключи"),
                            L("Remove key…", "Удалить ключ…")};
    for (int i = 0; i < 4; i++) {
        GtkWidget *b = gtk_button_new_with_label(labels[i]);
        g_object_set_data(G_OBJECT(b), "action", GINT_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(agent_action), window);
        gtk_box_pack_start(GTK_BOX(row), b, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(window);
    gtk_main();
}
bool kr_linux_tools_dispatch(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--keygen")) {
        keygen();
        return true;
    }
    if (argc > 1 && !strcmp(argv[1], "--askpass"))
        return false;
    kr_linux_agent_ensure();
    if (argc > 1 && !strcmp(argv[1], "--agent")) {
        agent();
        return true;
    }
    return false;
}
