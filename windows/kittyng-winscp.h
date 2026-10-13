#ifndef KITTYNG_WINSCP_H
#define KITTYNG_WINSCP_H
wchar_t *kr_winscp_path(DWORD *error);
wchar_t *kr_winscp_arguments(Conf *conf, DWORD *error);
bool kr_launch_winscp(Conf *conf, DWORD *error);
#endif
