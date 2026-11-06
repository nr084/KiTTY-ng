#ifndef KITTYNG_UPDATE_H
#define KITTYNG_UPDATE_H
#define KR_RELEASES_URL L"https://github.com/nr084/KiTTY-ng/releases/latest"
/* 0 pending, 1 current, 2 newer, -1 unavailable. */
void kr_update_check(void);
LONG kr_update_result(void);
void kr_update_automatic(void);
#endif
