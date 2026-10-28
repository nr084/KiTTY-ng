/* Exercise production Win32 wrapping before and after terminal initialization. */
#include "putty.h"
#include "misc.h"
#include "kittyng.h"
#include <stdio.h>
const char *const appname = "wrap-test";
HINSTANCE hinst;
void launch_help(HWND hwnd, const char *topic) {}
void modalfatalbox(const char *fmt, ...) { abort(); }
char *staticwrap(struct ctlpos *, HWND, const char *, int *);
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL: %s, line %d\n",#x,__LINE__); return 1; } } while (0)
static const char *labels[] = {
    "Commands run in the remote session after authentication.",
    "Use an existing proxy profile from the Proxies directory or current registry.",
    "System clipboard", "Last selected text",
    "Forwarded ports:", "Add new forwarded port:"
};
static int verify(HWND window) {
    const wchar_t unicode[] = L"\x041f\x0443\x0442\x044c / \x00e9 / \x4e2d / \xd83d\xde80";
    char *encoded = dup_wc_to_mb(CP_UTF8, unicode, "");
    wchar_t *decoded = dup_mb_to_wc(CP_UTF8, encoded);
    CHECK(!wcscmp(unicode, decoded));sfree(encoded);sfree(decoded);
    for (int width=150; width<=450; width+=150) {
        struct ctlpos cp={0};cp.hwnd=window;cp.width=width;
        for (size_t i=0;i<lenof(labels);i++) {
            int lines;char *out=staticwrap(&cp,window,labels[i],&lines);
            CHECK(lines>=1 && lines<=5);
            CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,out,-1,NULL,0)>0);
            for(char *p=out;*p;p++) if(*p=='\n') *p=' ';
            CHECK(!strcmp(out,kr_translate(labels[i])));
            sfree(out);
        }
    }
    return 0;
}
int main(void) {
    HWND window=CreateWindowW(L"STATIC",L"Wrap",WS_OVERLAPPED,0,0,700,700,NULL,NULL,NULL,NULL);
    CHECK(window);
    HFONT font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
    SendMessage(window,WM_SETFONT,(WPARAM)font,0);
    CHECK(!verify(window));
    Conf *conf=conf_new();conf_set_int(conf,CONF_vtmode,VT_UNICODE);
    const char *pages[]={"UTF-8","CP1251","UTF-8"};
    for(size_t i=0;i<lenof(pages);i++) {
        conf_set_str(conf,CONF_line_codepage,pages[i]);
        struct unicode_data data={0};data.font_codepage=1252;
        init_ucs(conf,&data);
        CHECK(!verify(window));
    }
    conf_free(conf);DestroyWindow(window);DeleteObject(font);
    puts("PASS: UTF-8 conversion and Win32 wrapping remain intact before/after terminal initialization");
    return 0;
}
