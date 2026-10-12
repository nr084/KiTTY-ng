#include "putty.h"
#include "kittyng-logui.h"
#include "kittyng-tray.h"
#include "kittyng.h"
#include "kittyng-session-menu.h"
#include "putty-rc.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #x); return 1; } } while (0)
const char *const appname = "windows-repairs-test";
void modalfatalbox(const char *format, ...) { abort(); }
static int check_tree(void)
{
    const char *names[] = {"Default Settings", "z-root", "Work/z", "Work/A", "Work/deep/Same", "Other/Same", "A&B", "\xd0\xa0\xd0\xb0\xd0\xb1\xd0\xbe\xd1\x82\xd0\xb0/\xd0\xa1\xd0\xb5\xd0\xb0\xd0\xbd\xd1\x81"};
    struct sesslist sessions = {0}; sessions.sessions = names; sessions.nsessions = lenof(names);
    HMENU menu = CreatePopupMenu(); CHECK(menu);
    kr_saved_sessions_menu(menu, &sessions, 0x1000, 16, 1024);
    CHECK(GetMenuItemCount(menu) == 5);
    wchar_t text[100]; GetMenuStringW(menu, 0, text, lenof(text), MF_BYPOSITION); CHECK(!wcscmp(text,L"Other"));
    HMENU work = GetSubMenu(menu,1); CHECK(work);
    CHECK(GetSubMenu(work,0)); CHECK(GetMenuItemID(GetSubMenu(work,0),0)==0x1030);
    CHECK(GetMenuItemID(work,1)==0x1020); CHECK(GetMenuItemID(work,2)==0x1010);
    GetMenuStringW(menu,3,text,lenof(text),MF_BYPOSITION); CHECK(!wcscmp(text,L"A&&B"));
    CHECK(GetMenuItemID(GetSubMenu(menu,2),0)==0x1060);
    kr_saved_sessions_menu(menu,&sessions,0x1000,16,2); CHECK(GetMenuItemCount(menu)==2);
    sessions.nsessions=1; kr_saved_sessions_menu(menu,&sessions,0x1000,16,1024);
    CHECK(GetMenuItemCount(menu)==1 && (GetMenuState(menu,0,MF_BYPOSITION)&MF_GRAYED));
    DestroyMenu(menu); return 0;
}
static INT_PTR CALLBACK test_dialog(HWND window,UINT message,WPARAM w,LPARAM l)
{ return message==WM_INITDIALOG; }
int main(int argc,char **argv)
{
    bool tray_only=argc>1&&!strcmp(argv[1],"--tray-only");
    if(!tray_only)CHECK(!check_tree());
    HMENU tray=kr_tray_popup_menu();CHECK(tray && GetMenuItemCount(tray)==5);CHECK(GetMenuDefaultItem(tray,FALSE,0)==1);
    wchar_t caption[100];GetMenuStringW(tray,1,caption,lenof(caption),MF_BYCOMMAND);CHECK(!wcscmp(caption,kr_language()?L"Восстановить":L"Restore"));CHECK(GetMenuItemID(tray,2)==2 && GetMenuItemID(tray,4)==3);DestroyMenu(tray);
    if(tray_only){puts("PASS: tray menu Restore/About/Exit, default item and localization");return 0;}
    HWND window=CreateDialogParamW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDD_LOGBOX),NULL,test_dialog,0); CHECK(window);
    HWND list=GetDlgItem(window,IDN_LIST),copy=GetDlgItem(window,IDN_COPY),close=GetDlgItem(window,IDOK); CHECK(list && copy && close);
    const char *message="2026-10-12 00:00:00\t\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 \xf0\x9f\x98\x80";
    wchar_t *expected=dup_mb_to_wc(CP_UTF8,message); kr_log_add(window,message); kr_log_add(window,"English");
    wchar_t text[128]; SendMessageW(list,LB_GETTEXT,0,(LPARAM)text); CHECK(!wcscmp(text,expected));
    SendMessageW(list,LB_SETSEL,TRUE,0); kr_log_extent(window); CHECK(SendMessageW(list,LB_GETHORIZONTALEXTENT,0,0)>100);
    HGLOBAL memory=kr_log_clipboard_text(message); CHECK(memory); wchar_t *data=GlobalLock(memory); CHECK(data && !wcscmp(data,expected)); GlobalUnlock(memory); GlobalFree(memory); sfree(expected);
    kr_log_layout(window); RECT a,b,c; GetWindowRect(copy,&a);GetWindowRect(close,&b);GetClientRect(window,&c); POINT origin={0};ClientToScreen(window,&origin);
    CHECK(abs((a.left+b.right)-2*origin.x-c.right)<=1); CHECK(a.top==b.top && a.right<b.left);
    CHECK(SendMessageW(list,LB_GETSEL,0,0)>0); DestroyWindow(window);
    CHECK(platform_default_i("ShiftedArrowKeys",0)==1);
    puts("PASS: UTF-8 log display/clipboard buffer, selection and centred buttons; nested Unicode menus with stable IDs; Windows shifted-arrow default"); return 0;
}
