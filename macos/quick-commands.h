#ifndef KITTYNG_MAC_QUICK_COMMANDS_H
#define KITTYNG_MAC_QUICK_COMMANDS_H
#ifdef __OBJC__
#import <AppKit/AppKit.h>
void kr_mac_show_quick_commands(id sender);
void kr_mac_add_quick_settings_menu(NSMenu *menu);
void kr_mac_initialize_quick_commands(void);
int kr_mac_test_quick_commands_ui(void);
#endif
#endif
