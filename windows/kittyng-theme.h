/* Global Windows interface theme; terminal palettes remain independent. */
#ifndef KITTYNG_THEME_H
#define KITTYNG_THEME_H
int kr_theme_mode(void);
bool kr_theme_save(int mode);
bool kr_theme_dark(void);
void kr_theme_start(void);
int kr_theme_messagebox(HWND, const wchar_t *, const wchar_t *, UINT, WORD);
void kr_theme_attach(HWND hwnd);
COLORREF kr_theme_background(void);
COLORREF kr_theme_foreground(void);
void kr_add_theme_controls(struct controlbox *, struct controlset *);
#endif
