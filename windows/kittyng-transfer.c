#include <wchar.h>
#include "putty.h"
#include <shellapi.h>
#include <commdlg.h>
#include "kittyng.h"
#include "kittyng-portable.h"
#include "kittyng-companions.h"
#include "kittyng-transfer.h"

typedef struct TransferJob {
    HWND owner;
    Conf *conf;
    wchar_t **paths;
    size_t count;
    bool upload;
    char *remote;
    char query_prefix[64];
} TransferJob;
static TransferJob *pending;
wchar_t **kr_test_drop_files;
size_t kr_test_drop_count;

static wchar_t *transfer_ini(void)
{
    const wchar_t *root = kr_portable_root();
    wchar_t module[32768];
    if (!root) {
        DWORD length = GetModuleFileNameW(NULL, module, lenof(module));
        if (!length || length >= lenof(module)) return NULL;
        wchar_t *slash = wcsrchr(module, L'\\');
        if (!slash) return NULL;
        *slash = 0; root = module;
    }
    wchar_t *ini = snewn(wcslen(root) + 12, wchar_t);
    wcscpy(ini, root); wcscat(ini, L"\\kitty.ini");
    return ini;
}

static char *transfer_utf8(const char *text)
{
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0))
        return dupstr(text);
    wchar_t *wide = dup_mb_to_wc(CP_ACP, text);
    char *utf8 = kr_transfer_to_utf8(wide);
    sfree(wide); return utf8;
}

static void add_argument(strbuf *command, const char *argument)
{
    if (command->len) put_byte(command, ' ');
    kr_quote_companion_argument(command, argument);
}

static bool transfer_destination(Conf *conf)
{
    const char *alternate = conf_get_str(conf, CONF_sftpconnect);
    if (*alternate) {
        char *copy = transfer_utf8(alternate), *host = copy;
        char *at = strrchr(copy, '@');
        if (at) { *at = 0; conf_set_utf8(conf, CONF_username, copy); host = at + 1; }
        char *port = NULL;
        if (*host == '[') {
            char *closing = strchr(host, ']');
            if (!closing) { sfree(copy); return false; }
            if (closing[1] == ':') port = closing + 2;
            else if (closing[1]) { sfree(copy); return false; }
            *closing = 0; host++;
        } else {
            char *colon = strchr(host, ':');
            if (colon && !strchr(colon + 1, ':')) { *colon = 0; port = colon + 1; }
        }
        if (!*host) { sfree(copy); return false; }
        if (port) {
            char *end; long number = strtol(port, &end, 10);
            if (!*port || *end || number < 1 || number > 65535) { sfree(copy); return false; }
            conf_set_int(conf, CONF_port, number);
        }
        conf_set_str(conf, CONF_host, host);
        sfree(copy);
    }
    wchar_t *ini = transfer_ini(), port[32];
    if (ini) {
        GetPrivateProfileStringW(L"KiTTY", L"pscpport", L"*", port, lenof(port), ini);
        sfree(ini);
        if (*port && wcscmp(port, L"*")) {
            wchar_t *end; long number = wcstol(port, &end, 10);
            if (*end || number < 1 || number > 65535) return false;
            conf_set_int(conf, CONF_port, number);
        }
    }
    return *conf_get_str(conf, CONF_host) != 0;
}

bool kr_transfer_run(Conf *original, bool upload, const wchar_t *const *paths,
                     size_t count, const char *remote, bool hide,
                     DWORD *status, DWORD *error)
{
    *error = ERROR_INVALID_PARAMETER; *status = 0;
    if (conf_get_int(original, CONF_protocol) != PROT_SSH) {
        *error = ERROR_NOT_SUPPORTED; return false;
    }
    if (!count || (!upload && (count != 1 || !remote || !*remote))) return false;
    Conf *conf = conf_copy(original);
    strbuf *arguments = strbuf_new();
    bool ok = false;
    if (!transfer_destination(conf)) goto cleanup;
    conf_set_bool(conf, CONF_kr_transfer_utf8, true);
    char *options = transfer_utf8(conf_get_str(conf, CONF_pscpoptions));
    wchar_t *option_line = dup_mb_to_wc(CP_UTF8, options);
    sfree(options);
    wchar_t *with_name = snewn(wcslen(option_line) + 6, wchar_t);
    wcscpy(with_name, L"kscp "); wcscat(with_name, option_line); sfree(option_line);
    int argc; wchar_t **argv = CommandLineToArgvW(with_name, &argc);
    sfree(with_name);
    if (!argv) { *error = GetLastError(); goto cleanup; }
    for (int i = 1; i < argc; i++) {
        char *option = kr_transfer_to_utf8(argv[i]);
        /* The internal snapshot must not be replaced by options from a file. */
        if (!strcmp(option, "--kitty-ng-conf")) {
            sfree(option); LocalFree(argv); goto cleanup;
        }
        add_argument(arguments, option); sfree(option);
    }
    LocalFree(argv);
    add_argument(arguments, conf_get_int(conf, CONF_winscpprot) == 0 ? "-scp" : "-sftp");
    char *directory = transfer_utf8(remote && *remote ? remote : conf_get_str(conf, CONF_pscpremotedir));
    if (!*directory) { sfree(directory); directory = dupstr("."); }
    const char *host = conf_get_str(conf, CONF_host);
    bool ipv6 = strchr(host, ':') && host[0] != '[';
    char *destination = dupprintf("%s%s%s:%s", ipv6 ? "[" : "", host, ipv6 ? "]" : "", directory);
    sfree(directory);
    if (!upload) add_argument(arguments, destination);
    for (size_t i = 0; i < count; i++) {
        if (upload && GetFileAttributesW(paths[i]) == INVALID_FILE_ATTRIBUTES) {
            *error = GetLastError(); sfree(destination); goto cleanup;
        }
        DWORD length = GetFullPathNameW(paths[i], 0, NULL, NULL);
        wchar_t *full = snewn(length ? length : 1, wchar_t);
        if (!length || !GetFullPathNameW(paths[i], length, full, NULL)) {
            *error = GetLastError(); sfree(full); sfree(destination); goto cleanup;
        }
        char *path = kr_transfer_to_utf8(full);
        add_argument(arguments, path);
        sfree(path); sfree(full);
    }
    if (upload) add_argument(arguments, destination);
    sfree(destination);
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, arguments->s);
    ok = kr_launch_companion_conf("kscp", wide, conf, hide, status, error);
    sfree(wide);
  cleanup:
    strbuf_free(arguments); conf_free(conf);
    return ok;
}

static void free_job(TransferJob *job)
{
    for (size_t i = 0; i < job->count; i++) sfree(job->paths[i]);
    sfree(job->paths); sfree(job->remote); conf_free(job->conf); sfree(job);
}

static DWORD WINAPI transfer_worker(void *context)
{
    TransferJob *job = context;
    DWORD status, error;
    bool ok = kr_transfer_run(job->conf, job->upload,
        (const wchar_t *const *)job->paths, job->count, job->remote,
        conf_get_bool(job->conf, CONF_kr_transfer_hide), &status, &error);
    HWND owner = job->owner;
    free_job(job);
    PostMessage(owner, WM_KR_TRANSFER_FINISHED, ok ? status : error, 0);
    return 0;
}

static void transfer_error(HWND owner, const char *message)
{
    kr_messagebox(owner, message, "File transfer", MB_OK | MB_ICONERROR);
}

static void start_job(TransferJob *job)
{
    HANDLE thread = CreateThread(NULL, 0, transfer_worker, job, 0, NULL);
    if (!thread) { transfer_error(job->owner, "Unable to start file transfer"); free_job(job); }
    else CloseHandle(thread);
}

static void submit_upload(TransferJob *job, Backend *backend)
{
    if (conf_get_int(job->conf, CONF_protocol) != PROT_SSH) {
        transfer_error(job->owner, "File transfer is available for SSH sessions only"); free_job(job); return;
    }
    if (conf_get_bool(job->conf, CONF_scp_auto_pwd)) {
        if (pending || !backend || !backend_sendok(backend) ||
            conf_get_bool(job->conf, CONF_no_remote_wintitle)) {
            transfer_error(job->owner, "Unable to query the current remote directory"); free_job(job); return;
        }
        pending = job;
        SetTimer(job->owner, KR_TRANSFER_PWD_TIMER, 5000, NULL);
        static unsigned sequence;
        sprintf(job->query_prefix, "__kitty_ng_pwd_%u:", ++sequence);
        char *query = dupprintf("printf '\\033]2;%s%%s\\007' \"$(pwd)\"\r", job->query_prefix);
        backend_send(backend, query, strlen(query));
        sfree(query);
    } else start_job(job);
}

static TransferJob *new_job(HWND owner, Conf *conf, bool upload)
{
    TransferJob *job = snew(TransferJob);
    memset(job, 0, sizeof(*job));
    job->owner = owner; job->conf = conf_copy(conf); job->upload = upload;
    return job;
}

void kr_transfer_drop(HWND owner, Conf *conf, Backend *backend, HDROP drop)
{
    TransferJob *job = new_job(owner, conf, true);
    job->count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    job->paths = snewn(job->count, wchar_t *);
    for (size_t i = 0; i < job->count; i++) {
        UINT length = DragQueryFileW(drop, i, NULL, 0);
        job->paths[i] = snewn(length + 1, wchar_t);
        DragQueryFileW(drop, i, job->paths[i], length + 1);
    }
    DragFinish(drop);
    if (job->count) submit_upload(job, backend); else free_job(job);
}

void kr_transfer_choose_upload(HWND owner, Conf *conf, Backend *backend)
{
    wchar_t *buffer = snewn(65536, wchar_t);
    memset(buffer, 0, 65536 * sizeof(*buffer));
    wchar_t *title = dup_mb_to_wc(CP_UTF8, kr_translate("Send files..."));
    wchar_t *ini = transfer_ini(), initial[32768] = L"";
    if (ini) { GetPrivateProfileStringW(L"KiTTY", L"uploaddir", L"", initial, lenof(initial), ini); sfree(ini); }
    OPENFILENAMEW picker = {0};
    picker.lStructSize = sizeof(picker); picker.hwndOwner = owner;
    picker.lpstrTitle = title; picker.lpstrFilter = L"All files\0*.*\0\0";
    picker.lpstrFile = buffer; picker.nMaxFile = 65536;
    picker.lpstrInitialDir = *initial ? initial : NULL;
    picker.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&picker)) {
        TransferJob *job = new_job(owner, conf, true);
        wchar_t *next = buffer + wcslen(buffer) + 1;
        if (!*next) {
            job->count = 1; job->paths = snewn(1, wchar_t *); job->paths[0] = dupwcs(buffer);
        } else {
            for (wchar_t *p = next; *p; p += wcslen(p) + 1) job->count++;
            job->paths = snewn(job->count, wchar_t *);
            for (size_t i = 0; i < job->count; i++, next += wcslen(next) + 1) {
                job->paths[i] = snewn(wcslen(buffer) + wcslen(next) + 2, wchar_t);
                wcscpy(job->paths[i], buffer); wcscat(job->paths[i], L"\\"); wcscat(job->paths[i], next);
            }
        }
        submit_upload(job, backend);
    }
    sfree(buffer); sfree(title);
}

static INT_PTR CALLBACK remote_file_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_INITDIALOG) {
        SetWindowLongPtr(dialog, GWLP_USERDATA, lparam);
        kr_localize_dialog(dialog);
        SetDlgItemTextW(dialog, IDC_KR_REMOTE_FILE, (wchar_t *)lparam);
        return true;
    }
    if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL)) {
        if (LOWORD(wparam) == IDOK) {
            wchar_t *path = (wchar_t *)GetWindowLongPtr(dialog, GWLP_USERDATA);
            if (!GetDlgItemTextW(dialog, IDC_KR_REMOTE_FILE, path, 32768)) return true;
        }
        EndDialog(dialog, LOWORD(wparam)); return true;
    }
    return false;
}

void kr_transfer_choose_download(HWND owner, Conf *conf)
{
    if (conf_get_int(conf, CONF_protocol) != PROT_SSH) {
        transfer_error(owner, "File transfer is available for SSH sessions only"); return;
    }
    wchar_t *remote = snewn(32768, wchar_t); remote[0] = 0;
    if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_KR_REMOTE_FILE),
                         owner, remote_file_dialog, (LPARAM)remote) == IDOK) {
        wchar_t *local = snewn(32768, wchar_t); local[0] = 0;
        OPENFILENAMEW picker = {0};
        wchar_t *title = dup_mb_to_wc(CP_UTF8, kr_translate("Receive file..."));
        picker.lStructSize = sizeof(picker); picker.hwndOwner = owner;
        picker.lpstrTitle = title; picker.lpstrFile = local; picker.nMaxFile = 32768;
        picker.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&picker)) {
            TransferJob *job = new_job(owner, conf, false);
            job->remote = kr_transfer_to_utf8(remote);
            job->count = 1; job->paths = snewn(1, wchar_t *); job->paths[0] = dupwcs(local);
            start_job(job);
        }
        sfree(title); sfree(local);
    }
    sfree(remote);
}

bool kr_transfer_title(HWND owner, const char *title, int codepage)
{
    if (strncmp(title, "__kitty_ng_pwd_", 15)) return false;
    if (!pending || pending->owner != owner ||
        strncmp(title, pending->query_prefix, strlen(pending->query_prefix))) return true;
    wchar_t *wide = dup_mb_to_wc(codepage, title + strlen(pending->query_prefix));
    char *remote = kr_transfer_to_utf8(wide); sfree(wide);
    TransferJob *job = pending; pending = NULL;
    KillTimer(owner, KR_TRANSFER_PWD_TIMER);
    if (!*remote || strchr(remote, '\r') || strchr(remote, '\n')) {
        sfree(remote); transfer_error(owner, "Unable to query the current remote directory"); free_job(job); return true;
    }
    job->remote = remote; start_job(job); return true;
}

void kr_transfer_cancel_query(HWND owner, bool notify)
{
    if (pending && pending->owner == owner) {
        TransferJob *job = pending; pending = NULL;
        KillTimer(owner, KR_TRANSFER_PWD_TIMER);
        free_job(job);
        if (notify) transfer_error(owner, "Unable to query the current remote directory");
    }
}
