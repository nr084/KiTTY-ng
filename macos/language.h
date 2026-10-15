#ifndef KITTY_NG_MAC_LANGUAGE_H
#define KITTY_NG_MAC_LANGUAGE_H
int kr_mac_language(void);
bool kr_mac_set_language(int language);
#ifdef __OBJC__
@class NSView;@class NSMenu;@class NSString;
NSString *kr_mac_localize_string(NSString *text);
void kr_mac_localize_view(NSView *view);
void kr_mac_localize_menu(NSMenu *menu);
void kr_mac_add_edit_menu(NSMenu *menu);
#endif
#endif
