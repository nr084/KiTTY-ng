#ifndef KITTY_NG_MAC_CLIENT_H
#define KITTY_NG_MAC_CLIENT_H
#include <stdbool.h>
const char *kr_mac_root(void);
bool kr_mac_select_root(const char *path);
void kr_mac_open_session(const char *session, const char *root);
void kr_mac_pump(void);
char *kr_mac_directory_at(const char *base,const char *key);
char *kr_mac_session_name_at(const char *base,const char *filename);
#endif
