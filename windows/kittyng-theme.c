#include "putty.h"
#include "kittyng.h"
#include "kittyng-portable.h"
#include "kittyng-theme.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>

#define THEME_REFRESH (WM_APP + 162)
#define THEME_SUBCLASS 0x4b475448
static HBRUSH dark_surface, dark_field;
static int preference = -1;
static bool dark;
static UINT theme_changed;
static bool awaiting_messagebox;

int kr_theme_mode(void)
{
    if (preference >= 0) return preference;
    DWORD value = 0;
    const wchar_t *root = kr_portable_root();
    if (root) {
        wchar_t *ini = dupwcscat(root, L"\\kitty.ini");
        value = GetPrivateProfileIntW(L"KiTTY", L"UITheme", 0, ini);
        sfree(ini);
    } else {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\KiTTY-NG", 0,
                          KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
            DWORD size = sizeof(value), type;
            if (RegQueryValueExW(key, L"UITheme", NULL, &type,
                                (BYTE *)&value, &size) != ERROR_SUCCESS ||
                type != REG_DWORD) value = 0;
            RegCloseKey(key);
        }
    }
    return preference = value <= 2 ? value : 0;
}
static void resolve_theme(void)
{
    HIGHCONTRASTW hc = {sizeof(hc)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) &&
        (hc.dwFlags & HCF_HIGHCONTRASTON)) { dark = false; return; }
    int mode = kr_theme_mode();
    DWORD light = 1, size = sizeof(light);
    HKEY key;
    if (!mode && RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL, (BYTE *)&light, &size);
        RegCloseKey(key);
    }
    dark = mode == 2 || (!mode && !light);
}
bool kr_theme_dark(void) { resolve_theme(); return dark; }
COLORREF kr_theme_background(void) { return kr_theme_dark() ? RGB(32,33,36) : GetSysColor(COLOR_BTNFACE); }
COLORREF kr_theme_foreground(void) { return kr_theme_dark() ? RGB(235,235,238) : GetSysColor(COLOR_WINDOWTEXT); }

static BOOL CALLBACK refresh_top(HWND hwnd, LPARAM unused)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) PostMessageW(hwnd, THEME_REFRESH, 0, 0);
    return TRUE;
}
bool kr_theme_save(int mode)
{
    if (mode < 0 || mode > 2) return false;
    bool ok;
    const wchar_t *root = kr_portable_root();
    if (root) {
        wchar_t *ini = dupwcscat(root, L"\\kitty.ini"), value[2] = {L'0'+mode, 0};
        ok = WritePrivateProfileStringW(L"KiTTY", L"UITheme", value, ini);
        sfree(ini);
    } else {
        HKEY key; DWORD value = mode;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\KiTTY-NG", 0, NULL,
            0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return false;
        ok = RegSetValueExW(key, L"UITheme", 0, REG_DWORD,
                            (BYTE *)&value, sizeof(value)) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    if (ok) { preference = mode; resolve_theme(); EnumWindows(refresh_top, 0);
        if (theme_changed) PostMessageW(HWND_BROADCAST, theme_changed, 0, 0); }
    return ok;
}

/* Keep menu data owned by callers intact (utility localization stores keys
 * there). Our drawing records are separate and restored when the theme changes. */
typedef struct ThemeMenuItem {
    struct ThemeMenuItem *next;
    HWND owner; HMENU menu; UINT position, command, type;
    wchar_t *text;
} ThemeMenuItem;
static ThemeMenuItem *menu_items;
static void restore_menu(HWND owner, HMENU menu)
{
    ThemeMenuItem **link = &menu_items;
    while (*link) {
        ThemeMenuItem *entry = *link;
        if (entry->owner == owner && (!menu || entry->menu == menu)) {
            if (IsMenu(entry->menu)) {
                MENUITEMINFOW info = {sizeof(info)}; info.fMask = MIIM_FTYPE;
                info.fType = entry->type;
                SetMenuItemInfoW(entry->menu, entry->position, TRUE, &info);
            }
            *link = entry->next; sfree(entry->text); sfree(entry);
        } else link = &entry->next;
    }
}
static void theme_menu(HWND owner, HMENU menu)
{
    if (!menu) return;
    restore_menu(owner, menu);
    MENUINFO mi = {sizeof(mi)}; mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = dark ? dark_surface : GetSysColorBrush(COLOR_MENU);
    SetMenuInfo(menu, &mi);
    if (!dark) return;
    for (int i = 0; i < GetMenuItemCount(menu); i++) {
        MENUITEMINFOW info = {sizeof(info)};
        info.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_ID;
        if (!GetMenuItemInfoW(menu, i, TRUE, &info) || (info.fType & MFT_OWNERDRAW)) continue;
        ThemeMenuItem *entry = snew(ThemeMenuItem);
        entry->owner=owner; entry->menu=menu; entry->position=i;
        entry->command=info.wID; entry->type=info.fType;
        entry->text=snewn(info.cch+1,wchar_t); info.dwTypeData=entry->text; info.cch++;
        GetMenuItemInfoW(menu,i,TRUE,&info);
        entry->next=menu_items; menu_items=entry;
        info.fMask=MIIM_FTYPE; info.fType |= MFT_OWNERDRAW;
        SetMenuItemInfoW(menu,i,TRUE,&info);
    }
}
static ThemeMenuItem *menu_entry(HWND owner, HMENU menu, UINT command)
{
    for (ThemeMenuItem *e=menu_items;e;e=e->next)
        if(e->owner==owner && (!menu || e->menu==menu) && e->command==command)return e;
    return NULL;
}
static bool draw_menu(HWND hwnd, UINT msg, LPARAM lp)
{
    if(msg==WM_MEASUREITEM) {
        MEASUREITEMSTRUCT *m=(void *)lp;
        if(m->CtlType!=ODT_MENU)return false;
        ThemeMenuItem *e=menu_entry(hwnd,NULL,m->itemID);if(!e)return false;
        HDC dc=GetDC(hwnd); HFONT old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        SIZE size; GetTextExtentPoint32W(dc,e->text,wcslen(e->text),&size);
        m->itemWidth=size.cx+(e->menu==GetMenu(hwnd)?20:48); m->itemHeight=(e->type&MFT_SEPARATOR)?8:max(size.cy+12,24);
        SelectObject(dc,old); ReleaseDC(hwnd,dc); return true;
    }
    if(msg==WM_DRAWITEM) {
        DRAWITEMSTRUCT *d=(void *)lp;
        if(d->CtlType!=ODT_MENU)return false;
        ThemeMenuItem *e=menu_entry(hwnd,(HMENU)d->hwndItem,d->itemID);if(!e)return false;
        HBRUSH brush=CreateSolidBrush((d->itemState&ODS_SELECTED)?RGB(58,78,105):RGB(32,33,36));
        FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);
        RECT r=d->rcItem;r.left+=24;r.right-=24;
        if(e->type&MFT_SEPARATOR) {
            RECT line=r;line.top=(r.top+r.bottom)/2;line.bottom=line.top+1;
            FillRect(d->hDC,&line,(HBRUSH)GetStockObject(GRAY_BRUSH));return true;
        }
        HFONT old=SelectObject(d->hDC,GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(d->hDC,TRANSPARENT);
        SetTextColor(d->hDC,(d->itemState&(ODS_GRAYED|ODS_DISABLED))?RGB(140,142,147):RGB(235,235,238));
        wchar_t *text=dupwcs(e->text),*shortcut=wcschr(text,L'\t');
        if(shortcut){*shortcut++=0;DrawTextW(d->hDC,shortcut,-1,&r,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);}
        DrawTextW(d->hDC,text,-1,&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER);sfree(text);
        MENUITEMINFOW info={sizeof(info)};info.fMask=MIIM_STATE|MIIM_SUBMENU;
        GetMenuItemInfoW(e->menu,e->position,TRUE,&info);
        if(info.fState&MFS_CHECKED){RECT check=d->rcItem;check.right=check.left+22;DrawTextW(d->hDC,L"\x2713",1,&check,DT_CENTER|DT_SINGLELINE|DT_VCENTER);}
        if(info.hSubMenu && e->menu!=GetMenu(hwnd)){RECT arrow=d->rcItem;arrow.left=arrow.right-20;DrawTextW(d->hDC,L"\x203a",1,&arrow,DT_CENTER|DT_SINGLELINE|DT_VCENTER);}
        SelectObject(d->hDC,old);return true;
    }
    return false;
}

static LRESULT CALLBACK themed_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
static BOOL CALLBACK theme_child(HWND child, LPARAM unused)
{
    wchar_t cls[64]; GetClassNameW(child, cls, lenof(cls));
    /* Native themed scrollbars/edit fields; buttons are drawn below in dark mode. */
    SetWindowTheme(child, dark ? L"DarkMode_Explorer" : NULL, NULL);
    SetWindowSubclass(child, themed_proc, THEME_SUBCLASS, 0);
    if (!_wcsicmp(cls, WC_TREEVIEWW)) {
        TreeView_SetBkColor(child, dark ? RGB(26,27,30) : GetSysColor(COLOR_WINDOW));
        TreeView_SetTextColor(child, dark ? RGB(235,235,238) : GetSysColor(COLOR_WINDOWTEXT));
    }
    if (!_wcsicmp(cls, WC_LISTVIEWW)) {
        ListView_SetBkColor(child, dark ? RGB(26,27,30) : GetSysColor(COLOR_WINDOW));
        ListView_SetTextBkColor(child, dark ? RGB(26,27,30) : GetSysColor(COLOR_WINDOW));
        ListView_SetTextColor(child, dark ? RGB(235,235,238) : GetSysColor(COLOR_WINDOWTEXT));
    }
    InvalidateRect(child, NULL, TRUE);
    return TRUE;
}
static void apply_theme(HWND hwnd)
{
    resolve_theme();
    if (!dark_surface) dark_surface = CreateSolidBrush(RGB(32,33,36));
    if (!dark_field) dark_field = CreateSolidBrush(RGB(26,27,30));
    BOOL enabled = dark;
    DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled));
    restore_menu(hwnd, NULL);
    theme_menu(hwnd, GetMenu(hwnd));
    DrawMenuBar(hwnd);
    EnumChildWindows(hwnd, theme_child, 0);
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
}
static LRESULT CALLBACK theme_activate(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_ACTIVATE && !(GetWindowLongPtrW((HWND)wp, GWL_STYLE) & WS_CHILD))
    {
        if (awaiting_messagebox) {
            SetPropW((HWND)wp, L"KiTTY-ng MessageBox", (HANDLE)1);
            awaiting_messagebox = false;
        }
        kr_theme_attach((HWND)wp);
    }
    return CallNextHookEx(NULL, code, wp, lp);
}
void kr_theme_start(void)
{
    static HHOOK hook;
    if (!hook) hook = SetWindowsHookExW(WH_CBT, theme_activate, NULL, GetCurrentThreadId());
}
/* Mark only MessageBox windows, not arbitrary application dialogs. */
int kr_theme_messagebox(HWND owner, const wchar_t *body,
                        const wchar_t *title, UINT flags, WORD language)
{
    kr_theme_start();
    bool previous = awaiting_messagebox;
    awaiting_messagebox = true;
    int result = MessageBoxExW(owner, body, title, flags, language);
    awaiting_messagebox = previous;
    return result;
}

/* The native MessageBox paints its command area after WM_ERASEBKGND.
 * Repaint that area after native painting, excluding every child control. */
static void paint_messagebox_footer(HWND hwnd)
{
    RECT area; GetClientRect(hwnd, &area);
    int top = area.bottom;
    HDC dc = GetDC(hwnd);
    int saved = SaveDC(dc);
    for (HWND child = GetWindow(hwnd, GW_CHILD); child;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child)) continue;
        RECT r; GetWindowRect(child, &r);
        MapWindowPoints(NULL, hwnd, (POINT *)&r, 2);
        wchar_t cls[32]; GetClassNameW(child, cls, lenof(cls));
        if (!_wcsicmp(cls, L"Button")) top = min(top, r.top);
        ExcludeClipRect(dc, r.left, r.top, r.right, r.bottom);
    }
    RECT gap = {0, 0, 0, 6}; MapDialogRect(hwnd, &gap);
    area.top = max(0, top - gap.bottom);
    FillRect(dc, &area, dark_surface);
    RestoreDC(dc, saved); ReleaseDC(hwnd, dc);
}

void kr_theme_attach(HWND hwnd)
{
    kr_theme_start();
    if (!theme_changed) theme_changed = RegisterWindowMessageW(L"KiTTY-ng.InterfaceThemeChanged");
    SetWindowSubclass(hwnd, themed_proc, THEME_SUBCLASS, 0);
    /* Dialog controls may still be constructed during WM_INITDIALOG. */
    PostMessageW(hwnd, THEME_REFRESH, 0, 0);
}
static bool paint_button(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    int type = style & BS_TYPEMASK;
    if (type == BS_OWNERDRAW || (style & (BS_BITMAP | BS_ICON))) return false;
    PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
    RECT r; GetClientRect(hwnd, &r);
    if (type != BS_GROUPBOX) FillRect(dc, &r, dark_surface);
    HFONT oldfont = SelectObject(dc, (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0));
    bool enabled = IsWindowEnabled(hwnd), focus = GetFocus() == hwnd;
    LRESULT state = SendMessageW(hwnd, BM_GETSTATE, 0, 0);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? RGB(235,235,238) : RGB(140,142,147));
    int n = GetWindowTextLengthW(hwnd); wchar_t *text = snewn(n+1, wchar_t);
    GetWindowTextW(hwnd, text, n+1);
    UINT flags = DT_SINGLELINE | DT_VCENTER;
    if (type == BS_GROUPBOX) {
        TEXTMETRICW metrics; GetTextMetricsW(dc, &metrics);
        SIZE caption; GetTextExtentPoint32W(dc, text, n, &caption);
        int inset = max(4, metrics.tmAveCharWidth);
        RECT box = r; box.top += metrics.tmHeight / 2;
        FrameRect(dc, &box, (HBRUSH)GetStockObject(GRAY_BRUSH));
        r.left += inset * 2; r.bottom = r.top + metrics.tmHeight;
        RECT backing = r;
        backing.left -= inset / 2;
        backing.right = min(r.right, r.left + caption.cx + inset / 2);
        FillRect(dc, &backing, dark_surface);
    } else if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX ||
               type == BS_3STATE || type == BS_AUTO3STATE ||
               type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) {
        int side = min(r.bottom-r.top, GetSystemMetrics(SM_CXMENUCHECK));
        RECT glyph = {r.left, (r.bottom-side)/2, r.left+side, (r.bottom+side)/2};
        UINT kind = (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK;
        if (SendMessageW(hwnd, BM_GETCHECK, 0, 0)) kind |= DFCS_CHECKED;
        if (!enabled) kind |= DFCS_INACTIVE;
        DrawFrameControl(dc, &glyph, DFC_BUTTON, kind);
        r.left += side + 6;
    } else {
        POINT mouse; GetCursorPos(&mouse); ScreenToClient(hwnd, &mouse);
        bool hot = PtInRect(&r, mouse);
        HBRUSH brush = CreateSolidBrush((state & BST_PUSHED) ? RGB(61,76,99) : hot ? RGB(65,68,75) : RGB(48,50,56));
        HPEN pen = CreatePen(PS_SOLID, 1, focus ? RGB(104,164,240) : RGB(88,91,99));
        HGDIOBJ bp = SelectObject(dc, brush), pp = SelectObject(dc, pen);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, 7, 7);
        SelectObject(dc, bp); SelectObject(dc, pp); DeleteObject(brush); DeleteObject(pen);
        flags |= DT_CENTER;
        if (style & BS_MULTILINE) {
            InflateRect(&r, -4, -2);
            flags = DT_CENTER | DT_WORDBREAK;
            RECT measured = r;
            DrawTextW(dc, text, -1, &measured, flags | DT_CALCRECT);
            r.top += max(0, ((r.bottom-r.top)-(measured.bottom-measured.top))/2);
        }
    }
    DrawTextW(dc, text, -1, &r, flags); sfree(text);
    if (focus) { InflateRect(&r, -4, -4); DrawFocusRect(dc, &r); }
    SelectObject(dc, oldfont); EndPaint(hwnd, &ps); return true;
}
static void paint_combo(HWND hwnd)
{
    PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
    RECT r;GetClientRect(hwnd,&r);FillRect(dc,&r,dark_field);
    HBRUSH border=CreateSolidBrush(GetFocus()==hwnd?RGB(104,164,240):RGB(88,91,99));
    FrameRect(dc,&r,border);DeleteObject(border);
    HFONT old=SelectObject(dc,(HFONT)SendMessageW(hwnd,WM_GETFONT,0,0));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(hwnd)?RGB(235,235,238):RGB(140,142,147));
    int n=GetWindowTextLengthW(hwnd);wchar_t *text=snewn(n+1,wchar_t);
    GetWindowTextW(hwnd,text,n+1);r.left+=6;r.right-=24;
    DrawTextW(dc,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);sfree(text);
    RECT arrow=r;arrow.left=r.right;arrow.right+=20;
    DrawTextW(dc,L"\x2304",1,&arrow,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    SelectObject(dc,old);EndPaint(hwnd,&ps);
}

static LRESULT CALLBACK themed_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                    UINT_PTR id, DWORD_PTR data)
{
    if (theme_changed && msg == theme_changed) { preference = -1; apply_theme(hwnd); return 0; }
    if (msg == WM_NCDESTROY) { restore_menu(hwnd,NULL); RemoveWindowSubclass(hwnd, themed_proc, id); return DefSubclassProc(hwnd,msg,wp,lp); }
    if (msg == THEME_REFRESH) { apply_theme(hwnd); return 0; }
    if (msg == WM_SETTINGCHANGE || msg == WM_THEMECHANGED) {
        /* Child SetWindowTheme emits WM_THEMECHANGED: only refresh top-levels. */
        if (!(GetWindowLongPtrW(hwnd,GWL_STYLE) & WS_CHILD)) PostMessageW(hwnd,THEME_REFRESH,0,0);
    }
    if (msg == WM_PARENTNOTIFY && LOWORD(wp) == WM_CREATE) PostMessageW(hwnd,THEME_REFRESH,0,0);
    if (msg == WM_INITMENUPOPUP) {
        LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);
        theme_menu(hwnd,(HMENU)wp);return result;
    }
    if (msg == WM_UNINITMENUPOPUP) restore_menu(hwnd,(HMENU)wp);
    if (dark && (msg == WM_MEASUREITEM || msg == WM_DRAWITEM) && draw_menu(hwnd,msg,lp)) return TRUE;
    if (dark) {
        if (msg == WM_PAINT && GetPropW(hwnd, L"KiTTY-ng MessageBox")) {
            LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
            paint_messagebox_footer(hwnd);
            return result;
        }
        if (msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) {
            SetTextColor((HDC)wp, IsWindowEnabled((HWND)lp) ? RGB(235,235,238) : RGB(140,142,147));
            bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
            SetBkColor((HDC)wp, field ? RGB(26,27,30) : RGB(32,33,36));
            return (LRESULT)(field ? dark_field : dark_surface);
        }
        wchar_t cls[64]; GetClassNameW(hwnd,cls,lenof(cls));
        if (msg == WM_ERASEBKGND && (!wcscmp(cls,L"#32770") || !wcscmp(cls,L"PuTTYHostKeyDialog") || !wcscmp(cls,L"PuTTYConfigBox") || !wcscmp(cls,L"PuTTYHostKeyMoreInfo"))) {
            RECT r; GetClientRect(hwnd,&r); FillRect((HDC)wp,&r,dark_surface); return 1;
        }
        if (!_wcsicmp(cls,L"ComboBox") && (GetWindowLongPtrW(hwnd,GWL_STYLE)&3)==CBS_DROPDOWNLIST) {
            if(msg==WM_PAINT){paint_combo(hwnd);return 0;}
            if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS || msg==CB_SETCURSEL || msg==WM_ENABLE)InvalidateRect(hwnd,NULL,FALSE);
        }
        if (!_wcsicmp(cls,L"Button")) {
            if (msg == WM_PAINT && paint_button(hwnd)) return 0;
            if (msg == WM_MOUSEMOVE) { TRACKMOUSEEVENT t = {sizeof(t),TME_LEAVE,hwnd,0}; TrackMouseEvent(&t); InvalidateRect(hwnd,NULL,FALSE); }
            if (msg == WM_MOUSELEAVE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == BM_SETCHECK || msg == WM_ENABLE)
                InvalidateRect(hwnd,NULL,FALSE);
        }
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}
