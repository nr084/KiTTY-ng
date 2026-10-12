/*
 * config-unix.c - the Unix-specific parts of the PuTTY configuration
 * box.
 */

#include <assert.h>
#include <stdlib.h>

#include "putty.h"
#include "dialog.h"
#include "storage.h"

void unix_setup_config_box(struct controlbox *b, bool midsession, int protocol)
{
    struct controlset *s;
    dlgcontrol *c;

    /*
     * The Conf structure contains two Unix-specific elements which
     * are not configured in here: stamp_utmp and login_shell. This
     * is because pterm does not put up a configuration box right at
     * the start, which is the only time when these elements would
     * be useful to configure.
     */

    /*
     * On Unix, we don't have a drop-down list for the printer
     * control.
     */
    s = ctrl_getset(b, "Terminal", "printing", "Remote-controlled printing");
    assert(s->ncontrols == 1 && s->ctrls[0]->type == CTRL_EDITBOX);
    s->ctrls[0]->editbox.has_list = false;

    /*
     * Unix supports a local-command proxy.
     */
    if (!midsession) {
        int i;
        s = ctrl_getset(b, "Connection/Proxy", "basics", NULL);
        for (i = 0; i < s->ncontrols; i++) {
            c = s->ctrls[i];
            if (c->type == CTRL_LISTBOX &&
                c->handler == proxy_type_handler) {
                c->context.i |= PROXY_UI_FLAG_LOCAL;
                break;
            }
        }
    }
#ifdef KITTY_NG_LINUX
    ctrl_settitle(b,"Connection/Automation","Commands after login");
    s=ctrl_getset(b,"Connection/Automation","startup","Startup automation");
    ctrl_editbox(s,"Automatic command",'a',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_autocommand),ED_STR);
    ctrl_filesel(s,"UTF-8 startup script",'s',FILTER_ALL_FILES,false,"Select script",HELPCTX(no_help),conf_filesel_handler,I(CONF_kr_script_file));
    ctrl_editbox(s,"Initial delay (ms)",'i',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_initdelay),ED_INT);
    ctrl_editbox(s,"Delay between characters (ms)",'b',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_bcdelay),ED_INT);
    ctrl_editbox(s,"Delay between commands (ms)",'d',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_kr_command_delay),ED_INT);
    s=ctrl_getset(b,"Connection/Automation","reconnect","Reconnection");
    ctrl_editbox(s,"Reconnect delay (seconds; 0 disables)",'r',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_kr_reconnect_delay),ED_INT);
    ctrl_settitle(b,"Connection/File transfer","SCP / SFTP transfers");
    s=ctrl_getset(b,"Connection/File transfer","destination","File transfer settings");
    ctrl_checkbox(s,"Use OSC 7 current directory",'c',HELPCTX(no_help),conf_checkbox_handler,I(CONF_scp_auto_pwd));
    ctrl_editbox(s,"Remote directory",'d',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_pscpremotedir),ED_STR);
    ctrl_editbox(s,"Alternate destination",'a',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_sftpconnect),ED_STR);
    ctrl_editbox(s,"Additional kscp options",'o',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_pscpoptions),ED_STR);
    s=ctrl_getset(b,"Connection/Proxy","profile","Shared proxy profile");
    ctrl_editbox(s,"Proxy profile from Proxies",'p',100,HELPCTX(no_help),conf_editbox_handler,I(CONF_proxyselection),ED_STR);
    s=ctrl_getset(b,"Window","extensions","KiTTY-ng window options");
    ctrl_checkbox(s,"Start maximized",'m',HELPCTX(no_help),conf_checkbox_handler,I(CONF_maximize));
    ctrl_checkbox(s,"Start fullscreen",'f',HELPCTX(no_help),conf_checkbox_handler,I(CONF_fullscreen));
    ctrl_checkbox(s,"Always on top",'t',HELPCTX(no_help),conf_checkbox_handler,I(CONF_alwaysontop));
    ctrl_checkbox(s,"Enable custom shortcuts",'s',HELPCTX(no_help),conf_checkbox_handler,I(CONF_kr_shortcuts));
    ctrl_checkbox(s,"Ctrl-click opens web links",'h',HELPCTX(no_help),conf_checkbox_handler,I(CONF_kr_hyperlinks));
    ctrl_checkbox(s,"Allow OSC 52 clipboard writes",'c',HELPCTX(no_help),conf_checkbox_handler,I(CONF_kr_remote_clipboard));
#endif
}
