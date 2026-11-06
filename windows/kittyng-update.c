#include "putty.h"
#include "kittyng.h"
#include "kittyng-update.h"
#include "version.h"
#include <commctrl.h>
#include <winhttp.h>
#include <ctype.h>

static volatile LONG manual_running, manual_result, automatic_running;

static LONG fetch_release(void)
{
    HINTERNET session = WinHttpOpen(L"KiTTY-ng update check",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!session) return -1;
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, L"api.github.com",
                                          INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET",
        L"/repos/nr084/KiTTY-ng/releases/latest", NULL, NULL, NULL,
        WINHTTP_FLAG_SECURE) : NULL;
    LONG result = -1;
    DWORD status = 0, status_size = sizeof(status);
    if (request && WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            NULL, &status, &status_size, NULL) && status == 200) {
        char *body = snewn(131073, char);
        DWORD total = 0, count = 0;
        bool complete = false;
        while (total < 131072 && WinHttpReadData(request, body + total,
                   min(4096, 131072-total), &count)) {
            if (!count) { complete = true; break; }
            total += count;
        }
        body[total] = 0;
        char *tag = complete ? strstr(body, "\"tag_name\"") : NULL;
        if (tag) {
            tag += strlen("\"tag_name\"");
            while (isspace((unsigned char)*tag)) tag++;
            if (*tag++ == ':') {
                while (isspace((unsigned char)*tag)) tag++;
                unsigned major, minor, revision;
                int end = 0;
                if (sscanf(tag, "\"v%5u.%5u-%5u%n", &major, &minor, &revision, &end) == 3 &&
                    end && tag[end] == '"' && major <= 65535 && minor <= 65535 &&
                    revision > 0 && revision <= 65535) {
                    const unsigned current[] = {BINARY_VERSION};
                    const unsigned latest[] = {major, minor, revision};
                    result = 1;
                    for (size_t i = 0; i < lenof(latest); i++) {
                        if (latest[i] != current[i]) {
                            result = latest[i] > current[i] ? 2 : 1;
                            break;
                        }
                    }
                }
            }
        }
        sfree(body);
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

static bool reserve_daily_check(void)
{
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"Local\\KiTTY-ng Update Check");
    if (!mutex) return false;
    DWORD wait = WaitForSingleObject(mutex, 2000);
    bool reserved = false;
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        HKEY key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\KiTTY-NG", 0, NULL,
                           0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            ULONGLONG stamp = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
            ULONGLONG last = 0;
            DWORD size = sizeof(last), type = 0;
            if (RegQueryValueExW(key, L"LastAutomaticUpdateCheck", NULL, &type,
                                (BYTE *)&last, &size) != ERROR_SUCCESS ||
                type != REG_QWORD || size != sizeof(last)) last = 0;
            if (stamp >= last && stamp-last >= 864000000000ULL)
                reserved = RegSetValueExW(key, L"LastAutomaticUpdateCheck", 0,
                           REG_QWORD, (BYTE *)&stamp, sizeof(stamp)) == ERROR_SUCCESS;
            RegCloseKey(key);
        }
        ReleaseMutex(mutex);
    }
    CloseHandle(mutex);
    return reserved;
}

static DWORD WINAPI manual_worker(void *unused)
{
    InterlockedExchange(&manual_result, fetch_release());
    InterlockedExchange(&manual_running, 0);
    return 0;
}

void kr_update_check(void)
{
    if (InterlockedCompareExchange(&manual_running, 1, 0)) return;
    InterlockedExchange(&manual_result, 0);
    HANDLE thread = CreateThread(NULL, 0, manual_worker, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else {
        InterlockedExchange(&manual_result, -1);
        InterlockedExchange(&manual_running, 0);
    }
}

LONG kr_update_result(void)
{
    return InterlockedCompareExchange(&manual_result, 0, 0);
}

static DWORD WINAPI automatic_worker(void *unused)
{
    if (reserve_daily_check() && fetch_release() == 2) {
        wchar_t *message = dup_mb_to_wc(CP_UTF8, kr_translate("A new version is available"));
        wchar_t *download = dup_mb_to_wc(CP_UTF8, kr_translate("Download from the home page"));
        wchar_t *cancel = dup_mb_to_wc(CP_UTF8, kr_translate("Cancel"));
        TASKDIALOG_BUTTON buttons[] = {{IDOK, download}, {IDCANCEL, cancel}};
        TASKDIALOGCONFIG config = { .cbSize = sizeof(config),
            .dwFlags = TDF_ALLOW_DIALOG_CANCELLATION, .pszWindowTitle = L"KiTTY-ng",
            .pszMainInstruction = message, .cButtons = lenof(buttons),
            .pButtons = buttons, .nDefaultButton = IDCANCEL,
            .pszMainIcon = TD_INFORMATION_ICON };
        int button = IDCANCEL;
        if (SUCCEEDED(TaskDialogIndirect(&config, &button, NULL, NULL)) && button == IDOK)
            ShellExecuteW(NULL, L"open", KR_RELEASES_URL, NULL, NULL, SW_SHOWNORMAL);
        sfree(cancel); sfree(download); sfree(message);
    }
    InterlockedExchange(&automatic_running, 0);
    return 0;
}

void kr_update_automatic(void)
{
    if (InterlockedCompareExchange(&automatic_running, 1, 0)) return;
    HANDLE thread = CreateThread(NULL, 0, automatic_worker, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&automatic_running, 0);
}
