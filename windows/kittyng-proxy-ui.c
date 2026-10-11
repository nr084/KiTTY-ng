#include "putty.h"
#include "dialog.h"
#include "kittyng.h"
#include "kittyng-extensions.h"

typedef struct ProxySelector { char **names; int count; Conf *inline_proxy; } ProxySelector;
static void proxy_free(void *context)
{
    ProxySelector *list = context;
    for (int i = 0; i < list->count; i++) sfree(list->names[i]);
    sfree(list->names); if (list->inline_proxy) conf_free(list->inline_proxy); sfree(list);
}

static void add_name(ProxySelector *list, const char *name)
{
    list->names = sresize(list->names, list->count+1, char *);
    list->names[list->count++] = dupstr(name);
}

static void proxy_handler(dlgcontrol *ctrl, dlgparam *dlg, void *data, int event)
{
    ProxySelector *list = ctrl->context.p;
    Conf *conf = data;
    if (event == EVENT_REFRESH) {
        if (!list->inline_proxy) { list->inline_proxy = conf_new(); kr_copy_proxy(list->inline_proxy, conf); }
        for (int i = 0; i < list->count; i++) sfree(list->names[i]);
        list->count = 0;
        add_name(list, "- Session defined proxy -"); add_name(list, "- No proxy -");
        HKEY key = open_regkey_ro(HKEY_CURRENT_USER, PUTTY_REG_POS "\\Proxies");
        for (int i = 0; key && list->count < 1024; i++) {
            char *encoded = enum_regkey(key, i); if (!encoded) break;
            strbuf *name = strbuf_new(); unescape_registry_key(encoded, name); sfree(encoded);
            if (strcmp(name->s, "None") && strcmp(name->s, "Default")) add_name(list, name->s);
            strbuf_free(name);
        }
        if (key) close_regkey(key);
        const char *selected = conf_get_str(conf, CONF_proxyselection);
        int selection = -1;
        for (int i = 0; i < list->count; i++) if (!strcmp(list->names[i], selected)) selection = i;
        if (selection < 0) { selection = list->count; add_name(list, selected); }
        dlg_update_start(ctrl, dlg); dlg_listbox_clear(ctrl, dlg);
        for (int i = 0; i < list->count; i++) {
            if (i < 2) kr_listbox_add_label(ctrl, dlg, list->names[i]);
            else kr_listbox_addwithid_raw(ctrl, dlg, list->names[i], i);
        }
        dlg_listbox_select(ctrl, dlg, selection); dlg_update_done(ctrl, dlg);
    } else if (event == EVENT_SELCHANGE || event == EVENT_VALCHANGE) {
        int selection = dlg_listbox_index(ctrl, dlg);
        if (selection >= 0 && selection < list->count) {
            if (!strcmp(conf_get_str(conf, CONF_proxyselection), "- Session defined proxy -")) kr_copy_proxy(list->inline_proxy, conf);
            conf_set_str(conf, CONF_proxyselection, list->names[selection]);
            if (!selection) kr_copy_proxy(conf, list->inline_proxy);
            kr_load_proxy(conf);
            const char *error = conf_get_str(conf, CONF_kr_proxy_error);
            if (*error) dlg_error_msg(dlg, error);
        }
    }
}

void kr_add_proxy_selector(struct controlbox *box, struct controlset *set)
{
    ProxySelector *list = ctrl_alloc_with_free(box, sizeof(*list), proxy_free);
    list->names = NULL; list->count = 0; list->inline_proxy = NULL;
    ctrl_droplist(set, "Proxy profile", NO_SHORTCUT, 100, HELPCTX(no_help), proxy_handler, P(list));
}
