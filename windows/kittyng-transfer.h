#ifndef KITTYNG_TRANSFER_H
#define KITTYNG_TRANSFER_H

#define IDM_KR_UPLOAD 0x01C0
#define IDM_KR_DOWNLOAD 0x01D0
#define WM_KR_TRANSFER_FINISHED (WM_APP + 102)
#define KR_TRANSFER_PWD_TIMER 0x4B525054
#define KR_TRANSFER_TEST_TIMER 0x4B525044
#define IDD_KR_REMOTE_FILE 3000
#define IDC_KR_REMOTE_FILE 3001

#ifndef RC_INVOKED
bool kr_transfer_run(Conf *conf, bool upload, const wchar_t *const *paths,
                     size_t count, const char *remote, bool hide,
                     DWORD *status, DWORD *error);
void kr_transfer_choose_upload(HWND owner, Conf *conf, Backend *backend);
void kr_transfer_choose_download(HWND owner, Conf *conf);
void kr_transfer_drop(HWND owner, Conf *conf, Backend *backend, HDROP drop);
bool kr_transfer_title(HWND owner, const char *title, int codepage);
void kr_transfer_cancel_query(HWND owner, bool notify);
extern wchar_t **kr_test_drop_files;
extern size_t kr_test_drop_count;
#endif
#endif
