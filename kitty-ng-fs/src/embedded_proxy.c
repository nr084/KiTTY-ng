#include "putty.h"
#include "network.h"
#include "proxy/proxy.h"
char *platform_setup_local_proxy(Socket *s, const char *cmd)
{ (void)s; (void)cmd; return dupstr("External proxy commands are unavailable in the embedded plugin; use a built-in SSH/HTTP/SOCKS proxy"); }
Socket *platform_new_connection(SockAddr *addr, const char *hostname, int port,
    bool privport, bool oobinline, bool nodelay, bool keepalive, Plug *plug, Conf *conf, Interactor *itr)
{
    (void)addr; (void)hostname; (void)port; (void)privport; (void)oobinline;
    (void)nodelay; (void)keepalive; (void)itr;
    if (conf_get_int(conf, CONF_proxy_type) != PROXY_CMD) return NULL;
    return new_error_socket_fmt(plug, "External proxy commands are unavailable in the embedded plugin");
}
Socket *platform_start_subprocess(const char *cmd, Plug *plug, const char *pfx, SubprocessWaiter **waiter)
{
    (void)cmd; (void)pfx; if (waiter) *waiter = NULL;
    return new_error_socket_fmt(plug, "External commands are unavailable in the embedded plugin");
}
