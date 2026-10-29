#include "putty.h"
#include "kittyng.h"
#include "kittyng-theme.h"
#include <commctrl.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL: %s\n",#x); return 1; } } while(0)
const char *const appname="theme-test";
void modalfatalbox(const char *format, ...) { abort(); }
static void pump(void) { MSG m;int n=0;while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);assert(++n<10000);} }
static unsigned long paint_hash(HWND hwnd)
{
    RECT r; GetClientRect(hwnd, &r); HDC dc=GetDC(hwnd);
    unsigned long hash=2166136261UL;
    for(int y=0;y<r.bottom;y++) for(int x=0;x<r.right;x++)
        hash=(hash^GetPixel(dc,x,y))*16777619UL;
    ReleaseDC(hwnd,dc);return hash;
}
static bool footer_ok;
static VOID CALLBACK inspect_messagebox(HWND unused, UINT msg, UINT_PTR timer, DWORD time)
{
    HWND box = GetForegroundWindow();
    if (!GetPropW(box, L"KiTTY-ng MessageBox")) return;
    RECT r; GetClientRect(box, &r);
    RedrawWindow(box, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    HDC dc = GetDC(box);
    footer_ok = GetPixel(dc, 4, r.bottom-4) == RGB(32,33,36);
    ReleaseDC(box, dc);
    KillTimer(NULL, timer);
    SendMessageW(box, WM_COMMAND, IDCANCEL, 0);
}

int main(int argc,char **argv)
{
    HKEY isolated=NULL; wchar_t keyname[128];
    bool registry=argc>1 && !strcmp(argv[1],"registry");
    if(registry){
        swprintf(keyname,lenof(keyname),L"Software\\KiTTY-ng-theme-test-%lu",GetCurrentProcessId());
        CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,keyname,0,NULL,0,KEY_ALL_ACCESS,NULL,&isolated,NULL)==ERROR_SUCCESS);
        CHECK(RegOverridePredefKey(HKEY_CURRENT_USER,isolated)==ERROR_SUCCESS);
    }
    INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_TREEVIEW_CLASSES};InitCommonControlsEx(&ic);
    HWND hwnd=CreateWindowW(L"STATIC",L"Theme test",WS_OVERLAPPEDWINDOW,0,0,400,300,NULL,NULL,NULL,NULL);
    CHECK(hwnd);
    HWND edit=CreateWindowW(L"EDIT",L"literal data",WS_CHILD,0,0,100,30,hwnd,(HMENU)1,NULL,NULL);
    HWND tree=CreateWindowW(WC_TREEVIEWW,L"",WS_CHILD,0,40,100,100,hwnd,(HMENU)2,NULL,NULL);
    CHECK(edit && tree);
    HMENU bar=CreateMenu(),popup=CreatePopupMenu();AppendMenuW(bar,MF_POPUP,(UINT_PTR)popup,L"Menu");
    AppendMenuW(popup,MF_STRING,7,L"Item");SetMenu(hwnd,bar);
    MENUITEMINFOW mi={sizeof(mi)};mi.fMask=MIIM_DATA;mi.dwItemData=1234;SetMenuItemInfoW(popup,0,TRUE,&mi);
    kr_theme_attach(hwnd);pump();
    CHECK(kr_theme_save(2));pump();CHECK(kr_theme_dark());
    CHECK(TreeView_GetBkColor(tree)==RGB(26,27,30));
    HWND radio=CreateWindowW(L"BUTTON",L"Long connection caption",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON,
        120,50,250,26,hwnd,(HMENU)3,NULL,NULL);
    HWND check=CreateWindowW(L"BUTTON",L"Include header",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
        120,90,250,26,hwnd,(HMENU)4,NULL,NULL);
    ShowWindow(hwnd,SW_SHOWNORMAL);kr_theme_attach(hwnd);pump();
    for(int i=0;i<2;i++) {
        HWND button=i?check:radio;
        SetWindowTextW(button,i?L"Header":L"COM");
        SendMessageW(button,BM_SETCHECK,BST_CHECKED,0);
        SendMessageW(button,BM_SETSTATE,TRUE,0);
        unsigned long native=paint_hash(button);
        RedrawWindow(button,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
        CHECK(native==paint_hash(button));
        SendMessageW(button,BM_SETSTATE,FALSE,0);
    }

    SendMessageW(hwnd,WM_INITMENUPOPUP,(WPARAM)popup,0);
    mi.fMask=MIIM_DATA|MIIM_FTYPE;GetMenuItemInfoW(popup,0,TRUE,&mi);
    CHECK(mi.dwItemData==1234 && (mi.fType&MFT_OWNERDRAW));
    SendMessageW(hwnd,WM_UNINITMENUPOPUP,(WPARAM)popup,0);
    mi.fMask=MIIM_DATA|MIIM_FTYPE;GetMenuItemInfoW(popup,0,TRUE,&mi);
    CHECK(mi.dwItemData==1234 && !(mi.fType&MFT_OWNERDRAW));
    ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); pump();
    UINT_PTR timer = SetTimer(NULL, 0, 250, inspect_messagebox);
    CHECK(timer);
    CHECK(kr_theme_messagebox(hwnd, L"Close all sessions in this window?", L"Theme test",
                             MB_OKCANCEL | MB_ICONQUESTION, MAKELANGID(LANG_ENGLISH,SUBLANG_DEFAULT)) == IDCANCEL);
    KillTimer(NULL, timer);
    CHECK(footer_ok);
    CHECK(!GetPropW(hwnd, L"KiTTY-ng MessageBox"));
    ShowWindow(hwnd, SW_HIDE);
    CHECK(kr_theme_save(1));pump();CHECK(!kr_theme_dark());
    CHECK(TreeView_GetBkColor(tree)==GetSysColor(COLOR_WINDOW));
    wchar_t value[40];GetWindowTextW(edit,value,lenof(value));CHECK(!wcscmp(value,L"literal data"));
    CHECK(!kr_theme_save(3));CHECK(kr_theme_mode()==1);
    CHECK(kr_theme_save(0));pump();CHECK(kr_theme_mode()==0);
    if(registry){
        HKEY settings;DWORD light=0;
        CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",0,NULL,0,KEY_ALL_ACCESS,NULL,&settings,NULL)==ERROR_SUCCESS);
        CHECK(RegSetValueExW(settings,L"AppsUseLightTheme",0,REG_DWORD,(BYTE *)&light,sizeof(light))==ERROR_SUCCESS);
        SendMessageW(hwnd,WM_SETTINGCHANGE,0,0);pump();CHECK(kr_theme_dark());
        light=1;RegSetValueExW(settings,L"AppsUseLightTheme",0,REG_DWORD,(BYTE *)&light,sizeof(light));
        SendMessageW(hwnd,WM_SETTINGCHANGE,0,0);pump();CHECK(!kr_theme_dark());RegCloseKey(settings);
    }
    DestroyWindow(hwnd);
    if(registry){CHECK(RegOverridePredefKey(HKEY_CURRENT_USER,NULL)==ERROR_SUCCESS);RegCloseKey(isolated);CHECK(RegDeleteTreeW(HKEY_CURRENT_USER,keyname)==ERROR_SUCCESS);}
    puts("PASS: light/dark/auto, live theme refresh, menu data, native controls, settings isolation");return 0;
}
