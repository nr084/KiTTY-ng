#ifndef KITTYRUS_H
#define KITTYRUS_H

/* UI strings are UTF-8; terminal data and session identifiers are untouched. */
int kr_language(void);
bool kr_set_language(int language);
const char *kr_translate(const char *english);
char *kr_printf(const char *format, ...);
BOOL kr_set_text(HWND hwnd, const char *text);
BOOL kr_set_item_text(HWND hwnd, int id, const char *text);
int kr_messagebox(HWND hwnd, const char *text, const char *caption, UINT flags);
void kr_localize_dialog(HWND hwnd);
void kr_localize_window(HWND hwnd, const char *english);
BOOL kr_append_menu(HMENU menu, UINT flags, UINT_PTR id, const char *english);
BOOL kr_insert_menu(HMENU menu, UINT position, UINT flags, UINT_PTR id, const char *english);
void kr_listbox_add_label(dlgcontrol *ctrl, dlgparam *dp, const char *text);
void kr_listbox_addwithid_raw(dlgcontrol *ctrl, dlgparam *dp, const char *text, int id);
void kr_apply_transparency(HWND hwnd, Conf *conf);
char *kr_prepare_password(Conf *conf);
void kr_load_password(Conf *conf);

#endif
