#ifndef KITTY_NG_MAC_CLI_LANGUAGE_H
#define KITTY_NG_MAC_CLI_LANGUAGE_H
const char *kr_mac_translate(const char *text);
void kr_mac_cli_set_language(const char *language);
int kr_mac_printf(const char *format,...);
int kr_mac_fprintf(FILE *stream,const char *format,...);
int kr_mac_vfprintf(FILE *stream,const char *format,va_list ap);
int kr_mac_fputs(const char *text,FILE *stream);
char *kr_mac_dupprintf(const char *format,...);
char *kr_mac_dupvprintf(const char *format,va_list ap);
#endif
