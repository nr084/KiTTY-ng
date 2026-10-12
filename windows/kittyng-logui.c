/* Event log UI uses Unicode independently of the selected interface language. */
#include "putty.h"
#include "putty-rc.h"
#include "kittyng-logui.h"

void kr_log_add(HWND dialog, const char *utf8)
{
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, utf8);
    SendDlgItemMessageW(dialog, IDN_LIST, LB_ADDSTRING, 0, (LPARAM)wide);
    sfree(wide);
}

void kr_log_extent(HWND dialog)
{
    HWND list = GetDlgItem(dialog, IDN_LIST);
    HDC dc = GetDC(list);
    HFONT font = (HFONT)SendMessageW(list, WM_GETFONT, 0, 0);
    HGDIOBJ old = font ? SelectObject(dc, font) : NULL;
    int count = SendMessageW(list, LB_GETCOUNT, 0, 0), width = 0;
    for (int i = 0; i < count; i++) {
        LRESULT length = SendMessageW(list, LB_GETTEXTLEN, i, 0);
        if (length == LB_ERR) continue;
        wchar_t *text = snewn(length + 1, wchar_t);
        if (SendMessageW(list, LB_GETTEXT, i, (LPARAM)text) != LB_ERR) {
            RECT rect = {0};
            DrawTextW(dc, text, length, &rect, DT_CALCRECT | DT_EXPANDTABS | DT_SINGLELINE | DT_NOPREFIX);
            if (rect.right > width) width = rect.right;
        }
        sfree(text);
    }
    if (old) SelectObject(dc, old);
    ReleaseDC(list, dc);
    SendMessageW(list, LB_SETHORIZONTALEXTENT, width + 12, 0);
}

void kr_log_layout(HWND dialog)
{
    RECT client, units = {0, 0, 8, 8}, button = {0, 0, 62, 18};
    GetClientRect(dialog, &client);
    MapDialogRect(dialog, &units); MapDialogRect(dialog, &button);
    int margin = units.right, gap = margin, height = button.bottom;
    int y = client.bottom - units.bottom - height;
    int x = (client.right - 2 * button.right - gap) / 2;
    MoveWindow(GetDlgItem(dialog, IDN_LIST), margin, units.bottom,
               max(1, client.right - 2 * margin), max(1, y - 2 * units.bottom), TRUE);
    MoveWindow(GetDlgItem(dialog, IDN_COPY), x, y, button.right, height, TRUE);
    MoveWindow(GetDlgItem(dialog, IDOK), x + button.right + gap, y, button.right, height, TRUE);
}

HGLOBAL kr_log_clipboard_text(const char *utf8)
{
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, utf8);
    size_t bytes = (wcslen(wide) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void *out = memory ? GlobalLock(memory) : NULL;
    if (out) { memcpy(out, wide, bytes); GlobalUnlock(memory); }
    else if (memory) { GlobalFree(memory); memory = NULL; }
    sfree(wide); return memory;
}

void kr_log_copy(HWND owner, const char *utf8)
{
    HGLOBAL memory = kr_log_clipboard_text(utf8);
    if (!memory) return;
    if (OpenClipboard(owner)) {
        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory)) memory = NULL;
        CloseClipboard();
    }
    if (memory) GlobalFree(memory);
}
