/* Embedded PuTTY SSH channel, synchronously pumped by serialized WFX calls.
 * No process launch, console handles, process environment or shell command. */
#include "putty.h"
#include "storage.h"
#include "transport.h"
#include "pathutil.h"
#include <stdint.h>
#include <stdio.h>

int kg_engine_set_root(const wchar_t *root);
void kg_engine_select_cleanup(void);
void kg_engine_storage_cleanup(void);
static KgPrompt request_prompt;
static void *request_ctx;
static KgTransport *active_transport;
void kg_transport_set_prompt(KgPrompt fn, void *ctx) { request_prompt = fn; request_ctx = ctx; }
static unsigned engine_users;
static wchar_t *engine_root;
struct KgTransport {
    Seat seat;
    Backend *backend;
    Conf *conf;
    LogContext *log;
    LogPolicy policy;
    bufchain received;
    HANDLE tick;
    KgPoll poll;
    void *poll_ctx;
    DWORD timeout_ms;
    ULONGLONG deadline;
    int failed, cancelled, password_tried, registered;
    size_t wanted;
    int wait_mode;
    char reason[4096];
};
static void fail(KgTransport *t, const char *message)
{
    if (!t->failed) snprintf(t->reason, sizeof(t->reason), "%s", message);
    t->failed = 1;
}
static KgTransport *from_seat(Seat *s) { return container_of(s, KgTransport, seat); }
static size_t output(Seat *seat, SeatOutputType type, const void *data, size_t len)
{
    KgTransport *t = from_seat(seat);
    if (type == SEAT_OUTPUT_STDOUT && !t->failed) {
        if (len > 2u * SF_MAX_PACKET - bufchain_size(&t->received))
            fail(t, "SSH receive buffer limit exceeded");
        else bufchain_add(&t->received, data, len);
    }
    return bufchain_size(&t->received);
}
static void fatal(Seat *s, const char *message) { fail(from_seat(s), message); }
static void remote_exit(Seat *s) { fail(from_seat(s), "SSH session closed by server"); }
static SeatPromptResult prompts(Seat *s, prompts_t *p)
{
    KgTransport *t = from_seat(s);
    const char *password = conf_get_str(t->conf, CONF_password);
    if (!p->n_prompts) return SPR_OK;
    if (!t->password_tried && p->to_server && p->n_prompts == 1 &&
        !p->prompts[0]->echo && password[0]) {
        prompt_set_result(p->prompts[0], password);
        t->password_tried = 1;
        smemclr((char *)password, strlen(password));
        conf_set_str(t->conf, CONF_password, "");
        return SPR_OK;
    }
    if (!request_prompt || p->n_prompts > 16)
        return SPR_SW_ABORT("Interactive authentication is unavailable");
    for (size_t i = 0; i < p->n_prompts; i++) {
        wchar_t answer[4096] = {0};
        char *label = dupprintf("%s\n%s\n%s", p->name ? p->name : "",
                               p->instruction ? p->instruction : "", p->prompts[i]->prompt);
        wchar_t *wide = dup_mb_to_wc(p->utf8 ? CP_UTF8 : CP_ACP, label);
        sfree(label);
        int ok = request_prompt(request_ctx, p->prompts[i]->echo ? 0 : 2,
            p->from_server ? L"KiTTY-ng FS — SSH server authentication" : L"KiTTY-ng FS — local authentication",
            wide, answer, sizeof(answer) / sizeof(*answer));
        sfree(wide);
        if (!ok) { smemclr(answer, sizeof(answer)); t->cancelled = 1; return SPR_USER_ABORT; }
        char *value = dup_wc_to_mb(p->utf8 ? CP_UTF8 : CP_ACP, answer, "");
        prompt_set_result(p->prompts[i], value);
        smemclr(answer, sizeof(answer)); smemclr(value, strlen(value)); sfree(value);
    }
    t->deadline = GetTickCount64() + t->timeout_ms;
    return SPR_OK;
}
static SeatPromptResult host_key(Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *, SeatPromptResult), void *ctx)
{
    KgTransport *t = from_seat(seat);
    (void)helpctx; (void)callback; (void)ctx;
    strbuf *message = strbuf_new();
    int changed = check_stored_host_key(host, port, keytype, keystr) == 2;
    put_fmt(message, "%s\n%s:%d (%s)\n\n",
        changed ? "WARNING: SERVER KEY HAS CHANGED" : "Unknown SSH server key", host, port, keytype);
    for (size_t i = 0; i < text->nitems; i++) {
        SeatDialogTextItem *item = &text->items[i];
        if (item->type == SDT_DISPLAY || item->type == SDT_SCARY_HEADING)
            put_fmt(message, "%s\n", item->text);
    }
    put_dataz(message, "\nCompare the fingerprint with the server administrator.\nSave this key and connect?");
    wchar_t *wide = dup_mb_to_wc(CP_UTF8, message->s), ignored[2] = {0};
    strbuf_free(message);
    int ok = request_prompt && request_prompt(request_ctx, 9,
        L"KiTTY-ng FS — verify SSH host key", wide, ignored, sizeof(ignored) / sizeof(*ignored));
    sfree(wide);
    t->deadline = GetTickCount64() + t->timeout_ms;
    if (!ok) return SPR_SW_ABORT("SSH server key was not accepted");
    store_host_key(seat, host, port, keytype, keystr);
    return t->failed ? SPR_SW_ABORT("Cannot save SSH host key") : SPR_OK;
}

static const SeatVtable seat_vt = {
    .output = output, .eof = nullseat_eof, .sent = nullseat_sent,
    .banner = nullseat_banner, .get_userpass_input = prompts,
    .notify_session_started = nullseat_notify_session_started,
    .notify_remote_exit = remote_exit, .notify_remote_disconnect = remote_exit,
    .connection_fatal = fatal, .nonfatal = fatal,
    .update_specials_menu = nullseat_update_specials_menu,
    .get_ttymode = nullseat_get_ttymode, .set_busy_status = nullseat_set_busy_status,
    .confirm_ssh_host_key = host_key,
    .confirm_weak_crypto_primitive = nullseat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = nullseat_confirm_weak_cached_hostkey,
    .prompt_descriptions = nullseat_prompt_descriptions,
    .is_utf8 = nullseat_is_always_utf8, .echoedit_update = nullseat_echoedit_update,
    .get_display = nullseat_get_display, .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = nullseat_get_window_pixel_size,
    .stripctrl_new = nullseat_stripctrl_new, .set_trust_status = nullseat_set_trust_status,
    .can_set_trust_status = nullseat_can_set_trust_status_no,
    .has_mixed_input_stream = nullseat_has_mixed_input_stream_no,
    .verbose = nullseat_verbose_no, .interactive = nullseat_interactive_yes,
    .get_cursor_position = nullseat_get_cursor_position,
};
static void log_event(LogPolicy *lp, const char *message) { (void)lp; (void)message; }
static int log_append(LogPolicy *lp, Filename *f, void (*cb)(void *, int), void *ctx)
{ (void)lp; (void)f; (void)cb; (void)ctx; return 0; }
static const LogPolicyVtable log_vt = {
    .eventlog = log_event, .askappend = log_append,
    .logging_error = log_event, .verbose = null_lp_verbose_no,
};
int kg_engine_poll(void)
{
    KgTransport *t = active_transport;
    if (!t || t->failed) return 0;
    if (t->poll && t->poll(t->poll_ctx)) { t->cancelled = 1; fail(t, "Operation cancelled"); }
    if (GetTickCount64() >= t->deadline) fail(t, "SSH operation timed out");
    return !t->failed;
}
static bool pre(void *ctx, const HANDLE **extra, size_t *count)
{
    KgTransport *t = ctx;
    if (t->failed) return false;
    if (t->poll && t->poll(t->poll_ctx)) {
        t->cancelled = 1; fail(t, "Operation cancelled"); return false;
    }
    if (GetTickCount64() >= t->deadline) { fail(t, "SSH operation timed out"); return false; }
    if ((t->wait_mode == 0 && backend_sendok(t->backend)) ||
        (t->wait_mode == 1 && bufchain_size(&t->received) >= t->wanted) ||
        (t->wait_mode == 2 && backend_sendbuffer(t->backend) == 0)) return false;
    if (backend_exitcode(t->backend) >= 0) { fail(t, "SSH channel closed"); return false; }
    *extra = &t->tick; *count = 1;
    return true;
}
static bool post(void *ctx, size_t index) { (void)index; return !((KgTransport *)ctx)->failed; }
static int pump(KgTransport *t, int mode, size_t wanted)
{
    t->wait_mode = mode; t->wanted = wanted;
    t->deadline = GetTickCount64() + t->timeout_ms;
    active_transport = t;
    cli_main_loop(pre, post, t);
    active_transport = NULL;
    return !t->failed;
}
static int read_exact(void *ctx, void *buffer, size_t size)
{
    KgTransport *t = ctx;
    unsigned char *out = buffer;
    if (size > SF_MAX_PACKET) return 0;
    while (size) {
        if (!pump(t, 1, 1)) return 0;
        size_t n = bufchain_size(&t->received);
        if (n > size) n = size;
        bufchain_fetch_consume(&t->received, out, n);
        backend_unthrottle(t->backend, bufchain_size(&t->received));
        out += n; size -= n;
    }
    return 1;
}
static int write_all(void *ctx, const void *buffer, size_t size)
{
    KgTransport *t = ctx;
    if (t->failed || size > SF_MAX_PACKET) return 0;
    backend_send(t->backend, buffer, size);
    return pump(t, 2, 0);
}
KgTransport *kg_transport_start(const wchar_t *root, const wchar_t *session,
    DWORD timeout_ms, KgPoll poll, void *poll_ctx, wchar_t *error, size_t error_cap)
{
    KgTransport *t = calloc(1, sizeof(*t));
    char *name = kg_utf8(session), *realhost = NULL, *problem;
    LARGE_INTEGER due;
    if (!t || !name) { free(t); free(name); return NULL; }
    t->seat.vt = &seat_vt; t->policy.vt = &log_vt;
    t->poll = poll; t->poll_ctx = poll_ctx; t->timeout_ms = timeout_ms;
    bufchain_init(&t->received);
    if (engine_users && _wcsicmp(engine_root, root)) {
        fail(t, "Close all connections before changing the profile directory"); goto failed;
    }
    if (!engine_users) {
        if (!kg_engine_set_root(root)) { fail(t, "Profile storage is busy"); goto failed; }
        engine_root = kg_wdup(root);
        sk_init(); winselcli_setup();
        settings_set_default_protocol(PROT_SSH); settings_set_default_port(22);
    }
    engine_users++; t->registered = 1;
    t->conf = conf_new();
    do_defaults(name, t->conf);
    if (conf_get_int(t->conf, CONF_protocol) != PROT_SSH) {
        fail(t, "File transfer requires an SSH profile"); goto failed;
    }
    if (conf_get_int(t->conf, CONF_proxy_type) == PROXY_CMD) {
        fail(t, "External proxy commands are unavailable; use a built-in SSH, HTTP or SOCKS proxy");
        goto failed;
    }
    conf_set_int(t->conf, CONF_logtype, LGTYP_NONE);
    conf_set_int(t->conf, CONF_sshprot, 3); /* SSH-2 only */
    conf_set_bool(t->conf, CONF_nopty, true);
    conf_set_bool(t->conf, CONF_ssh_subsys, true);
    conf_set_bool(t->conf, CONF_ssh_simple, true);
    conf_set_bool(t->conf, CONF_x11_forward, false);
    conf_set_bool(t->conf, CONF_agentfwd, false);
    conf_set_bool(t->conf, CONF_ssh_connection_sharing, false);
    conf_set_str(t->conf, CONF_remote_cmd, "sftp");
    conf_set_str(t->conf, CONF_remote_cmd2, "");
    {
        char *key;
        while ((key = conf_get_str_nthstrkey(t->conf, CONF_portfwd, 0)) != NULL)
            conf_del_str_str(t->conf, CONF_portfwd, key);
    }
    t->tick = CreateWaitableTimerW(NULL, FALSE, NULL);
    due.QuadPart = -100000;
    if (!t->tick || !SetWaitableTimer(t->tick, &due, 50, NULL, NULL, FALSE)) {
        fail(t, "Cannot create SSH event timer"); goto failed;
    }
    t->log = log_init(&t->policy, t->conf);
    problem = backend_init(&ssh_backend, &t->seat, &t->backend, t->log, t->conf,
        conf_get_str(t->conf, CONF_host), conf_get_int(t->conf, CONF_port),
        &realhost, true, conf_get_bool(t->conf, CONF_tcp_keepalives));
    sfree(realhost);
    if (problem) { fail(t, problem); sfree(problem); goto failed; }
    if (!pump(t, 0, 0)) goto failed;
    free(name);
    return t;
failed:
    if (error && error_cap) {
        wchar_t *wide = kg_wide(t->reason);
        wcsncpy(error, wide ? wide : L"SSH connection failed", error_cap - 1);
        error[error_cap - 1] = 0; free(wide);
    }
    free(name); kg_transport_close(t); return NULL;
}
SfIO kg_transport_io(KgTransport *t) { SfIO io = {t, read_exact, write_all}; return io; }
const char *kg_transport_error(KgTransport *t) { return t ? t->reason : "SSH connection unavailable"; }
int kg_transport_cancelled(KgTransport *t) { return t && t->cancelled; }
int kg_transport_alive(KgTransport *t)
{ return t && !t->failed && t->backend && backend_exitcode(t->backend) < 0; }
void kg_transport_close(KgTransport *t)
{
    if (!t) return;
    t->poll = NULL;
    if (t->backend) backend_free(t->backend);
    if (t->log) log_free(t->log);
    if (t->conf) {
        char *password = conf_get_str(t->conf, CONF_password);
        smemclr(password, strlen(password));
        password = conf_get_str(t->conf, CONF_proxy_password);
        smemclr(password, strlen(password));
        conf_free(t->conf);
    }
    if (t->registered) {
        if (--engine_users == 0) {
            sk_cleanup(); kg_engine_select_cleanup(); kg_engine_storage_cleanup(); free(engine_root); engine_root = NULL;
        }
    }
    if (t->tick) CloseHandle(t->tick);
    bufchain_clear(&t->received);
    SecureZeroMemory(t, sizeof(*t)); free(t);
}
void kg_transport_abandon(KgTransport *t) { kg_transport_close(t); }
