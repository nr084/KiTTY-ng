#ifndef KITTYNG_H
#define KITTYNG_H

/* UI strings are UTF-8; terminal data and session identifiers are untouched. */
int kr_language(void);
int kr_windows_language(LANGID language);
BOOL kr_utility_set_item_text(HWND hwnd, int id, const char *text);
BOOL kr_utility_set_raw_item_text(HWND hwnd, int id, const char *text);
int kr_utility_combo_add(HWND hwnd, int id, const char *text);
bool kr_utility_dialog_event(HWND hwnd, UINT message);
void kr_relocalize_menu(HMENU menu);
int kr_console_printf(const char *format, ...);
int kr_console_fprintf(FILE *stream, const char *format, ...);
int kr_console_vfprintf(FILE *stream, const char *format, va_list ap);
char *kr_utility_vprintf(const char *format, va_list ap);
int kr_console_fputs(const char *text, FILE *stream);
bool kr_language_arg(const char *option, const char *value);
bool kr_set_language(int language);
struct controlbox;
struct controlset;
void kr_add_language_controls(struct controlbox *box, struct controlset *set);
const char *kr_translate(const char *english);
const char *kr_untranslate(const char *text);
void kr_utility_localize(HWND hwnd);
void kr_add_utility_language_menu(HWND hwnd, int base);
BOOL kr_utility_append_menu(HMENU menu, UINT flags, UINT_PTR id, const char *text);
void kr_utility_free_menu(HMENU menu);
bool kr_utility_language_command(HWND hwnd, int base, int command);
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
void kr_listbox_addwithid_utf8(dlgcontrol *ctrl, dlgparam *dp, const char *text, int id);
void kr_apply_transparency(HWND hwnd, Conf *conf);
char *kr_prepare_password(Conf *conf);
void kr_load_password(Conf *conf);

#endif
