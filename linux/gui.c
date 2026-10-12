/* Linux profile UI uses the existing PuTTY GTK terminal and configuration. */
#define _GNU_SOURCE
#include "putty.h"
#include <gtk/gtk.h>
#include "gui.h"
#include "cli-language.h"
#include "icons.h"
#include "linux/language.h"
#include "portable.h"
#include "storage.h"
#include "tools-gui.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define L(en, ru) (!strcmp(kr_mac_translate("Cancel"), "Cancel") ? (en) : (ru))
extern void launch_duplicate_session(Conf *conf);
extern GtkWidget *create_config_box(const char *, Conf *, bool, int,
                                    void (*)(void *, int), void *);
static GtkWidget *window, *list, *search, *root_label, *language;
static GtkListStore *store;
static char *folder;
static char *saved_query;
static bool restart_ui;
static void message(const char *text) {
    GtkWidget *d =
        gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL,
                               GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", text);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}
static char *ask(const char *title, const char *initial, bool secret) {
    GtkWidget *d = gtk_dialog_new_with_buttons(
        title, window ? GTK_WINDOW(window) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR, L("Cancel", "Отмена"),
        GTK_RESPONSE_CANCEL, L("Continue", "Продолжить"), GTK_RESPONSE_OK,
        NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), !secret);
    gtk_entry_set_text(GTK_ENTRY(entry), initial ? initial : "");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_container_set_border_width(
        GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), 16);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))),
                      entry);
    gtk_widget_show_all(d);
    char *value = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK
                      ? g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)))
                      : NULL;
    gtk_widget_destroy(d);
    return value;
}
static char *selected(bool *directory) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(
            gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), &model, &iter))
        return NULL;
    char *name;
    gboolean isdir;
    gtk_tree_model_get(model, &iter, 1, &name, 2, &isdir, -1);
    if (directory)
        *directory = isdir;
    return name;
}
static int compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
                   gpointer unused) {
    gboolean ad, bd;
    char *an, *bn;
    gtk_tree_model_get(model, a, 0, &an, 2, &ad, -1);
    gtk_tree_model_get(model, b, 0, &bn, 2, &bd, -1);
    int result = ad != bd ? (ad ? -1 : 1) : g_utf8_collate(an, bn);
    if (!strcmp(an, "..") && strcmp(bn, ".."))
        result = -1;
    if (!strcmp(bn, "..") && strcmp(an, ".."))
        result = 1;
    g_free(an);
    g_free(bn);
    return result;
}
static bool matches(const char *name) {
    const char *query = gtk_entry_get_text(GTK_ENTRY(search));
    char *a = g_utf8_casefold(name, -1), *b = g_utf8_casefold(query, -1);
    bool result = strstr(a, b) != NULL;
    g_free(a);
    g_free(b);
    return result;
}
static void reload(void) {
    char *previous = list ? selected(NULL) : NULL;
    gtk_list_store_clear(store);
    gtk_label_set_text(GTK_LABEL(root_label),
                       kr_mac_root() ? kr_mac_root() : "");
    if (folder && *folder) {
        GtkTreeIter i;
        gtk_list_store_append(store, &i);
        gtk_list_store_set(store, &i, 0, "..", 1, "..", 2, TRUE, -1);
    }
    struct sesslist sessions = {0};
    get_sesslist(&sessions, true);
    GHashTable *seen =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    size_t prefix = folder ? strlen(folder) : 0;
    for (int n = 0; n < sessions.nsessions; n++) {
        const char *full = sessions.sessions[n], *leaf = full;
        if (prefix) {
            if (strncmp(full, folder, prefix) || full[prefix] != '/')
                continue;
            leaf = full + prefix + 1;
        }
        if (!strcmp(full, "Default Settings"))
            continue;
        const char *slash = strchr(leaf, '/');
        char *shortname =
            slash ? g_strndup(leaf, slash - leaf) : g_strdup(leaf);
        if (!g_hash_table_contains(seen, shortname) && matches(shortname)) {
            g_hash_table_add(seen, g_strdup(shortname));
            char *display = slash ? g_strdup_printf("<%s>", shortname)
                                  : g_strdup(shortname);
            char *path = prefix ? g_strdup_printf("%s/%s", folder, shortname)
                                : g_strdup(shortname);
            GtkTreeIter i;
            gtk_list_store_append(store, &i);
            gtk_list_store_set(store, &i, 0, display, 1, path, 2, slash != NULL,
                               -1);
            g_free(display);
            g_free(path);
        }
        g_free(shortname);
    }
    g_hash_table_destroy(seen);
    get_sesslist(&sessions, false);
    if (previous) {
        GtkTreeIter iter;
        gboolean valid =
            gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
        while (valid) {
            char *name;
            gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 1, &name, -1);
            bool same = !strcmp(name, previous);
            g_free(name);
            if (same) {
                gtk_tree_selection_select_iter(
                    gtk_tree_view_get_selection(GTK_TREE_VIEW(list)), &iter);
                break;
            }
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
        }
        g_free(previous);
    }
}
static void open_selected(void) {
    bool dir;
    char *name = selected(&dir);
    if (!name)
        return;
    if (dir) {
        if (!strcmp(name, "..")) {
            char *slash = strrchr(folder, '/');
            if (slash)
                *slash = 0;
            else
                *folder = 0;
        } else {
            g_free(folder);
            folder = g_strdup(name);
        }
        reload();
    } else {
        Conf *conf = conf_new();
        do_defaults(name, conf);
        launch_duplicate_session(conf);
        conf_free(conf);
    }
    g_free(name);
}
static void activate(GtkTreeView *view, GtkTreePath *path,
                     GtkTreeViewColumn *column, gpointer data) {
    open_selected();
}
static void advanced_done(void *ctx, int result) {
    Conf *conf = ctx;
    if (result > 0)
        launch_duplicate_session(conf);
    conf_free(conf);
    reload();
}
static char *profile_fingerprint(const char *name) {
    char *path = kr_mac_profile_path(name), *bytes = NULL, *hash = NULL;
    gsize length = 0;
    if (path && g_file_get_contents(path, &bytes, &length, NULL) &&
        length <= 1048576)
        hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
                                           (const guchar *)bytes, length);
    if (bytes) {
        smemclr(bytes, length);
        g_free(bytes);
    }
    sfree(path);
    return hash;
}
static void edit_profile(bool duplicate, bool fresh) {
    bool dir = false;
    char *source = fresh ? NULL : selected(&dir);
    if (dir || (!fresh && !source)) {
        g_free(source);
        return;
    }
    char *name = duplicate || fresh ? ask(L("Profile name", "Имя профиля"),
                                          fresh ? "" : source, false)
                                    : g_strdup(source);
    if (!name || !*name) {
        g_free(source);
        g_free(name);
        return;
    }
    char *baseline = source ? profile_fingerprint(source) : NULL;
    Conf *conf = conf_new();
    do_defaults(source, conf);
    if (fresh) {
        conf_set_int(conf, CONF_protocol, PROT_SSH);
        conf_set_int(conf, CONF_port, 22);
    }
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        L("Profile settings", "Настройки профиля"), GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR, L("Cancel", "Отмена"),
        GTK_RESPONSE_CANCEL, L("Advanced…", "Все настройки…"), 2,
        L("Save", "Сохранить"), GTK_RESPONSE_OK, NULL);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    gtk_container_add(
        GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
    const char *labels[] = {L("Host", "Сервер"),
                            L("Port", "Порт"),
                            L("User name", "Имя пользователя"),
                            L("Saved password", "Сохранённый пароль"),
                            L("Private key", "Закрытый ключ"),
                            L("Startup command", "Автокоманда")};
    GtkWidget *entries[6];
    char port[16];
    snprintf(port, sizeof(port), "%d", conf_get_int(conf, CONF_port));
    const char *values[] = {
        conf_get_str(conf, CONF_host),
        port,
        conf_get_str_ambi(conf, CONF_username, NULL),
        conf_get_str(conf, CONF_password),
        filename_to_str(conf_get_filename(conf, CONF_keyfile)),
        conf_get_str(conf, CONF_autocommand)};
    for (int i = 0; i < 6; i++) {
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(labels[i]), 0, i, 1, 1);
        entries[i] = gtk_entry_new();
        atk_object_set_name(gtk_widget_get_accessible(entries[i]), labels[i]);
        gtk_entry_set_text(GTK_ENTRY(entries[i]), values[i]);
        gtk_entry_set_width_chars(GTK_ENTRY(entries[i]), 45);
        if (i == 3)
            gtk_entry_set_visibility(GTK_ENTRY(entries[i]), FALSE);
        gtk_grid_attach(GTK_GRID(grid), entries[i], 1, i, 1, 1);
    }
    GtkWidget *clear_password = gtk_check_button_new_with_label(
        L("Clear saved password", "Очистить пароль"));
    gtk_grid_attach(GTK_GRID(grid), clear_password, 2, 3, 1, 1);
    gtk_widget_show_all(dialog);
    int result = gtk_dialog_run(GTK_DIALOG(dialog));
    if (result == GTK_RESPONSE_OK || result == 2) {
        const char *porttext = gtk_entry_get_text(GTK_ENTRY(entries[1]));
        char *end;
        long number = strtol(porttext, &end, 10);
        if (*end || number < 1 || number > 65535) {
            message(L("Invalid port", "Недопустимый порт"));
            result = GTK_RESPONSE_CANCEL;
        } else {
            conf_set_str(conf, CONF_host,
                         gtk_entry_get_text(GTK_ENTRY(entries[0])));
            conf_set_int(conf, CONF_port, (int)number);
            conf_set_utf8(conf, CONF_username,
                          gtk_entry_get_text(GTK_ENTRY(entries[2])));
            const char *password = gtk_entry_get_text(GTK_ENTRY(entries[3]));
            if (gtk_toggle_button_get_active(
                    GTK_TOGGLE_BUTTON(clear_password))) {
                conf_set_str(conf, CONF_password, "");
                conf_set_str(conf, CONF_kr_password_encoded, "");
                conf_set_bool(conf, CONF_kr_password_decoded, true);
            } else if (strcmp(password, conf_get_str(conf, CONF_password))) {
                conf_set_str(conf, CONF_password, password);
                conf_set_bool(conf, CONF_kr_password_decoded, true);
            }
            Filename *key =
                filename_from_str(gtk_entry_get_text(GTK_ENTRY(entries[4])));
            conf_set_filename(conf, CONF_keyfile, key);
            filename_free(key);
            conf_set_str(conf, CONF_autocommand,
                         gtk_entry_get_text(GTK_ENTRY(entries[5])));
        }
    }
    gtk_widget_destroy(dialog);
    if (result == GTK_RESPONSE_OK && source) {
        char *current = profile_fingerprint(source);
        if (!baseline || !current || strcmp(baseline, current)) {
            message(L(
                "Profile changed while editing. Reopen it before saving.",
                "Профиль изменился во время редактирования. Откройте его снова "
                "перед сохранением."));
            result = GTK_RESPONSE_CANCEL;
        }
        g_free(current);
    }
    if (result == GTK_RESPONSE_OK) {
        settings_r *existing = open_settings_r(name);
        if ((duplicate || fresh) && existing) {
            message(L("A profile with this name already exists",
                      "Профиль с таким именем уже существует"));
        } else {
            bool ok = true;
            if (duplicate) {
                char *from = kr_mac_profile_path(source),
                     *to = kr_mac_profile_path(name), *bytes = NULL;
                gsize len;
                GError *error = NULL;
                ok = from && to &&
                     g_file_get_contents(from, &bytes, &len, &error) &&
                     len <= 1048576;
                if (ok) {
                    char *parent = g_path_get_dirname(to);
                    ok = g_mkdir_with_parents(parent, 0700) == 0;
                    g_free(parent);
                    int fd =
                        ok ? open(to, O_WRONLY | O_CREAT | O_EXCL, 0600) : -1;
                    ok = fd >= 0;
                    size_t offset = 0;
                    while (ok && offset < len) {
                        ssize_t n = write(fd, bytes + offset, len - offset);
                        if (n < 0 && errno == EINTR)
                            continue;
                        if (n <= 0) {
                            ok = false;
                            break;
                        }
                        offset += n;
                    }
                    if (fd >= 0)
                        close(fd);
                    if (!ok && fd >= 0)
                        unlink(to);
                }
                if (!ok)
                    message(error ? error->message
                                  : L("Unable to duplicate profile",
                                      "Не удалось дублировать профиль"));
                if (error)
                    g_error_free(error);
                if (bytes) {
                    smemclr(bytes, len);
                    g_free(bytes);
                }
                sfree(from);
                sfree(to);
            }
            if (ok) {
                char *error = save_settings(name, conf);
                if (error) {
                    message(error);
                    sfree(error);
                }
            }
        }
        close_settings_r(existing);
    } else if (result == 2) {
        create_config_box("KiTTY-ng", conf, false, 0, advanced_done, conf);
        conf = NULL;
    }
    if (conf)
        conf_free(conf);
    g_free(baseline);
    g_free(name);
    g_free(source);
    reload();
}
static void clicked(GtkButton *button, gpointer action) {
    switch (GPOINTER_TO_INT(action)) {
    case 0:
        edit_profile(false, true);
        break;
    case 1:
        edit_profile(false, false);
        break;
    case 2: {
        bool dir;
        char *n = selected(&dir);
        if (n && !dir) {
            GtkWidget *d = gtk_message_dialog_new(
                GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                GTK_BUTTONS_OK_CANCEL, "%s: %s",
                L("Delete profile", "Удалить профиль"), n);
            if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK)
                del_settings(n);
            gtk_widget_destroy(d);
        }
        g_free(n);
        reload();
        break;
    }
    case 3:
        edit_profile(true, false);
        break;
    case 4:
        reload();
        break;
    case 5: {
        int count = kr_mac_upgrade_profiles();
        char *s = g_strdup_printf(
            "%s: %d", L("Updated profiles", "Обновлено профилей"), count);
        message(s);
        g_free(s);
        reload();
        break;
    }
    case 6:
        open_selected();
        break;
    case 7: {
        GtkWidget *d = gtk_file_chooser_dialog_new(
            L("Portable folder", "Портативная папка"), GTK_WINDOW(window),
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, L("Cancel", "Отмена"),
            GTK_RESPONSE_CANCEL, L("Select", "Выбрать"), GTK_RESPONSE_OK, NULL);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
            char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
            kr_mac_set_root(path);
            g_free(path);
            g_free(folder);
            folder = g_strdup("");
            reload();
        }
        gtk_widget_destroy(d);
        break;
    }
    case 9:
    case 10: {
        char executable[4096];
        ssize_t n =
            readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (n > 0) {
            executable[n] = 0;
            const char *image = getenv("APPIMAGE");
            char *args[] = {
                image && *image ? (char *)image : executable,
                GPOINTER_TO_INT(action) == 9 ? "--keygen" : "--agent", NULL};
            GError *error = NULL;
            if (!g_spawn_async(NULL, args, NULL, 0, NULL, NULL, NULL, &error)) {
                message(error->message);
                g_error_free(error);
            }
        }
        break;
    }
    case 8: {
        const char *lang =
            gtk_combo_box_get_active(GTK_COMBO_BOX(language)) == 1 ? "ru"
                                                                   : "en";
        char *error = NULL;
        if (!kr_linux_save_language(lang, &error)) {
            message(error);
            sfree(error);
            break;
        }
        g_free(saved_query);
        saved_query = g_strdup(gtk_entry_get_text(GTK_ENTRY(search)));
        restart_ui = true;
        gtk_widget_destroy(window);
        window = NULL;
        break;
    }
    }
}
static GtkWidget *button(const char *label, int action) {
    GtkWidget *b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", G_CALLBACK(clicked),
                     GINT_TO_POINTER(action));
    return b;
}
bool kr_linux_dispatch(int argc, char **argv) {
    if (!(argc > 1 && !strcmp(argv[1], "--askpass")))
        for (int i = 1; i + 1 < argc; i++) {
            if (!strcmp(argv[i], "--"))
                break;
            if (!strcmp(argv[i], "--language") &&
                (!strcmp(argv[i + 1], "en") || !strcmp(argv[i + 1], "ru")))
                kr_mac_cli_set_language(argv[++i]);
        }
    if (kr_linux_tools_dispatch(argc, argv))
        return true;
    if (argc >= 4 && !strcmp(argv[1], "--askpass")) {
        if (!strcmp(argv[2], "hostkey") || !strcmp(argv[2], "confirm")) {
            GtkWidget *d = gtk_message_dialog_new(
                NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                "%s", argv[3]);
            gtk_window_set_title(GTK_WINDOW(d), "KiTTY-ng — SSH");
            gtk_dialog_add_button(GTK_DIALOG(d), L("Cancel", "Отмена"),
                                  GTK_RESPONSE_CANCEL);
            gtk_dialog_add_button(GTK_DIALOG(d),
                                  !strcmp(argv[2], "hostkey")
                                      ? L("Accept once", "Принять один раз")
                                      : L("Continue", "Продолжить"),
                                  2);
            if (!strcmp(argv[2], "hostkey"))
                gtk_dialog_add_button(
                    GTK_DIALOG(d), L("Save host key", "Сохранить ключ сервера"),
                    3);
            int result = gtk_dialog_run(GTK_DIALOG(d));
            gtk_widget_destroy(d);
            if (result == 2 || result == 3) {
                fputs(result == 3                   ? "save"
                      : !strcmp(argv[2], "hostkey") ? "once"
                                                    : "yes",
                      stdout);
                return true;
            }
            exit(1);
        }
        char *value = ask(argv[3], "", !strcmp(argv[2], "secret"));
        if (value) {
            fputs(value, stdout);
            smemclr(value, strlen(value));
            g_free(value);
            return true;
        }
        exit(1);
    }
    return false;
}
int kr_linux_browser(void) {
    if (!folder)
        folder = g_strdup("");
    do {
        restart_ui = false;
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(window), "KiTTY-ng");
        GtkWidget *header = gtk_header_bar_new();
        gtk_header_bar_set_title(GTK_HEADER_BAR(header), "KiTTY-ng");
        gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
        gtk_window_set_titlebar(GTK_WINDOW(window), header);
        kr_linux_icon(GTK_WINDOW(window));
        gtk_window_set_default_size(GTK_WINDOW(window), 860, 620);
        g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(box), 18);
        gtk_container_add(GTK_CONTAINER(window), box);
        GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        root_label = gtk_label_new(kr_mac_root());
        gtk_label_set_ellipsize(GTK_LABEL(root_label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_pack_start(GTK_BOX(top), root_label, TRUE, TRUE, 0);
        language = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(language), "English");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(language), "Русский");
        gtk_combo_box_set_active(
            GTK_COMBO_BOX(language),
            strcmp(kr_mac_translate("Cancel"), "Cancel") ? 1 : 0);
        gtk_box_pack_start(GTK_BOX(top), language, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(top), button(L("Apply", "Применить"), 8),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box),
                           button(L("Choose folder…", "Выбрать каталог…"), 7),
                           FALSE, FALSE, 0);
        search = gtk_search_entry_new();
        if (saved_query)
            gtk_entry_set_text(GTK_ENTRY(search), saved_query);
        gtk_entry_set_placeholder_text(GTK_ENTRY(search),
                                       L("Quick search", "Быстрый поиск"));
        g_signal_connect_swapped(search, "changed", G_CALLBACK(reload), NULL);
        gtk_box_pack_start(GTK_BOX(box), search, FALSE, FALSE, 0);
        store =
            gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
        list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store),
                                                compare, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id(
            GTK_TREE_SORTABLE(store), GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
            GTK_SORT_ASCENDING);
        gtk_tree_view_append_column(
            GTK_TREE_VIEW(list),
            gtk_tree_view_column_new_with_attributes(
                L("Saved sessions", "Сохранённые сеансы"),
                gtk_cell_renderer_text_new(), "text", 0, NULL));
        g_signal_connect(list, "row-activated", G_CALLBACK(activate), NULL);
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_container_add(GTK_CONTAINER(scroll), list);
        gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        const char *names[] = {L("New", "Новый"),
                               L("Edit", "Изменить"),
                               L("Delete", "Удалить"),
                               L("Duplicate", "Дублировать"),
                               L("Refresh", "Обновить"),
                               L("Upgrade profiles", "Обновить профили")};
        for (int i = 0; i < 6; i++)
            gtk_box_pack_start(GTK_BOX(actions), button(names[i], i), TRUE,
                               TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);
        GtkWidget *utilities = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(utilities), button("KiTTYgen", 9), TRUE,
                           TRUE, 0);
        gtk_box_pack_start(GTK_BOX(utilities), button("Kageant", 10), TRUE,
                           TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), utilities, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), button(L("Open", "Открыть"), 6), FALSE,
                           FALSE, 0);
        reload();
        gtk_widget_show_all(window);
        gtk_main();
        g_object_unref(store);
    } while (restart_ui);
    return 0;
}
