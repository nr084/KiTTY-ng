/* Same event selection as PuTTY, with explicit DLL cleanup. */
#include "windows/select-cli.c"
void kg_engine_select_cleanup(void)
{
    if (winselcli_sockets) {
        SOCKET *p;
        while ((p = delpos234(winselcli_sockets, 0)) != NULL) sfree(p);
        freetree234(winselcli_sockets); winselcli_sockets = NULL;
    }
    if (winselcli_event != INVALID_HANDLE_VALUE) CloseHandle(winselcli_event);
    winselcli_event = INVALID_HANDLE_VALUE;
}
