/* Destination policy for uploads; callers only pass current when enabled. */
#ifndef KITTY_NG_MAC_TRANSFER_PATH_H
#define KITTY_NG_MAC_TRANSFER_PATH_H
static const char *kr_mac_upload_path(const char *current,const char *configured){
    return current&&*current?current:configured&&*configured?configured:".";
}
#endif
