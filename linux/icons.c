/* One product icon for all GTK windows; packaged resource path is
 * deterministic. */
#include <gtk/gtk.h>
#include <unistd.h>
void kr_linux_icon(GtkWindow *window) {
    const char *app = getenv("APPDIR");
    char *path =
        app ? g_build_filename(
                  app, "usr/share/icons/hicolor/256x256/apps/kitty-ng.png",
                  NULL)
            : NULL;
    GError *error = NULL;
    if (path && gtk_window_set_icon_from_file(window, path, &error)) {
        g_free(path);
        return;
    }
    if (error)
        g_error_free(error);
    g_free(path);
    gtk_window_set_icon_name(window, "utilities-terminal");
}
