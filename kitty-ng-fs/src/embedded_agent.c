/* PuTTY Pageant protocol, adapted for a DLL: no worker thread can outlive
 * the plugin. Named-pipe operations are bounded, cancellable and owner checked.
 * The WM_COPYDATA fallback below is derived from windows/agent-client.c (MIT). */
#include "putty.h"
#include "pageant.h"
#include "security-api.h"
#include "cryptoapi.h"
#include "transport.h"
static bool wm_copydata_agent_exists(void)
{
    HWND hwnd;
    hwnd = FindWindowA("Pageant", "Pageant");
    if (!hwnd)
        return false;
    else
        return true;
}

static void wm_copydata_agent_query(strbuf *query, void **out, int *outlen)
{
    HWND hwnd;
    char *mapname;
    HANDLE filemap;
    unsigned char *p, *ret;
    int id, retlen;
    COPYDATASTRUCT cds;
    SECURITY_ATTRIBUTES sa, *psa;
    PSECURITY_DESCRIPTOR psd = NULL;
    PSID usersid = NULL;

    *out = NULL;
    *outlen = 0;

    if (query->len > AGENT_MAX_MSGLEN)
        return;                        /* query too large */

    hwnd = FindWindowA("Pageant", "Pageant");
    if (!hwnd)
        return;                        /* *out == NULL, so failure */
    mapname = dupprintf("PageantRequest%08x", (unsigned)GetCurrentThreadId());

    psa = NULL;
    if (got_advapi()) {
        /*
         * Make the file mapping we create for communication with
         * Pageant owned by the user SID rather than the default. This
         * should make communication between processes with slightly
         * different contexts more reliable: in particular, command
         * prompts launched as administrator should still be able to
         * run PSFTPs which refer back to the owning user's
         * unprivileged Pageant.
         */
        usersid = get_user_sid();

        if (usersid) {
            psd = (PSECURITY_DESCRIPTOR)
                LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
            if (psd) {
                if (p_InitializeSecurityDescriptor(
                        psd, SECURITY_DESCRIPTOR_REVISION) &&
                    p_SetSecurityDescriptorOwner(psd, usersid, false)) {
                    sa.nLength = sizeof(sa);
                    sa.bInheritHandle = true;
                    sa.lpSecurityDescriptor = psd;
                    psa = &sa;
                } else {
                    LocalFree(psd);
                    psd = NULL;
                }
            }
        }
    }

    filemap = CreateFileMappingA(INVALID_HANDLE_VALUE, psa, PAGE_READWRITE,
                                0, AGENT_MAX_MSGLEN, mapname);
    if (filemap == NULL || filemap == INVALID_HANDLE_VALUE) {
        sfree(mapname);
        if (psd) LocalFree(psd);
        return;                        /* *out == NULL, so failure */
    }
    p = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, 0);

    if (!p) { CloseHandle(filemap); sfree(mapname); if (psd) LocalFree(psd); return; }
    memcpy(p, query->s, query->len);
    cds.dwData = AGENT_COPYDATA_ID;
    cds.cbData = 1 + strlen(mapname);
    cds.lpData = mapname;

    /*
     * The user either passed a null callback (indicating that the
     * query is required to be synchronous) or CreateThread failed.
     * Either way, we need a synchronous request.
     */
    DWORD_PTR reply = 0;
    id = SendMessageTimeoutA(hwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &reply) ? (int)reply : 0;
    if (id > 0) {
        uint32_t length_field = GET_32BIT_MSB_FIRST(p);
        if (length_field > 0 && length_field <= AGENT_MAX_MSGLEN - 4) {
            retlen = length_field + 4;
            ret = snewn(retlen, unsigned char);
            memcpy(ret, p, retlen);
            *out = ret;
            *outlen = retlen;
        } else {
            /*
             * If we get here, we received an out-of-range length
             * field, either without space for a message type code or
             * overflowing the FileMapping.
             *
             * Treat this as if Pageant didn't answer at all - which
             * actually means we do nothing, and just don't fill in
             * out and outlen.
             */
        }
    }
    UnmapViewOfFile(p);
    CloseHandle(filemap);
    sfree(mapname);
    if (psd)
        LocalFree(psd);
}

static HANDLE open_agent(void)
{
    char *name = agent_named_pipe_name();
    HANDLE pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                             OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    sfree(name);
    if (pipe == INVALID_HANDLE_VALUE) return pipe;
    PSID user = get_user_sid(), owner;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!user || !got_advapi() || p_GetSecurityInfo(pipe, SE_KERNEL_OBJECT,
        OWNER_SECURITY_INFORMATION, &owner, NULL, NULL, NULL, &descriptor) != ERROR_SUCCESS) {
        CloseHandle(pipe); return INVALID_HANDLE_VALUE;
    }
    bool valid = EqualSid(owner, user);
    LocalFree(descriptor);
    if (!valid) { CloseHandle(pipe); return INVALID_HANDLE_VALUE; }
    return pipe;
}
static bool transfer(HANDLE pipe, void *buffer, DWORD length, bool writing)
{
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return false;
    bool ok = true;
    unsigned char *p = buffer;
    while (length && ok) {
        DWORD count = 0;
        ResetEvent(ov.hEvent);
        BOOL immediate = writing ? WriteFile(pipe, p, length, &count, &ov) :
                                   ReadFile(pipe, p, length, &count, &ov);
        if (!immediate && GetLastError() != ERROR_IO_PENDING) { ok = false; break; }
        if (!immediate) {
            while (WaitForSingleObject(ov.hEvent, 50) == WAIT_TIMEOUT) {
                if (!kg_engine_poll()) { CancelIoEx(pipe, &ov); ok = false; break; }
            }
            /* Retire the cancelled operation before releasing its buffers. */
            if (!GetOverlappedResult(pipe, &ov, &count, TRUE)) ok = false;
        }
        if (!count || count > length) ok = false;
        if (ok) { p += count; length -= count; }
    }
    CloseHandle(ov.hEvent); return ok;
}
bool agent_exists(void)
{
    HANDLE pipe = open_agent();
    if (pipe != INVALID_HANDLE_VALUE) { CloseHandle(pipe); return true; }
    return wm_copydata_agent_exists();
}
agent_pending_query *agent_query(strbuf *query, void **out, int *outlen,
    void (*callback)(void *, void *, int), void *callback_ctx)
{
    (void)callback; (void)callback_ctx;
    *out = NULL; *outlen = 0;
    strbuf_finalise_agent_query(query);
    if (query->len > AGENT_MAX_MSGLEN) return NULL;
    HANDLE pipe = open_agent();
    if (pipe != INVALID_HANDLE_VALUE) {
        unsigned char sizebuf[4];
        if (transfer(pipe, query->s, (DWORD)query->len, true) && transfer(pipe, sizebuf, 4, false)) {
            uint32_t n = GET_32BIT_MSB_FIRST(sizebuf);
            if (n && n <= AGENT_MAX_MSGLEN - 4) {
                unsigned char *answer = snewn(n + 4, unsigned char);
                memcpy(answer, sizebuf, 4);
                if (transfer(pipe, answer + 4, n, false)) { *out = answer; *outlen = (int)n + 4; }
                else { smemclr(answer, n + 4); sfree(answer); }
            }
        }
        CloseHandle(pipe);
        return NULL;
    }
    if (kg_engine_poll()) wm_copydata_agent_query(query, out, outlen);
    return NULL;
}
void agent_cancel_query(agent_pending_query *pq) { (void)pq; }
Socket *agent_connect(Plug *plug)
{ return new_error_socket_fmt(plug, "Agent forwarding is disabled for file transfers"); }
