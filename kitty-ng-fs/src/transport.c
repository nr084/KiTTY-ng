#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0a00
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include "transport.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KG_IO_BUFFER 65536u
#define KG_STDERR_BUFFER 4096u
#define KG_STDERR_TAIL 8192u
#define KG_WAIT_SLICE 100u
#define KG_CLEANUP_LIMIT 2000u

struct KgTransport {
    HANDLE input, output, error_pipe, process, job;
    OVERLAPPED data_ov, error_ov;
    int data_pending, error_pending, error_closed;
    int failed, cancelled;
    DWORD timeout_ms;
    KgPoll poll;
    void *poll_ctx;
    ULONGLONG last_poll;
    unsigned char data_buffer[KG_IO_BUFFER];
    unsigned char error_buffer[KG_STDERR_BUFFER];
    char error_tail[KG_STDERR_TAIL + 1u];
    size_t error_size;
    char reason[512];
    char diagnostic[KG_STDERR_TAIL + 1024u];
};

static void close_handle(HANDLE *h)
{
    if (*h && *h != INVALID_HANDLE_VALUE) CloseHandle(*h);
    *h = NULL;
}

static void fail_win32(KgTransport *t, const char *what, DWORD code)
{
    if (!t->reason[0])
        snprintf(t->reason, sizeof(t->reason), "%s (Windows error %lu).",
                 what, (unsigned long)code);
    t->failed = 1;
    if (t->job && t->process) TerminateJobObject(t->job, ERROR_OPERATION_ABORTED);
}

static void fail_text(KgTransport *t, const char *what)
{
    if (!t->reason[0])
        snprintf(t->reason, sizeof(t->reason), "%s", what);
    t->failed = 1;
    if (t->job && t->process) TerminateJobObject(t->job, ERROR_OPERATION_ABORTED);
}

static int check_cancel(KgTransport *t, int force)
{
    ULONGLONG now = GetTickCount64();
    if (t->cancelled) return 0;
    if (t->poll && (force || now - t->last_poll >= KG_WAIT_SLICE)) {
        t->last_poll = now;
        if (t->poll(t->poll_ctx)) {
            t->cancelled = 1;
            fail_text(t, "Operation cancelled.");
            return 0;
        }
    }
    return !t->failed;
}

/* Retain a bounded, printable diagnostic tail, never mix stderr into SFTP. */
static void append_error(KgTransport *t, const unsigned char *data, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char c = data[i];
        if ((c < 32 && c != '\n' && c != '\t') || c == 127) continue;
        if (t->error_size == KG_STDERR_TAIL) {
            memmove(t->error_tail, t->error_tail + 1024,
                    KG_STDERR_TAIL - 1024);
            t->error_size -= 1024;
        }
        t->error_tail[t->error_size++] = (char)c;
    }
    t->error_tail[t->error_size] = 0;
}

/* At most sixteen reads per call: a noisy child must not starve cancellation. */
static void drain_error(KgTransport *t)
{
    unsigned loops = 0;
    if (!t->error_pipe || t->error_closed) return;
    while (loops++ != 16) {
        DWORD bytes = 0, code;
        BOOL ok;
        if (t->error_pending) {
            ok = GetOverlappedResult(t->error_pipe, &t->error_ov, &bytes, FALSE);
            if (!ok && GetLastError() == ERROR_IO_INCOMPLETE) return;
            t->error_pending = 0;
        } else {
            ResetEvent(t->error_ov.hEvent);
            ok = ReadFile(t->error_pipe, t->error_buffer, KG_STDERR_BUFFER,
                          &bytes, &t->error_ov);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                t->error_pending = 1;
                return;
            }
        }
        if (!ok) {
            code = GetLastError();
            (void)code; /* stderr failure must not replace the real SSH error */
            t->error_closed = 1;
            return;
        }
        if (!bytes) {
            t->error_closed = 1;
            return;
        }
        append_error(t, t->error_buffer, bytes);
    }
}

static void note_process_exit(KgTransport *t)
{
    DWORD code = 0;
    if (t->reason[0]) return;
    if (t->process && GetExitCodeProcess(t->process, &code) && code != STILL_ACTIVE)
        snprintf(t->reason, sizeof(t->reason),
                 "KiTTY-ng Klink exited (code %lu).", (unsigned long)code);
    else
        snprintf(t->reason, sizeof(t->reason), "SSH transport closed unexpectedly.");
    t->failed = 1;
}

/* A single overlapped operation always targets the object's bounce buffer,
 * not caller memory. Timeout/cancellation therefore cannot outlive a caller's
 * SFTP buffer, even in the pathological case of delayed kernel cancellation. */
static int exchange_chunk(KgTransport *t, int writing, DWORD amount,
                          ULONGLONG deadline, DWORD *transferred)
{
    HANDLE pipe = writing ? t->input : t->output;
    DWORD code, count = 0;
    BOOL ok;
    if (!check_cancel(t, 0)) return 0;
    if (GetTickCount64() >= deadline) {
        fail_text(t, writing ? "Timed out writing to KiTTY-ng Klink."
                             : "Timed out waiting for KiTTY-ng Klink.");
        return 0;
    }
    drain_error(t);
    ResetEvent(t->data_ov.hEvent);
    ok = writing ? WriteFile(pipe, t->data_buffer, amount, &count, &t->data_ov)
                 : ReadFile(pipe, t->data_buffer, amount, &count, &t->data_ov);
    if (ok) {
        if (!count && amount) {
            note_process_exit(t);
            return 0;
        }
        *transferred = count;
        return 1;
    }
    code = GetLastError();
    if (code != ERROR_IO_PENDING) {
        drain_error(t);
        if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA)
            note_process_exit(t);
        else
            fail_win32(t, writing ? "Writing to Klink failed" : "Reading from Klink failed", code);
        return 0;
    }
    t->data_pending = 1;
    for (;;) {
        HANDLE handles[3];
        DWORD nhandles = 0, wait_ms, result;
        ULONGLONG now;
        drain_error(t);
        if (GetOverlappedResult(pipe, &t->data_ov, &count, FALSE)) {
            t->data_pending = 0;
            if (!count && amount) {
                note_process_exit(t);
                return 0;
            }
            *transferred = count;
            return 1;
        }
        code = GetLastError();
        if (code != ERROR_IO_INCOMPLETE) {
            t->data_pending = 0;
            if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA)
                note_process_exit(t);
            else
                fail_win32(t, writing ? "Writing to Klink failed" : "Reading from Klink failed", code);
            return 0;
        }
        if (!check_cancel(t, 0)) break;
        now = GetTickCount64();
        if (now >= deadline) {
            fail_text(t, writing ? "Timed out writing to KiTTY-ng Klink."
                                 : "Timed out waiting for KiTTY-ng Klink.");
            break;
        }
        wait_ms = (DWORD)(deadline - now > KG_WAIT_SLICE ? KG_WAIT_SLICE : deadline - now);
        handles[nhandles++] = t->data_ov.hEvent;
        if (t->error_pending) handles[nhandles++] = t->error_ov.hEvent;
        if (t->process) handles[nhandles++] = t->process;
        result = WaitForMultipleObjects(nhandles, handles, FALSE, wait_ms);
        if (result == WAIT_FAILED) {
            fail_win32(t, "Waiting for Klink failed", GetLastError());
            break;
        }
        if (t->process && WaitForSingleObject(t->process, 0) == WAIT_OBJECT_0) {
            /* Let an already-completed stdout read deliver its last bytes. */
            if (GetOverlappedResult(pipe, &t->data_ov, &count, FALSE) && count) {
                t->data_pending = 0;
                *transferred = count;
                return 1;
            }
            drain_error(t);
            note_process_exit(t);
            break;
        }
    }
    CancelIoEx(pipe, &t->data_ov);
    return 0;
}

static int read_exact(void *ctx, void *buffer, size_t size)
{
    KgTransport *t = (KgTransport *)ctx;
    unsigned char *p = (unsigned char *)buffer;
    ULONGLONG deadline;
    if (!t || t->failed || (!buffer && size)) return 0;
    if (!check_cancel(t, 0)) return 0;
    deadline = GetTickCount64() + t->timeout_ms;
    while (size) {
        DWORD got = 0, want = (DWORD)(size > KG_IO_BUFFER ? KG_IO_BUFFER : size);
        if (!exchange_chunk(t, 0, want, deadline, &got)) return 0;
        memcpy(p, t->data_buffer, got);
        p += got;
        size -= got;
    }
    return 1;
}

static int write_all(void *ctx, const void *buffer, size_t size)
{
    KgTransport *t = (KgTransport *)ctx;
    const unsigned char *p = (const unsigned char *)buffer;
    ULONGLONG deadline;
    if (!t || t->failed || (!buffer && size)) return 0;
    if (!check_cancel(t, 0)) return 0;
    deadline = GetTickCount64() + t->timeout_ms;
    while (size) {
        DWORD sent = 0, want = (DWORD)(size > KG_IO_BUFFER ? KG_IO_BUFFER : size);
        memcpy(t->data_buffer, p, want);
        if (!exchange_chunk(t, 1, want, deadline, &sent)) return 0;
        p += sent;
        size -= sent;
    }
    return 1;
}

/* Standard Windows argv quoting. Quotes/backslashes in session or directory
 * names remain data; no cmd.exe or other command interpreter is involved. */
static wchar_t *make_command_line(const wchar_t *exe, const wchar_t *root,
                                  const wchar_t *session)
{
    const wchar_t *args[] = {exe, L"--portable-dir", root, L"--language", L"en",
        L"-load", session, L"-batch", L"-T", L"-no-antispoof",
        L"-no-sanitise-stdout", L"-s", L"sftp"};
    size_t i, needed = 1, used = 0;
    wchar_t *cmd;
    for (i = 0; i < sizeof(args) / sizeof(args[0]); ++i) {
        size_t n = wcslen(args[i]);
        if (n > 16000 || needed > 32767 - (n * 2 + 3)) return NULL;
        needed += n * 2 + 3;
    }
    cmd = (wchar_t *)malloc(needed * sizeof(wchar_t));
    if (!cmd) return NULL;
    for (i = 0; i < sizeof(args) / sizeof(args[0]); ++i) {
        const wchar_t *p = args[i];
        if (i) cmd[used++] = L' ';
        cmd[used++] = L'"';
        while (*p) {
            size_t slashes = 0, k;
            while (*p == L'\\') { ++slashes; ++p; }
            if (*p == L'"' || !*p) {
                for (k = 0; k < slashes * 2; ++k) cmd[used++] = L'\\';
                if (*p == L'"') { cmd[used++] = L'\\'; cmd[used++] = *p++; }
            } else {
                for (k = 0; k < slashes; ++k) cmd[used++] = L'\\';
                cmd[used++] = *p++;
            }
        }
        cmd[used++] = L'"';
    }
    cmd[used] = 0;
    return cmd;
}

static int make_user_security(SECURITY_DESCRIPTOR *sd, PACL *acl)
{
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    DWORD need = 0, status;
    EXPLICIT_ACCESSW access;
    int ok = 0;
    *acl = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &need);
    if (!need) goto done;
    user = (TOKEN_USER *)malloc(need);
    if (!user) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto done; }
    if (!GetTokenInformation(token, TokenUser, user, need, &need)) goto done;
    ZeroMemory(&access, sizeof(access));
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = (LPWSTR)user->User.Sid;
    status = SetEntriesInAclW(1, &access, NULL, acl);
    if (status != ERROR_SUCCESS) { SetLastError(status); goto done; }
    if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(sd, TRUE, *acl, FALSE)) goto done;
    ok = 1;
done:
    status = GetLastError();
    free(user);
    close_handle(&token);
    if (!ok && *acl) { LocalFree(*acl); *acl = NULL; }
    SetLastError(status);
    return ok;
}

static int make_pipe_pair(int parent_writes, SECURITY_DESCRIPTOR *sd,
                           HANDLE *parent, HANDLE *child)
{
    unsigned char random_bytes[32];
    static const wchar_t hex[] = L"0123456789abcdef";
    wchar_t name[128] = L"\\\\.\\pipe\\KiTTYngFS-";
    SECURITY_ATTRIBUTES server_sa = {sizeof(server_sa), sd, FALSE};
    SECURITY_ATTRIBUTES child_sa = {sizeof(child_sa), NULL, TRUE};
    size_t n = wcslen(name), i;
    DWORD saved;
    *parent = *child = NULL;
    if (BCryptGenRandom(NULL, random_bytes, sizeof(random_bytes),
                       BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        SetLastError(ERROR_GEN_FAILURE);
        return 0;
    }
    for (i = 0; i < sizeof(random_bytes); ++i) {
        name[n++] = hex[random_bytes[i] >> 4];
        name[n++] = hex[random_bytes[i] & 15];
    }
    name[n] = 0;
    *parent = CreateNamedPipeW(name,
        (parent_writes ? PIPE_ACCESS_OUTBOUND : PIPE_ACCESS_INBOUND) |
        FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, KG_IO_BUFFER, KG_IO_BUFFER, 0, &server_sa);
    if (*parent == INVALID_HANDLE_VALUE) { *parent = NULL; return 0; }
    *child = CreateFileW(name, parent_writes ? GENERIC_READ : GENERIC_WRITE,
        0, &child_sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (*child == INVALID_HANDLE_VALUE) {
        saved = GetLastError();
        *child = NULL;
        close_handle(parent);
        SetLastError(saved);
        return 0;
    }
    /* Successful CreateFile connects the sole server instance. Calling
     * ConnectNamedPipe now would just report ERROR_PIPE_CONNECTED. */
    return 1;
}

static void copy_start_error(KgTransport *t, wchar_t *out, size_t cap)
{
    const char *msg;
    int n;
    if (!out || !cap) return;
    out[0] = 0;
    msg = kg_transport_error(t);
    n = MultiByteToWideChar(CP_UTF8, 0, msg, -1, out, (int)(cap > INT_MAX ? INT_MAX : cap));
    if (!n) {
        const wchar_t fallback[] = L"Cannot start KiTTY-ng Klink.";
        size_t count = sizeof(fallback) / sizeof(fallback[0]) - 1;
        if (count >= cap) count = cap - 1;
        memcpy(out, fallback, count * sizeof(wchar_t));
        out[count] = 0;
    }
}

KgTransport *kg_transport_start(const wchar_t *exe, const wchar_t *root,
    const wchar_t *session, DWORD timeout_ms, KgPoll poll, void *poll_ctx,
    wchar_t *error, size_t error_cap)
{
    KgTransport *t = (KgTransport *)calloc(1, sizeof(*t));
    SECURITY_DESCRIPTOR sd;
    PACL acl = NULL;
    HANDLE child[3] = {NULL, NULL, NULL};
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    SIZE_T attr_size = 0;
    wchar_t *cmd = NULL;
    int attr_initialized = 0;
    DWORD saved;
    size_t i;
    if (error && error_cap) error[0] = 0;
    if (!t) {
        if (error && error_cap > 1) {
            const wchar_t text[] = L"Out of memory.";
            size_t n = sizeof(text) / sizeof(text[0]) - 1;
            if (n >= error_cap) n = error_cap - 1;
            memcpy(error, text, n * sizeof(wchar_t)); error[n] = 0;
        }
        return NULL;
    }
    t->timeout_ms = timeout_ms ? timeout_ms : 30000;
    t->poll = poll;
    t->poll_ctx = poll_ctx;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&limits, sizeof(limits));
    if (!exe || !*exe || !root || !*root || !session || !*session) {
        fail_text(t, "Executable, portable directory and session name are required.");
        goto failed;
    }
    if (!check_cancel(t, 1)) goto failed;
    cmd = make_command_line(exe, root, session);
    if (!cmd) { fail_text(t, "Command line is too long or memory is unavailable."); goto failed; }
    t->data_ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    t->error_ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!t->data_ov.hEvent || !t->error_ov.hEvent) {
        fail_win32(t, "Creating I/O events failed", GetLastError()); goto failed;
    }
    if (!make_user_security(&sd, &acl)) {
        fail_win32(t, "Creating private pipe security failed", GetLastError()); goto failed;
    }
    if (!make_pipe_pair(1, &sd, &t->input, &child[0]) ||
        !make_pipe_pair(0, &sd, &t->output, &child[1]) ||
        !make_pipe_pair(0, &sd, &t->error_pipe, &child[2])) {
        fail_win32(t, "Creating private Klink pipes failed", GetLastError()); goto failed;
    }
    LocalFree(acl); acl = NULL;
    t->job = CreateJobObjectW(NULL, NULL);
    if (!t->job) { fail_win32(t, "Creating Klink job failed", GetLastError()); goto failed; }
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(t->job, JobObjectExtendedLimitInformation,
                                  &limits, sizeof(limits))) {
        fail_win32(t, "Configuring Klink job failed", GetLastError()); goto failed;
    }
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    if (!attr_size) { fail_win32(t, "Creating process attributes failed", GetLastError()); goto failed; }
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!si.lpAttributeList) { fail_text(t, "Out of memory creating process attributes."); goto failed; }
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size)) {
        fail_win32(t, "Initializing process attributes failed", GetLastError()); goto failed;
    }
    attr_initialized = 1;
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, child, sizeof(child), NULL, NULL)) {
        fail_win32(t, "Restricting inherited handles failed", GetLastError()); goto failed;
    }
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = child[0];
    si.StartupInfo.hStdOutput = child[1];
    si.StartupInfo.hStdError = child[2];
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
        NULL, root, &si.StartupInfo, &pi)) {
        fail_win32(t, "Starting KiTTY-ng Klink failed", GetLastError()); goto failed;
    }
    t->process = pi.hProcess;
    if (!AssignProcessToJobObject(t->job, t->process)) {
        saved = GetLastError();
        TerminateProcess(t->process, 1); /* still suspended: no child code ran */
        fail_win32(t, "Assigning Klink to its private job failed", saved); goto failed;
    }
    if (ResumeThread(pi.hThread) == (DWORD)-1) {
        fail_win32(t, "Resuming KiTTY-ng Klink failed", GetLastError()); goto failed;
    }
    close_handle(&pi.hThread);
    for (i = 0; i < 3; ++i) close_handle(&child[i]);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    free(cmd);
    drain_error(t);
    return t;
failed:
    close_handle(&pi.hThread);
    for (i = 0; i < 3; ++i) close_handle(&child[i]);
    if (attr_initialized) DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    if (acl) LocalFree(acl);
    free(cmd);
    copy_start_error(t, error, error_cap);
    kg_transport_close(t);
    return NULL;
}

SfIO kg_transport_io(KgTransport *t)
{
    SfIO io;
    io.ctx = t;
    io.read_exact = read_exact;
    io.write_all = write_all;
    return io;
}

const char *kg_transport_error(KgTransport *t)
{
    if (!t) return "SSH transport is unavailable.";
    drain_error(t);
    if (!t->reason[0] && t->process &&
        WaitForSingleObject(t->process, 0) == WAIT_OBJECT_0) note_process_exit(t);
    snprintf(t->diagnostic, sizeof(t->diagnostic), "%s%s%s",
        t->reason, t->reason[0] && t->error_size ? "\n" : "", t->error_tail);
    return t->diagnostic;
}

int kg_transport_cancelled(KgTransport *t)
{
    return t ? t->cancelled : 0;
}

int kg_transport_alive(KgTransport *t)
{
    if (!t || t->failed || !t->process) return 0;
    drain_error(t);
    if (WaitForSingleObject(t->process, 0) != WAIT_TIMEOUT) {
        note_process_exit(t);
        return 0;
    }
    return 1;
}

static void dispose(KgTransport *t, int wait_allowed)
{
    ULONGLONG end;
    if (!t) return;
    t->failed = 1;
    t->poll = NULL;
    /* All owned descendants are killed, including proxy helpers. */
    close_handle(&t->job);
    if (t->input) CancelIoEx(t->input, NULL);
    if (t->output) CancelIoEx(t->output, NULL);
    if (t->error_pipe) CancelIoEx(t->error_pipe, NULL);
    end = GetTickCount64() + (wait_allowed ? KG_CLEANUP_LIMIT : 0);
    for (;;) {
        HANDLE events[2];
        DWORD nevents = 0;
        if (t->data_pending && HasOverlappedIoCompleted(&t->data_ov)) t->data_pending = 0;
        if (t->error_pending && HasOverlappedIoCompleted(&t->error_ov)) t->error_pending = 0;
        if (!t->data_pending && !t->error_pending) break;
        if (!wait_allowed || GetTickCount64() >= end) break;
        if (t->data_pending) events[nevents++] = t->data_ov.hEvent;
        if (t->error_pending) events[nevents++] = t->error_ov.hEvent;
        WaitForMultipleObjects(nevents, events, FALSE, KG_WAIT_SLICE);
    }
    close_handle(&t->input);
    close_handle(&t->output);
    close_handle(&t->error_pipe);
    close_handle(&t->process);
    if (t->data_pending || t->error_pending) {
        /* DLL detach is forbidden to wait. Keep the completion objects
         * and their buffers alive if the kernel has not retired cancelled I/O.
         * They contain no threads or function pointers used by the kernel.
         * This exceptional bounded leak is reclaimed when Total Commander
         * exits and prevents an OVERLAPPED/buffer use-after-free on unload. */
        return;
    }
    close_handle(&t->data_ov.hEvent);
    close_handle(&t->error_ov.hEvent);
    free(t);
}

void kg_transport_close(KgTransport *t)
{
    dispose(t, 1);
}

void kg_transport_abandon(KgTransport *t)
{
    dispose(t, 0);
}
