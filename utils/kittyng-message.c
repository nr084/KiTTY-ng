#include "putty.h"

/* Translate application format strings before inserting user/server data. */
char *kr_message_printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char *result = dupvprintf(kr_event_text(format), ap);
    va_end(ap);
    return result;
}
