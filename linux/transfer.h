#ifndef KITTY_NG_LINUX_TRANSFER_H
#define KITTY_NG_LINUX_TRANSFER_H
#include "putty.h"
#include <gtk/gtk.h>
void kr_linux_upload(Conf *, GtkWindow *, char **paths,
                     const char *current_directory);
void kr_linux_download(Conf *, GtkWindow *);
bool kr_linux_transfer_shutdown(void);
#endif
