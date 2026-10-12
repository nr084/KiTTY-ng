#ifndef KITTY_NG_MAC_AUTOMATION_H
#define KITTY_NG_MAC_AUTOMATION_H
char *kr_mac_startup_commands(Conf *conf,char **error);
char *kr_mac_script_text(const Filename *file,char **error);
char *kr_mac_command_text(const char *text,bool escapes,bool submit);
#endif
