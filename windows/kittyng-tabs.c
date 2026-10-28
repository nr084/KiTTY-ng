/* A tab frame owns independent terminal processes, retaining the existing
 * SSH, authentication, clipboard and terminal lifecycle in each process. */
#include "putty.h"
#include "kittyng-portable.h"
#include "kittyng-tabs.h"
#include "kittyng.h"
#include "kittyng-tray.h"
#include <shellapi.h>
#include <commctrl.h>
#include <wchar.h>
#include <stdio.h>
#define TAB_CLASS L"KiTTYng.SessionTabs"
#define TAB_ATTACH 0x4b475442u
#define TAB_DUPLICATE 0x0030u
#define TAB_CLOSE 0x7101u
#define TAB_LEFT 0x7102u
#define TAB_RIGHT 0x7103u
#define TAB_MAX 64
#define TAB_SHELL_COMMAND (WM_APP + 0x4B2)
#define TAB_TRAY_EVENT (WM_APP + 0x4B3)
#define TAB_FULLSCREEN 0x0180u
#define TAB_TRAY 0x01B0u

typedef struct {
    HWND frame, strip, pages[TAB_MAX], pressed_close;
    HFONT font;
    HWND dragged;
    POINT drag_start;
    bool dragging;
    int dpi, hover;
    int count, selected;
    bool closing, tray, fullscreen;
    WINDOWPLACEMENT placement;
    LONG_PTR frame_style;
    int system_menu_items;
} TabFrame;

bool kr_tabs_enabled(void)
{
    wchar_t path[32768], value[32];
    const wchar_t *root = kr_portable_root();
    if (root) swprintf(path, lenof(path), L"%ls\\kitty.ini", root);
    else {
        DWORD n = GetModuleFileNameW(NULL, path, lenof(path));
        if (!n || n >= lenof(path)) return true;
        wchar_t *slash = wcsrchr(path, L'\\');
        if (!slash) return true;
        wcscpy(slash + 1, L"kitty.ini");
    }
    GetPrivateProfileStringW(L"KiTTY", L"SessionTabs", L"1", value, lenof(value), path);
    return _wcsicmp(value,L"0") && _wcsicmp(value,L"no") &&
           _wcsicmp(value,L"false") && _wcsicmp(value,L"off");
}
/* Suppress only the initial host transition; never change system animation settings. */
static void frame_initial_animation(HWND hwnd, BOOL disabled)
{
    HMODULE library = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return;
    typedef HRESULT (WINAPI *SetAttribute)(HWND, DWORD, LPCVOID, DWORD);
    SetAttribute set = (SetAttribute)GetProcAddress(library, "DwmSetWindowAttribute");
    if (set) set(hwnd, 3 /* DWMWA_TRANSITIONS_FORCEDISABLED */, &disabled, sizeof(disabled));
    FreeLibrary(library);
}
static bool same_executable(HWND window)
{
    DWORD pid, n = 32768; wchar_t mine[32768], other[32768];
    if (!IsWindow(window) || !GetWindowThreadProcessId(window, &pid)) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    bool ok = process && QueryFullProcessImageNameW(process, 0, other, &n) &&
              GetModuleFileNameW(NULL, mine, lenof(mine)) && !_wcsicmp(mine, other);
    if (process) CloseHandle(process);
    return ok;
}
static bool is_frame(HWND window)
{
    wchar_t name[80];
    return window && GetClassNameW(window, name, lenof(name)) &&
        !wcscmp(name, TAB_CLASS) && same_executable(window);
}
HWND kr_tabs_parent(HWND terminal)
{
    HWND parent = GetParent(terminal);
    return is_frame(parent) ? parent : NULL;
}
bool kr_tabs_attach(HWND host, HWND terminal)
{
    if (!is_frame(host)) return false;
    COPYDATASTRUCT copy = {TAB_ATTACH, sizeof(terminal), &terminal};
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(host, WM_COPYDATA, (WPARAM)terminal, (LPARAM)&copy,
        SMTO_ABORTIFHUNG, 5000, &result) && result;
}
bool kr_tabs_duplicate(HWND terminal)
{
    if (!kr_tabs_enabled() || kr_tabs_parent(terminal)) return false;
    wchar_t exe[32768];
    if (!GetModuleFileNameW(NULL, exe, lenof(exe))) return false;
    wchar_t *line = snewn(32832, wchar_t);
    swprintf(line, 32832, L"\"%ls\" --kitty-tab-host=%p --language %ls", exe, terminal, kr_language()?L"ru":L"en");
    STARTUPINFOW si = { .cb = sizeof(si) }; PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    sfree(line);
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return ok != 0;
}
bool kr_tabs_command(HWND terminal,UINT command)
{
    HWND host=kr_tabs_parent(terminal);
    if(!host)return false;
    PostMessageW(host,TAB_SHELL_COMMAND,command,0);return true;
}
static bool frame_tray(TabFrame *f,bool add)
{
    NOTIFYICONDATAW icon={.cbSize=sizeof(icon),.hWnd=f->frame,.uID=1,
        .uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP,.uCallbackMessage=TAB_TRAY_EVENT};
    icon.hIcon=(HICON)GetClassLongPtrW(f->frame,GCLP_HICON);wcscpy(icon.szTip,L"KiTTY-ng");
    bool result=Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&icon)!=FALSE;
    if(result)f->tray=add;return result;
}
static void frame_fullscreen(TabFrame *f)
{
    if(!f->fullscreen){
        f->placement.length=sizeof(f->placement);GetWindowPlacement(f->frame,&f->placement);
        f->frame_style=GetWindowLongPtrW(f->frame,GWL_STYLE);
        MONITORINFO monitor={.cbSize=sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromWindow(f->frame,MONITOR_DEFAULTTONEAREST),&monitor))return;
        f->fullscreen=true;SetPropW(f->frame,L"KiTTYng.Fullscreen",(HANDLE)1);
        SetWindowLongPtrW(f->frame,GWL_STYLE,f->frame_style&~(WS_CAPTION|WS_THICKFRAME));
        SetWindowPos(f->frame,NULL,monitor.rcMonitor.left,monitor.rcMonitor.top,
            monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_NOZORDER|SWP_FRAMECHANGED);
    }else{
        f->fullscreen=false;RemovePropW(f->frame,L"KiTTYng.Fullscreen");
        SetWindowLongPtrW(f->frame,GWL_STYLE,f->frame_style);SetWindowPlacement(f->frame,&f->placement);
        SetWindowPos(f->frame,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    }
}
static void frame_copy_menu(TabFrame *f,HMENU destination,HMENU source,bool system,int depth)
{
    if(depth>8)return;
    DWORD_PTR result;
    SendMessageTimeoutW(f->pages[f->selected],WM_INITMENUPOPUP,(WPARAM)source,0,SMTO_ABORTIFHUNG,1000,&result);
    bool added=!system;
    for(int i=0;i<GetMenuItemCount(source);i++){
        wchar_t text[1024];MENUITEMINFOW item={.cbSize=sizeof(item),
            .fMask=MIIM_FTYPE|MIIM_STATE|MIIM_ID|MIIM_SUBMENU|MIIM_STRING,.dwTypeData=text,.cch=lenof(text)};
        if(!GetMenuItemInfoW(source,i,TRUE,&item))continue;
        if((item.fType&MFT_SEPARATOR) && !added)continue;
        if(!(item.fType&MFT_SEPARATOR) && !item.hSubMenu && item.wID>=0xf000)continue;
        if(item.fType&MFT_SEPARATOR){AppendMenuW(destination,MF_SEPARATOR,0,NULL);continue;}
        HMENU submenu=NULL;
        if(item.hSubMenu){submenu=CreatePopupMenu();frame_copy_menu(f,submenu,item.hSubMenu,false,depth+1);}
        AppendMenuW(destination,(submenu?MF_POPUP:MF_STRING)|(item.fState&(MFS_DISABLED|MFS_CHECKED)),
            submenu?(UINT_PTR)submenu:item.wID,text);
        added=true;
    }
}
static void frame_update_menu(TabFrame *f)
{
    if(f->selected<0 || f->selected>=f->count)return;
    HMENU menu=GetSystemMenu(f->frame,FALSE);
    while(GetMenuItemCount(menu)>f->system_menu_items)DeleteMenu(menu,f->system_menu_items,MF_BYPOSITION);
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    frame_copy_menu(f,menu,GetSystemMenu(f->pages[f->selected],FALSE),true,0);
}
static void frame_focus(TabFrame *f)
{
    if (f->selected < 0 || f->selected >= f->count) return;
    HWND page = f->pages[f->selected];
    /* Let the terminal's own thread take focus after tab selection finishes.
     * Attaching/detaching queues here loses the foreground input target. */
    PostMessageW(page, KR_WM_TAB_FOCUS, (WPARAM)f->frame, 0);
}
static int tab_px(TabFrame *f,int value) {return MulDiv(value,f->dpi,96);}
static RECT tab_close_rect(TabFrame *f,int index)
{
    RECT r={0};TabCtrl_GetItemRect(f->strip,index,&r);
    int size=tab_px(f,16),right=r.right-tab_px(f,10);
    int top=r.top+(r.bottom-r.top-size)/2;
    return (RECT){right-size,top,right,top+size};
}
static int tab_at(TabFrame *f,POINT point)
{
    TCHITTESTINFO hit={.pt=point};return TabCtrl_HitTest(f->strip,&hit);
}
static void tab_paint(TabFrame *f,HDC target)
{
    RECT bounds;GetClientRect(f->strip,&bounds);
    if(bounds.right<=0 || bounds.bottom<=0)return;
    HDC dc=CreateCompatibleDC(target);
    HBITMAP bitmap=CreateCompatibleBitmap(target,bounds.right,bounds.bottom);
    HGDIOBJ old_bitmap=SelectObject(dc,bitmap),old_font=SelectObject(dc,f->font);
    HBRUSH background=CreateSolidBrush(RGB(242,243,245));FillRect(dc,&bounds,background);DeleteObject(background);
    SetBkMode(dc,TRANSPARENT);
    for(int i=0;i<f->count;i++){
        RECT r;if(!TabCtrl_GetItemRect(f->strip,i,&r) || r.right<0 || r.left>=bounds.right)continue;
        bool active=i==f->selected;
        RECT card=r;InflateRect(&card,-tab_px(f,2),-tab_px(f,2));
        COLORREF color=active?RGB(218,234,253):i==f->hover?RGB(226,229,233):RGB(235,237,240);
        HBRUSH brush=CreateSolidBrush(color);HPEN border=CreatePen(PS_SOLID,1,active?RGB(125,169,223):color);
        HGDIOBJ old_brush=SelectObject(dc,brush),old_pen=SelectObject(dc,border);
        RoundRect(dc,card.left,card.top,card.right,card.bottom,tab_px(f,12),tab_px(f,12));
        SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(brush);DeleteObject(border);
        RECT close=tab_close_rect(f,i);POINT cursor;GetCursorPos(&cursor);ScreenToClient(f->strip,&cursor);
        bool hot=PtInRect(&close,cursor);
        /* Supersample the small circular icon so its outline and white cross
         * remain smooth at 100%, 125%, and 150% display scaling. */
        int diameter=close.right-close.left, scale=4, side=diameter*scale;
        HDC icon=CreateCompatibleDC(dc);HBITMAP surface=CreateCompatibleBitmap(dc,side,side);
        HGDIOBJ saved_bitmap=SelectObject(icon,surface);
        RECT square={0,0,side,side};brush=CreateSolidBrush(color);FillRect(icon,&square,brush);DeleteObject(brush);
        HRGN circle=CreateEllipticRgn(scale,scale,side-scale,side-scale);SelectClipRgn(icon,circle);
        for(int y=0;y<side;y++){
            int red=hot?246-y*45/side:239-y*50/side;
            brush=CreateSolidBrush(hot?RGB(red,87-y*53/side,58-y*40/side):RGB(151-y*25/side,157-y*25/side,165-y*25/side));
            RECT line={0,y,side,y+1};FillRect(icon,&line,brush);DeleteObject(brush);
        }
        brush=CreateSolidBrush(hot?RGB(255,145,117):RGB(193,198,204));
        old_brush=SelectObject(icon,brush);old_pen=SelectObject(icon,GetStockObject(NULL_PEN));
        Ellipse(icon,side/5,scale,side*4/5,side/2);
        SelectObject(icon,old_brush);SelectObject(icon,old_pen);DeleteObject(brush);
        SelectClipRgn(icon,NULL);DeleteObject(circle);
        HPEN cross=CreatePen(PS_SOLID,scale*2,RGB(255,255,255));old_pen=SelectObject(icon,cross);
        int inset=side*5/16;
        MoveToEx(icon,inset,inset,NULL);LineTo(icon,side-inset,side-inset);
        MoveToEx(icon,side-inset-1,inset,NULL);LineTo(icon,inset-1,side-inset);
        SelectObject(icon,old_pen);DeleteObject(cross);
        int old_mode=SetStretchBltMode(dc,HALFTONE);POINT origin;SetBrushOrgEx(dc,0,0,&origin);
        StretchBlt(dc,close.left,close.top,diameter,diameter,icon,0,0,side,side,SRCCOPY);
        SetBrushOrgEx(dc,origin.x,origin.y,NULL);SetStretchBltMode(dc,old_mode);
        SelectObject(icon,saved_bitmap);DeleteObject(surface);DeleteDC(icon);
        wchar_t title[512];GetWindowTextW(f->pages[i],title,lenof(title));
        RECT text=r;text.left+=tab_px(f,14);text.right=close.left-tab_px(f,8);
        SetTextColor(dc,active?RGB(24,65,116):RGB(84,92,103));
        DrawTextW(dc,title,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    BitBlt(target,0,0,bounds.right,bounds.bottom,dc,0,0,SRCCOPY);
    SelectObject(dc,old_font);SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);
}
static void frame_refresh(TabFrame *f);
static void frame_layout(TabFrame *f);
static void tab_move(TabFrame *f,int from,int to)
{
    if(from==to || from<0 || to<0 || from>=f->count || to>=f->count)return;
    HWND active=f->pages[f->selected],moving=f->pages[from];
    if(from<to)memmove(f->pages+from,f->pages+from+1,(to-from)*sizeof(HWND));
    else memmove(f->pages+to+1,f->pages+to,(from-to)*sizeof(HWND));
    f->pages[to]=moving;
    for(int i=0;i<f->count;i++)if(f->pages[i]==active)f->selected=i;
    frame_refresh(f);frame_layout(f);
}
static LRESULT CALLBACK tab_strip_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
    TabFrame *f=(TabFrame*)data;
    POINT point={(short)LOWORD(lp),(short)HIWORD(lp)};
    switch(message){
      case WM_ERASEBKGND:return 1;
      case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);tab_paint(f,dc);EndPaint(hwnd,&ps);return 0;}
      case WM_MOUSEMOVE:{
        f->hover=tab_at(f,point);InvalidateRect(hwnd,NULL,FALSE);
        if(f->dragged && (wp&MK_LBUTTON)){
            if(abs(point.x-f->drag_start.x)>GetSystemMetrics(SM_CXDRAG))f->dragging=true;
            if(f->dragging){
                int from=-1;for(int i=0;i<f->count;i++)if(f->pages[i]==f->dragged)from=i;
                tab_move(f,from,tab_at(f,point));
            }
        }
        TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);break;
      }
      case WM_MOUSELEAVE:f->hover=-1;InvalidateRect(hwnd,NULL,FALSE);break;
      case WM_LBUTTONDOWN:{
        int index=tab_at(f,point);
        if(index>=0 && index<f->count){RECT close=tab_close_rect(f,index);
            RECT hit=close;InflateRect(&hit,tab_px(f,3),tab_px(f,3));
            if(PtInRect(&hit,point)){f->pressed_close=f->pages[index];SetCapture(hwnd);return 0;}
            f->selected=index;TabCtrl_SetCurSel(hwnd,index);frame_layout(f);frame_focus(f);
            f->dragged=f->pages[index];f->drag_start=point;f->dragging=false;SetCapture(hwnd);
            return 0;
        }
        break;
      }
      case WM_LBUTTONUP:
        if(f->pressed_close){
            HWND page=f->pressed_close;f->pressed_close=NULL;ReleaseCapture();
            int index=tab_at(f,point);
            if(index>=0 && index<f->count && f->pages[index]==page){RECT close=tab_close_rect(f,index);
                InflateRect(&close,tab_px(f,3),tab_px(f,3));
                if(PtInRect(&close,point))PostMessageW(page,KR_WM_TAB_CLOSE,(WPARAM)f->frame,0);}
            return 0;
        }
        if(f->dragged){f->dragged=NULL;f->dragging=false;ReleaseCapture();frame_focus(f);return 0;}
        break;
      case WM_CAPTURECHANGED:f->pressed_close=NULL;f->dragged=NULL;f->dragging=false;break;
      case WM_NCDESTROY:RemoveWindowSubclass(hwnd,tab_strip_proc,id);break;
    }
    return DefSubclassProc(hwnd,message,wp,lp);
}
static void frame_layout(TabFrame *f)
{
    RECT r; GetClientRect(f->frame, &r);
    HDC dc = GetDC(f->frame);
    int top = f->count > 1 ? MulDiv(34, GetDeviceCaps(dc, LOGPIXELSY), 96) : 0;
    ReleaseDC(f->frame, dc);
    /* The frame is DPI aware in the same mode as the terminals. */
    MoveWindow(f->strip, 0, 0, max(1,r.right), top, TRUE);
    TabCtrl_SetItemSize(f->strip,tab_px(f,184),tab_px(f,30));
    InvalidateRect(f->strip,NULL,FALSE);
    ShowWindow(f->strip, top ? SW_SHOW : SW_HIDE);
    for (int i=0;i<f->count;i++) {
        MoveWindow(f->pages[i], 0, top, max(1,r.right), max(1,r.bottom-top), TRUE);
        ShowWindow(f->pages[i], i==f->selected ? SW_SHOWNA : SW_HIDE);
    }
    if (f->count && f->selected >= 0) {
        wchar_t title[512]; GetWindowTextW(f->pages[f->selected], title, lenof(title));
        SetWindowTextW(f->frame, title);
    }
}
static void frame_refresh(TabFrame *f)
{
    bool changed = false;
    for (int i=0;i<f->count;) {
        if (!IsWindow(f->pages[i])) {
            memmove(f->pages+i,f->pages+i+1,(f->count-i-1)*sizeof(HWND));
            if (f->selected>=i) f->selected--;
            f->count--; changed=true;
        } else i++;
    }
    if (!f->count) { DestroyWindow(f->frame); return; }
    if (f->selected<0) f->selected=0;
    if (changed) TabCtrl_DeleteAllItems(f->strip);
    for (int i=0;i<f->count;i++) {
        wchar_t title[512]; GetWindowTextW(f->pages[i],title,lenof(title));
        TCITEMW item={ .mask=TCIF_TEXT, .pszText=title };
        if (i>=TabCtrl_GetItemCount(f->strip)) SendMessageW(f->strip,TCM_INSERTITEMW,i,(LPARAM)&item);
        else SendMessageW(f->strip,TCM_SETITEMW,i,(LPARAM)&item);
    }
    TabCtrl_SetCurSel(f->strip,f->selected);
    if (changed) { frame_layout(f); frame_focus(f); }
    else if (f->selected>=0) {
        wchar_t title[512];GetWindowTextW(f->pages[f->selected],title,lenof(title));SetWindowTextW(f->frame,title);
    }
}
/* Keep the active connection selected when another tab is moved. */
static void frame_tab_menu(TabFrame *f)
{
    POINT screen, client;
    GetCursorPos(&screen); client=screen; ScreenToClient(f->strip,&client);
    TCHITTESTINFO hit={ .pt=client };
    int index=TabCtrl_HitTest(f->strip,&hit);
    if(index<0 || index>=f->count)return;
    HWND target=f->pages[index];
    HMENU menu=CreatePopupMenu();
    kr_append_menu(menu,MF_STRING,TAB_CLOSE,"Close tab");
    kr_append_menu(menu,MF_STRING,TAB_DUPLICATE,"Duplicate session");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    kr_append_menu(menu,MF_STRING | (index==0?MF_GRAYED:0),TAB_LEFT,"Move left");
    kr_append_menu(menu,MF_STRING | (index==f->count-1?MF_GRAYED:0),TAB_RIGHT,"Move right");
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,f->frame,NULL);
    DestroyMenu(menu);
    /* The menu runs a nested event loop: a connection can close meanwhile. */
    for(index=0;index<f->count && f->pages[index]!=target;index++);
    if(index==f->count)return;
    if(command==TAB_CLOSE){PostMessageW(target,KR_WM_TAB_CLOSE,(WPARAM)f->frame,0);return;}
    if(command==TAB_DUPLICATE){PostMessageW(target,WM_SYSCOMMAND,TAB_DUPLICATE,0);return;}
    int next=index+(command==TAB_LEFT?-1:command==TAB_RIGHT?1:0);
    if(next==index || next<0 || next>=f->count)return;
    HWND active=f->pages[f->selected];
    f->pages[index]=f->pages[next];f->pages[next]=target;
    for(int i=0;i<f->count;i++)if(f->pages[i]==active)f->selected=i;
    frame_refresh(f);frame_layout(f);frame_focus(f);
}
static bool frame_add(TabFrame *f, HWND page)
{
    if (f->count == TAB_MAX || !same_executable(page) || page == f->frame) return false;
    for (int i=0;i<f->count;i++) if (f->pages[i]==page) return true;
    LONG_PTR old = GetWindowLongPtrW(page,GWL_STYLE);
    ShowWindow(page,SW_HIDE);
    /* SetParent needs WS_CHILD in place to establish the cross-thread input
     * relationship. Setting it afterwards leaves separate keyboard queues. */
    SetWindowLongPtrW(page,GWL_STYLE,(old & ~(WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU|WS_MAXIMIZE|WS_MINIMIZE))|WS_CHILD);
    SetLastError(0);
    HWND previous = SetParent(page,f->frame);
    if (!previous && GetLastError()) {
        SetWindowLongPtrW(page,GWL_STYLE,old);
        ShowWindow(page,SW_SHOW);return false;
    }
    SetWindowPos(page,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    f->pages[f->count++]=page;f->selected=f->count-1;
    frame_refresh(f);frame_layout(f);SetForegroundWindow(f->frame);frame_focus(f);
    return true;
}
static LRESULT CALLBACK frame_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    TabFrame *f=(TabFrame*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if (!f) return DefWindowProcW(hwnd,message,wp,lp);
    switch(message) {
      case TAB_SHELL_COMMAND:
        if(wp==TAB_FULLSCREEN)frame_fullscreen(f);
        else if(wp==TAB_TRAY && frame_tray(f,true))ShowWindow(hwnd,SW_HIDE);
        return 0;
      case TAB_TRAY_EVENT:
        if(lp==WM_LBUTTONUP || lp==WM_LBUTTONDBLCLK){frame_tray(f,false);ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);frame_focus(f);}
        else if(lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU){
            HMENU menu=kr_tray_popup_menu();POINT point;GetCursorPos(&point);SetForegroundWindow(hwnd);
            UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,NULL);
            DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
            if(command==1){frame_tray(f,false);ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);frame_focus(f);}
            else if(command==2 && f->count)PostMessageW(f->pages[f->selected],WM_SYSCOMMAND,0x0150,0);
            else if(command==3)PostMessageW(hwnd,WM_CLOSE,0,0);
        }return 0;
      case WM_INITMENU:frame_update_menu(f);return 0;
      case WM_COPYDATA: {
        COPYDATASTRUCT *copy=(COPYDATASTRUCT*)lp; HWND page;
        if (copy->dwData!=TAB_ATTACH || copy->cbData!=sizeof(page)) return FALSE;
        memcpy(&page,copy->lpData,sizeof(page));
        return page==(HWND)wp && !f->closing && frame_add(f,page);
      }
      case WM_SIZE: frame_layout(f);return 0;
      case WM_SETFOCUS: frame_focus(f);return 0;
      case WM_NOTIFY:
        if (((NMHDR*)lp)->hwndFrom==f->strip && ((NMHDR*)lp)->code==NM_RCLICK) {frame_tab_menu(f);return 0;}
        if (((NMHDR*)lp)->hwndFrom==f->strip && ((NMHDR*)lp)->code==TCN_SELCHANGE) {
            f->selected=TabCtrl_GetCurSel(f->strip);frame_layout(f);frame_focus(f);return 0;
        }break;
      case WM_COMMAND:
        if (LOWORD(wp)==TAB_CLOSE && f->count) {PostMessageW(f->pages[f->selected],KR_WM_TAB_CLOSE,(WPARAM)f->frame,0);return 0;}
        if (f->count) {PostMessageW(f->pages[f->selected],WM_COMMAND,wp,lp);return 0;}break;
      case WM_SYSCOMMAND:
        if (wp<0xf000 && f->count) {PostMessageW(f->pages[f->selected],WM_SYSCOMMAND,wp,lp);return 0;}break;
      case WM_TIMER:
        if(wp==2){KillTimer(hwnd,2);frame_initial_animation(hwnd,FALSE);return 0;}
        frame_refresh(f);return 0;
      case WM_CLOSE: {
        bool warn=false;
        for(int i=0;i<f->count;i++) {
            DWORD_PTR answer=1;
            if(!SendMessageTimeoutW(f->pages[i],KR_WM_TAB_CLOSE_WARNING,0,0,
                SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&answer)) answer=1;
            warn |= answer!=0;
        }
        if (warn && kr_messagebox(hwnd,"Close all sessions in this window?","KiTTY-ng",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return 0;
        f->closing=true;
        for(int i=0;i<f->count;i++) PostMessageW(f->pages[i],KR_WM_TAB_CLOSE,(WPARAM)f->frame,0);
        /* Keep cancelled sessions alive and available. */
        f->closing=false;return 0;
      }
      case WM_DESTROY: if(f->tray)frame_tray(f,false);KillTimer(hwnd,1);DeleteObject(f->font);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
bool kr_tabs_host_entry(HINSTANCE instance, const char *command, int *result)
{
    const char prefix[]="--kitty-tab-host="; HWND source; char extra, language[3];
    if (strncmp(command,prefix,sizeof(prefix)-1)) return false;
    *result=1;
    if (sscanf(command+sizeof(prefix)-1,"%p --language %2s%c",&source,language,&extra)!=2 ||
        (strcmp(language,"en") && strcmp(language,"ru")) || !same_executable(source)) return true;
    WNDCLASSEXW wc={ .cbSize=sizeof(wc), .lpfnWndProc=frame_proc, .hInstance=instance,
        .hCursor=LoadCursor(NULL,IDC_ARROW),.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1),.lpszClassName=TAB_CLASS };
    wc.hIcon=(HICON)GetClassLongPtrW(source,GCLP_HICON);wc.hIconSm=(HICON)GetClassLongPtrW(source,GCLP_HICONSM);
    RegisterClassExW(&wc);
    TabFrame f={ .selected=-1, .hover=-1 };RECT r;GetWindowRect(source,&r);
    bool maximized=IsZoomed(source);
    bool fullscreen=!(GetWindowLongPtrW(source,GWL_STYLE)&WS_CAPTION);
    f.frame=CreateWindowExW(0,TAB_CLASS,L"KiTTY-ng",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,r.left,r.top,r.right-r.left,r.bottom-r.top,NULL,NULL,instance,NULL);
    if (!f.frame) return true;
    frame_initial_animation(f.frame, TRUE);
    SetWindowLongPtrW(f.frame,GWLP_USERDATA,(LONG_PTR)&f);
    f.strip=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN|TCS_BUTTONS|TCS_FLATBUTTONS|TCS_FIXEDWIDTH|TCS_FOCUSNEVER,0,0,1,1,f.frame,NULL,instance,NULL);
    HDC dc=GetDC(f.frame);f.dpi=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(f.frame,dc);
    f.font=CreateFontW(-tab_px(&f,13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    SendMessageW(f.strip,WM_SETFONT,(WPARAM)f.font,TRUE);
    SetWindowSubclass(f.strip,tab_strip_proc,1,(DWORD_PTR)&f);
    if (!frame_add(&f,source)) {DestroyWindow(f.frame);return true;}
    f.system_menu_items=GetMenuItemCount(GetSystemMenu(f.frame,FALSE));
    frame_update_menu(&f);
    /* Set the final host geometry while hidden; child duplicates stay hidden
     * until frame_add reparents them. No standalone maximize animation. */
    if(fullscreen)frame_fullscreen(&f);
    else if(maximized){
        WINDOWPLACEMENT placement={.length=sizeof(placement)};
        GetWindowPlacement(f.frame,&placement);
        placement.showCmd=SW_SHOWMAXIMIZED;
        SetWindowPlacement(f.frame,&placement);
    }
    ShowWindow(f.frame,SW_SHOW);SetTimer(f.frame,1,200,NULL);SetTimer(f.frame,2,500,NULL);
    PostMessageW(source,WM_SYSCOMMAND,TAB_DUPLICATE,0);
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    *result=0;return true;
}
