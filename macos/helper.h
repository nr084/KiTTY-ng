#ifndef KITTY_NG_MAC_HELPER_H
#define KITTY_NG_MAC_HELPER_H
void kr_mac_begin_tool(void);
void kr_mac_exec_askpass(const char *question);
bool kr_mac_gui_prompts(void);
char *kr_mac_askpass(const char *mode,const char *question);
char *kr_mac_dialog_text(SeatDialogText *text);
bool kr_mac_import_conf(Conf *conf,const char *source);
#endif
