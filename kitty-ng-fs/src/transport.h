/* KiTTY-ng FS: local, bounded subprocess transport. */
#ifndef KITTYNGFS_TRANSPORT_H
#define KITTYNGFS_TRANSPORT_H

#include <windows.h>
#include <stddef.h>
#include <wchar.h>
#include "sftp.h"

typedef struct KgTransport KgTransport;
typedef int (*KgPoll)(void *ctx); /* nonzero cancels the current connection */

/* Uses only the explicitly named executable; never invokes a shell or PATH.
 * Poll is called on the calling thread, including while an I/O is stalled.
 * The object is single-threaded; its lifetime must outlive SfIO consumers. */
KgTransport *kg_transport_start(const wchar_t *exe, const wchar_t *root,
    const wchar_t *session, DWORD timeout_ms, KgPoll poll, void *poll_ctx,
    wchar_t *error, size_t error_cap);
SfIO kg_transport_io(KgTransport *transport);
void kg_transport_close(KgTransport *transport);
/* DLL detach variant: no waits, callbacks or worker threads. */
void kg_transport_abandon(KgTransport *transport);
const char *kg_transport_error(KgTransport *transport);
int kg_transport_cancelled(KgTransport *transport);
int kg_transport_alive(KgTransport *transport);

#endif
