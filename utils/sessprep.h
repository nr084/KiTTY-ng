#ifndef PUTTY_UTILS_SESSPREP_H
#define PUTTY_UTILS_SESSPREP_H

#include "defs.h"

/* Extract the connection host from an HTTP(S) URL, if supplied. */
bool kr_prepare_http_host(Conf *conf);

#endif
