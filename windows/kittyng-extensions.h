#ifndef KITTYNG_EXTENSIONS_H
#define KITTYNG_EXTENSIONS_H
/* Stored commands are data for the remote terminal, never local shell code. */
wchar_t *kr_command_text(const char *text, bool escapes, bool submit);
char *kr_ini_string(const char *section, const char *name, const char *fallback);
UINT kr_parse_shortcut(const char *text);
bool kr_script_reference(const char *text);
struct KrBinding { UINT key, action; wchar_t *text; };
char *kr_url_at(const char *line, size_t column);
wchar_t *kr_clipboard_text(const char *osc);
int kr_scroll_lines(int setting, int rows);
int kr_ini_int(const char *name, int fallback);
int kr_ini_milliseconds(const char *name, int fallback);
void kr_load_proxy(Conf *conf);
void kr_copy_proxy(Conf *destination, Conf *source);
struct controlbox;
struct controlset;
void kr_add_proxy_selector(struct controlbox *box, struct controlset *set);
#endif
