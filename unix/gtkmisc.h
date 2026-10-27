/*
 * Miscellaneous helper functions for GTK.
 */

#ifndef PUTTY_GTKMISC_H
#define PUTTY_GTKMISC_H

int string_width(const char *text);
void get_label_text_dimensions(const char *text, int *width, int *height);

void align_label_left(GtkLabel *label);

GtkWidget *our_dialog_new(void);
void our_dialog_add_to_content_area(GtkWindow *dlg, GtkWidget *w,
                                    bool expand, bool fill, guint padding);
void our_dialog_set_action_area(GtkWindow *dlg, GtkWidget *w);
GtkBox *our_dialog_make_action_hbox(GtkWindow *dlg);

#ifdef KITTY_NG_LINUX
bool kr_linux_windows_open(void);
bool kr_linux_tab_pending(void);
#endif

#endif /* PUTTY_GTKMISC_H */
