#ifndef KITTYNG_TABS_H
#define KITTYNG_TABS_H
#define KR_WM_TAB_CLOSE (WM_APP + 0x4B0)
#define KR_WM_TAB_CLOSE_WARNING (WM_APP + 0x4B1)
#define KR_WM_TAB_FOCUS (WM_APP + 0x4B4)
bool kr_tabs_enabled(void);
bool kr_tabs_host_entry(HINSTANCE instance, const char *command, int *result);
bool kr_tabs_duplicate(HWND terminal);
HWND kr_tabs_parent(HWND terminal);
bool kr_tabs_attach(HWND host, HWND terminal);
bool kr_tabs_command(HWND terminal, UINT command);
#endif
