/* File transfers use the same anonymous Conf pipe as the macOS port. */
#define _GNU_SOURCE
#include "putty.h"
#include <gtk/gtk.h>
#include "transfer.h"
#include "cli-language.h"
#include "portable.h"
#include "storage.h"
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#define L(en, ru) (!strcmp(kr_mac_translate("Cancel"), "Cancel") ? (en) : (ru))
typedef struct Transfer {
    GtkWidget *window, *view, *button;
    GPid pid;
    GString *output;
    guint watches[2];
    int status;
    bool done;
    GIOChannel *channels[2];
} Transfer;
static GList *transfers;
static bool shutting_down;
static void close_job(GtkWidget *widget, gpointer data) {
    Transfer *job = data;
    if (!job->done) {
        kill(-job->pid, SIGTERM);
        gtk_widget_hide(job->window);
    } else
        gtk_widget_destroy(job->window);
}
static gboolean delete_job(GtkWidget *widget, GdkEvent *event, gpointer data) {
    close_job(widget, data);
    return TRUE;
}
static void free_job(gpointer data) {
    Transfer *job = data;
    for (int i = 0; i < 2; i++)
        if (job->channels[i])
            g_io_channel_unref(job->channels[i]);
    g_string_free(job->output, TRUE);
    transfers = g_list_remove(transfers, job);
    g_free(job);
    if (shutting_down && !transfers && gtk_main_level())
        gtk_main_quit();
}
static GIOStatus read_output(GIOChannel *channel, Transfer *job) {
    char buffer[4096];
    gsize n = 0;
    GError *error = NULL;
    GIOStatus status =
        g_io_channel_read_chars(channel, buffer, sizeof(buffer), &n, &error);
    if (n) {
        if (job->output->len + n <= 1048576)
            g_string_append_len(job->output, buffer, n);
        char *text = g_utf8_make_valid(job->output->str, job->output->len);
        gtk_text_buffer_set_text(
            gtk_text_view_get_buffer(GTK_TEXT_VIEW(job->view)), text, -1);
        g_free(text);
    }
    if (error)
        g_error_free(error);
    return n ? G_IO_STATUS_NORMAL : status;
}
static gboolean receive(GIOChannel *channel, GIOCondition condition,
                        gpointer data) {
    Transfer *job = data;
    GIOStatus status = read_output(channel, job);
    bool keep = status != G_IO_STATUS_EOF && status != G_IO_STATUS_ERROR;
    if (!keep) {
        guint id = g_source_get_id(g_main_current_source());
        for (int i = 0; i < 2; i++)
            if (job->watches[i] == id)
                job->watches[i] = 0;
    }
    return keep;
}
static void finished(GPid pid, gint status, gpointer data) {
    Transfer *job = data;
    job->done = true;
    job->status = status;
    g_spawn_close_pid(pid);
    for (int i = 0; i < 2; i++)
        if (job->watches[i]) {
            g_source_remove(job->watches[i]);
            job->watches[i] = 0;
        }
    /* Child exit can precede the last stdout/stderr readiness notification. */
    for (int i = 0; i < 2; i++)
        while (read_output(job->channels[i], job) == G_IO_STATUS_NORMAL) {
        }
    gtk_window_set_title(GTK_WINDOW(job->window),
                         status == 0 ? L("KiTTY-ng — transfer completed",
                                         "KiTTY-ng — передача завершена")
                                     : L("KiTTY-ng — transfer failed",
                                         "KiTTY-ng — ошибка передачи"));
    gtk_button_set_label(GTK_BUTTON(job->button), L("Close", "Закрыть"));
    if (!gtk_widget_get_visible(job->window))
        gtk_widget_destroy(job->window);
}
static void child_setup(gpointer unused) { setpgid(0, 0); }
static char *helper(void) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0)
        return NULL;
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (!slash)
        return NULL;
    *slash = 0;
    return g_build_filename(exe, "kscp", NULL);
}
static void failure(GtkWindow *owner, const char *text) {
    GtkWidget *d =
        gtk_message_dialog_new(owner, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                               GTK_BUTTONS_CLOSE, "%s", text);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}
static void launch(Conf *original, GtkWindow *owner, char **paths, bool upload,
                   const char *remote) {
    if (conf_get_int(original, CONF_protocol) != PROT_SSH) {
        failure(owner, L("File transfer requires SSH",
                         "Для передачи файлов нужен SSH"));
        return;
    }
    char *program = helper();
    struct stat st;
    if (!program || lstat(program, &st) || !S_ISREG(st.st_mode) ||
        access(program, X_OK)) {
        g_free(program);
        failure(owner,
                L("Bundled kscp is unavailable", "Встроенный kscp недоступен"));
        return;
    }
    Conf *conf = conf_copy(original);
    const char *alternate = conf_get_str(conf, CONF_sftpconnect);
    if (*alternate) {
        char *copy = g_strdup(alternate), *host = copy,
             *at = strrchr(copy, '@'), *port = NULL;
        if (at) {
            *at = 0;
            conf_set_utf8(conf, CONF_username, copy);
            host = at + 1;
        }
        if (*host == '[') {
            char *close = strchr(host, ']');
            if (!close) {
                g_free(copy);
                conf_free(conf);
                g_free(program);
                failure(owner, "Invalid SFTPConnect");
                return;
            }
            if (close[1] == ':')
                port = close + 2;
            else if (close[1]) {
                g_free(copy);
                conf_free(conf);
                g_free(program);
                failure(owner, "Invalid SFTPConnect");
                return;
            }
            *close = 0;
            host++;
        } else {
            char *colon = strchr(host, ':');
            if (colon && !strchr(colon + 1, ':')) {
                *colon = 0;
                port = colon + 1;
            }
        }
        if (!*host) {
            g_free(copy);
            conf_free(conf);
            g_free(program);
            failure(owner, "Invalid SFTPConnect");
            return;
        }
        if (port) {
            char *end;
            long value = strtol(port, &end, 10);
            if (!*port || *end || value < 1 || value > 65535) {
                g_free(copy);
                conf_free(conf);
                g_free(program);
                failure(owner, "Invalid SFTPConnect port");
                return;
            }
            conf_set_int(conf, CONF_port, value);
        }
        conf_set_str(conf, CONF_host, host);
        g_free(copy);
    }
    char *port = kr_mac_ini("KiTTY", "pscpport", "*");
    if (*port && strcmp(port, "*")) {
        char *end;
        long value = strtol(port, &end, 10);
        if (*end || value < 1 || value > 65535) {
            sfree(port);
            conf_free(conf);
            g_free(program);
            failure(owner, "Invalid pscpport");
            return;
        }
        conf_set_int(conf, CONF_port, value);
    }
    sfree(port);
    const char *host = conf_get_str(conf, CONF_host),
               *user = conf_get_str_ambi(conf, CONF_username, NULL);
    char *address =
        strchr(host, ':') ? g_strdup_printf("[%s]", host) : g_strdup(host);
    char *destination =
        g_strdup_printf("%s%s%s:%s", user, *user ? "@" : "", address, remote);
    g_free(address);
    GPtrArray *args = g_ptr_array_new();
    g_ptr_array_add(args, program);
    g_ptr_array_add(args, "--kitty-ng-conf");
    g_ptr_array_add(args, "stdin");
    char **options =
        g_strsplit_set(conf_get_str(conf, CONF_pscpoptions), " \t\r\n", -1);
    const char *allowed[] = {"-r", "-p", "-q", "-v", "-scp", "-sftp", "-batch"};
    for (int i = 0; options[i]; i++) {
        if (!*options[i])
            continue;
        bool valid = false;
        for (size_t n = 0; n < lenof(allowed); n++)
            if (!strcmp(options[i], allowed[n]))
                valid = true;
        if (!valid) {
            failure(owner, L("Unsupported PSCPOptions argument",
                             "Неподдерживаемый аргумент PSCPOptions"));
            g_strfreev(options);
            g_ptr_array_free(args, TRUE);
            g_free(program);
            g_free(destination);
            conf_free(conf);
            return;
        }
        g_ptr_array_add(args, options[i]);
    }
    g_ptr_array_add(args, conf_get_int(conf, CONF_winscpprot) == 0 ? "-scp"
                                                                   : "-sftp");
    bool recursive = false;
    for (int i = 0; paths[i]; i++)
        if (g_file_test(paths[i], G_FILE_TEST_IS_DIR))
            recursive = true;
    if (recursive)
        g_ptr_array_add(args, "-r");
    g_ptr_array_add(args, "--");
    if (!upload)
        g_ptr_array_add(args, destination);
    for (int i = 0; paths[i]; i++)
        g_ptr_array_add(args, paths[i]);
    if (upload)
        g_ptr_array_add(args, destination);
    g_ptr_array_add(args, NULL);
    Transfer *job = g_new0(Transfer, 1);
    job->output = g_string_new(NULL);
    char **env = g_get_environ();
    env = g_environ_setenv(env, "KITTY_NG_GUI_PROMPTS", "1", TRUE);
    int input, output, errors;
    GError *error = NULL;
    bool ok = g_spawn_async_with_pipes(
        NULL, (char **)args->pdata, env, G_SPAWN_DO_NOT_REAP_CHILD, child_setup,
        NULL, &job->pid, &input, &output, &errors, &error);
    g_strfreev(env);
    g_ptr_array_free(args, TRUE);
    g_strfreev(options);
    g_free(program);
    g_free(destination);
    if (!ok) {
        failure(owner, error->message);
        g_error_free(error);
        conf_free(conf);
        free_job(job);
        return;
    }
    signal(SIGPIPE, SIG_IGN);
    strbuf *snapshot = strbuf_new();
    conf_serialise(BinarySink_UPCAST(snapshot), conf);
    conf_free(conf);
    unsigned char header[12] = {'K',
                                'N',
                                'G',
                                'M',
                                'A',
                                'C',
                                '1',
                                '\n',
                                snapshot->len >> 24,
                                snapshot->len >> 16,
                                snapshot->len >> 8,
                                snapshot->len};
    for (int part = 0; part < 2; part++) {
        const char *data = part ? snapshot->s : (char *)header;
        size_t len = part ? snapshot->len : sizeof(header);
        while (len) {
            ssize_t n = write(input, data, len);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            data += n;
            len -= n;
        }
    }
    close(input);
    smemclr(snapshot->s, snapshot->len);
    strbuf_free(snapshot);
    job->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(
        GTK_WINDOW(job->window),
        L("KiTTY-ng — file transfer", "KiTTY-ng — передача файлов"));
    gtk_window_set_default_size(GTK_WINDOW(job->window), 680, 420);
    if (owner)
        gtk_window_set_transient_for(GTK_WINDOW(job->window), owner);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    gtk_container_add(GTK_CONTAINER(job->window), box);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    job->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(job->view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(job->view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), job->view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    job->button = gtk_button_new_with_label(L("Cancel", "Отмена"));
    g_signal_connect(job->button, "clicked", G_CALLBACK(close_job), job);
    g_signal_connect(job->window, "delete-event", G_CALLBACK(delete_job), job);
    gtk_box_pack_start(GTK_BOX(box), job->button, FALSE, FALSE, 0);
    g_object_set_data_full(G_OBJECT(job->window), "transfer", job, free_job);
    int streams[] = {output, errors};
    for (int i = 0; i < 2; i++) {
        GIOChannel *channel = g_io_channel_unix_new(streams[i]);
        g_io_channel_set_encoding(channel, NULL, NULL);
        g_io_channel_set_close_on_unref(channel, TRUE);
        g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL);
        job->watches[i] = g_io_add_watch(channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                         receive, job);
        job->channels[i] = channel;
    }
    g_child_watch_add(job->pid, finished, job);
    transfers = g_list_prepend(transfers, job);
    gtk_widget_show_all(job->window);
}
void kr_linux_upload(Conf *conf, GtkWindow *owner, char **paths,
                     const char *current_directory) {
    const char *remote = conf_get_str(conf, CONF_pscpremotedir);
    if (conf_get_bool(conf, CONF_scp_auto_pwd) && current_directory &&
        *current_directory)
        remote = current_directory;
    launch(conf, owner, paths, true, *remote ? remote : ".");
}
void kr_linux_download(Conf *conf, GtkWindow *owner) {
    GtkWidget *d = gtk_dialog_new_with_buttons(
        L("Receive file…", "Получить файл…"), owner, GTK_DIALOG_MODAL,
        L("Cancel", "Отмена"), GTK_RESPONSE_CANCEL, L("Download", "Скачать"),
        GTK_RESPONSE_OK, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(
        GTK_ENTRY(entry),
        L("Remote file or directory", "Удалённый файл или каталог"));
    gtk_container_set_border_width(
        GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), 16);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))),
                      entry);
    gtk_widget_show_all(d);
    char *remote = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK
                       ? g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)))
                       : NULL;
    gtk_widget_destroy(d);
    if (!remote || !*remote) {
        g_free(remote);
        return;
    }
    d = gtk_file_chooser_dialog_new(
        L("Save in folder", "Сохранить в папке"), owner,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, L("Cancel", "Отмена"),
        GTK_RESPONSE_CANCEL, L("Select", "Выбрать"), GTK_RESPONSE_OK, NULL);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d), g_get_home_dir());
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        char *paths[] = {path, NULL};
        launch(conf, owner, paths, false, remote);
        g_free(path);
    }
    gtk_widget_destroy(d);
    g_free(remote);
}

bool kr_linux_transfer_shutdown(void) {
    shutting_down = true;
    for (GList *entry = transfers; entry;) {
        Transfer *job = entry->data;
        entry = entry->next;
        close_job(NULL, job);
    }
    return transfers != NULL;
}
