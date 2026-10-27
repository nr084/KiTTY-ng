/* Library frontend hooks: no console, process exit, or GUI timer window. */
#include "putty.h"
const bool share_can_be_downstream = false;
const bool share_can_be_upstream = false;
void timer_change_notify(unsigned long next) { (void)next; }
void old_keyfile_warning(void) { }
void nonfatal(const char *fmt, ...) { (void)fmt; }
void modalfatalbox(const char *fmt, ...)
{
    /* Only unrecoverable core failures (e.g. allocator exhaustion) get here.
     * Ordinary network/authentication errors go through the per-session Seat. */
    (void)fmt;
    RaiseException(STATUS_NO_MEMORY, EXCEPTION_NONCONTINUABLE, 0, NULL);
    abort();
}
