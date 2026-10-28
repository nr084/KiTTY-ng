/* Common editor for global commands; deliberately independent of Conf. */
#include "kittyng-quick-commands.h"
#ifdef _WIN32
#define quick_translate kr_translate
#define quick_list_add kr_listbox_addwithid_utf8
#define quick_set dlg_editbox_set_utf8
#define quick_get dlg_editbox_get_utf8
#else
#include "cli-language.h"
#define quick_translate kr_mac_translate
#define quick_list_add dlg_listbox_addwithid
#define quick_set dlg_editbox_set
#define quick_get dlg_editbox_get
#endif

struct quick_editor {
    KrQuickCommands commands;
    dlgcontrol *list, *name, *instruction, *enter, *add, *save, *remove;
    int selected;
    bool refreshing;
};
static void quick_editor_free(void *data)
{
    struct quick_editor *q = data;
    kr_quick_free(&q->commands);
}
static void quick_fields(struct quick_editor *q, dlgparam *dlg)
{
    const KrQuickCommand *c = q->selected >= 0 ?
        &q->commands.items[q->selected] : NULL;
    quick_set(q->name, dlg, c && c->name ? c->name : "");
    quick_set(q->instruction, dlg, c && c->instruction ? c->instruction : "");
    dlg_checkbox_set(q->enter, dlg, c && c->name ? c->enter : true);
}
static void quick_reload(struct quick_editor *q, dlgparam *dlg)
{
    char *error = NULL;
    q->refreshing = true;
    kr_quick_free(&q->commands);
    if (!kr_quick_load(&q->commands, quick_translate("Update Ubuntu"), &error)) {
        dlg_error_msg(dlg, quick_translate(error));
        sfree(error);
    }
    dlg_update_start(q->list, dlg);
    dlg_listbox_clear(q->list, dlg);
    int row = 0, selected_row = -1;
    for (int i = 0; i < KR_QUICK_LIMIT; i++) {
        if (!q->commands.items[i].name) continue;
        quick_list_add(q->list, dlg, q->commands.items[i].name, i);
        if (i == q->selected) selected_row = row;
        row++;
    }
    if (selected_row < 0 && row) {
        selected_row = 0;
        q->selected = dlg_listbox_getid(q->list, dlg, 0);
    } else if (!row) q->selected = -1;
    if (selected_row >= 0) dlg_listbox_select(q->list, dlg, selected_row);
    dlg_update_done(q->list, dlg);
    quick_fields(q, dlg);
    q->refreshing = false;
}
static void quick_handler(dlgcontrol *ctrl, dlgparam *dlg, void *data, int event)
{
    struct quick_editor *q = ctrl->context.p;
    if (q->refreshing) return;
    if (event == EVENT_REFRESH && ctrl == q->list) {
        quick_reload(q, dlg);
    } else if (event == EVENT_SELCHANGE && ctrl == q->list) {
        int row = dlg_listbox_index(ctrl, dlg);
        q->selected = row < 0 ? -1 : dlg_listbox_getid(ctrl, dlg, row);
        quick_fields(q, dlg);
    } else if (event == EVENT_ACTION) {
        if (ctrl == q->add) {
            int i;
            for (i = 0; i < KR_QUICK_LIMIT; i++)
                if (!q->commands.items[i].name) break;
            if (i == KR_QUICK_LIMIT) {
                dlg_error_msg(dlg, quick_translate("Up to 20 quick commands can be saved."));
                return;
            }
            q->selected = i;
            quick_fields(q, dlg);
            dlg_set_focus(q->name, dlg);
        } else if (ctrl == q->save || ctrl == q->remove) {
            if (q->selected < 0) {
                dlg_error_msg(dlg, quick_translate("Select a command or click Add."));
                return;
            }
            char *error = NULL;
            char *name = ctrl == q->save ? quick_get(q->name, dlg) : NULL;
            char *instruction = ctrl == q->save ? quick_get(q->instruction, dlg) : NULL;
            bool ok = kr_quick_update(q->selected, name, instruction,
                                     dlg_checkbox_get(q->enter, dlg), &error);
            sfree(name); sfree(instruction);
            if (ok) quick_reload(q, dlg);
            else { dlg_error_msg(dlg, quick_translate(error)); sfree(error); }
        }
    }
}
static void quick_editor_setup(struct controlbox *b)
{
    struct quick_editor *q = ctrl_alloc_with_free(b, sizeof(*q), quick_editor_free);
    memset(q, 0, sizeof(*q)); q->selected = -1;
    /* Initialize global storage once, independently of visiting this page. */
    char *error = NULL;
    kr_quick_load(&q->commands, quick_translate("Update Ubuntu"), &error);
    sfree(error);
    ctrl_settitle(b, "Window/Quick commands", "Quick commands for all sessions");
    struct controlset *s = ctrl_getset(b, "Window/Quick commands", "commands", "Quick commands");
    q->list = ctrl_listbox(s, "Saved commands (up to 20):", NO_SHORTCUT,
                          HELPCTX(no_help), quick_handler, P(q));
    q->list->listbox.height = 6;
    q->name = ctrl_editbox(s, "Command name:", NO_SHORTCUT, 100,
                          HELPCTX(no_help), quick_handler, P(q), P(NULL));
    q->instruction = ctrl_editbox(s, "Command to send:", NO_SHORTCUT, 100,
                                 HELPCTX(no_help), quick_handler, P(q), P(NULL));
    q->enter = ctrl_checkbox(s, "Press Enter after sending the command", NO_SHORTCUT,
                            HELPCTX(no_help), quick_handler, P(q));
    ctrl_columns(s, 3, 33, 34, 33);
    q->add = ctrl_pushbutton(s, "Add", NO_SHORTCUT, HELPCTX(no_help), quick_handler, P(q));
    q->add->column = 0;
    q->save = ctrl_pushbutton(s, "Save", NO_SHORTCUT, HELPCTX(no_help), quick_handler, P(q));
    q->save->column = 1;
    q->remove = ctrl_pushbutton(s, "Delete", NO_SHORTCUT, HELPCTX(no_help), quick_handler, P(q));
    q->remove->column = 2;
    ctrl_columns(s, 1, 100);
    ctrl_text(s, "Save and Delete apply immediately to all sessions. Commands are stored outside session profiles.", HELPCTX(no_help));
}
#undef quick_translate
#undef quick_set
#undef quick_get

#undef quick_list_add
