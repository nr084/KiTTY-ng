#ifndef KITTYNG_COMPANIONS_H
#define KITTYNG_COMPANIONS_H

const char *kr_companion_name(const char *tool);
/* Returned paths are absolute, same-directory, PE-checked and caller-owned. */
wchar_t *kr_companion_path(const char *tool, DWORD *error);
bool kr_launch_companion(const char *tool, const wchar_t *arguments,
                         bool wait, DWORD *exit_code, DWORD *error);
void kr_quote_companion_argument(strbuf *command, const char *argument);
bool kr_launch_companion_conf(const char *tool, const wchar_t *arguments,
                              Conf *conf, bool hide, DWORD *exit_code, DWORD *error);
bool kr_import_companion_conf(Conf *conf, const char *token, DWORD *error);
bool kr_companion_utf8(void);

char *kr_transfer_to_utf8(const wchar_t *text);

#endif
