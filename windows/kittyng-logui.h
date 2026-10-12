#ifndef KITTYNG_LOGUI_H
#define KITTYNG_LOGUI_H
void kr_log_add(HWND dialog, const char *utf8);
void kr_log_extent(HWND dialog);
void kr_log_layout(HWND dialog);
HGLOBAL kr_log_clipboard_text(const char *utf8);
void kr_log_copy(HWND owner, const char *utf8);
#endif
