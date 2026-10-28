#ifndef KITTY_NG_MAC_CLIENT_H
#define KITTY_NG_MAC_CLIENT_H
#include <stdbool.h>
const char *kr_mac_root(void);
bool kr_mac_select_root(const char *path);
void kr_mac_open_session(const char *session, const char *root);
struct conf_tag;
void kr_mac_open_conf(struct conf_tag *conf);
#ifdef __OBJC__
@class NSMenu;
void kr_mac_add_session_menu(NSMenu *menu);
int kr_mac_test_session_menu_ui(void);
int kr_mac_test_tabs_ui(void);
#endif
bool kr_mac_request_termination(void);
void kr_mac_pump(void);
char *kr_mac_directory_at(const char *base,const char *key);
char *kr_mac_session_name_at(const char *base,const char *filename);
#endif
