#ifndef KG_PATHUTIL_H
#define KG_PATHUTIL_H
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
wchar_t *kg_wdup(const wchar_t *s);
char *kg_utf8(const wchar_t *s);
wchar_t *kg_wide(const char *s);
wchar_t *kg_local_path(const wchar_t *s);
int kg_display_name(const char *raw, wchar_t *out, size_t cap);
char *kg_remote_path(const char *base, const wchar_t *relative);
int kg_random_hex(char out[33]);
FILETIME kg_filetime(uint32_t seconds);
int kg_unix_time(const FILETIME *ft, uint32_t *seconds);
int kg_quote_arg(wchar_t *out, size_t cap, size_t *used, const wchar_t *arg);
#endif
