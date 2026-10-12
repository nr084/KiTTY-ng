#ifndef KITTY_NG_MAC_TRANSFER_H
#define KITTY_NG_MAC_TRANSFER_H
NSTask *kr_mac_run_helper(NSString *name,NSArray<NSString *> *args,NSString *title,void(^completion)(int,NSString*));
void kr_mac_upload(Conf *conf,NSWindow *owner,NSArray<NSURL *> *files,NSString *directory);
void kr_mac_download(Conf *conf,NSWindow *owner,NSString *directory);
#endif
