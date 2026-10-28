#include "putty.h"
#include "dialog.h"
#include "kittyng.h"
#include "kittyng-i18n.h"

typedef struct LanguageControls {
    dlgcontrol *selector;
    int selected;
} LanguageControls;

static void language_handler(dlgcontrol *ctrl, dlgparam *dlg,
                             void *data, int event)
{
    LanguageControls *language = ctrl->context.p;
    if (ctrl == language->selector && event == EVENT_REFRESH) {
        /* Keep language IDs independent of their positions in the list. */
        dlg_update_start(ctrl, dlg);
        dlg_listbox_clear(ctrl, dlg);
        for (size_t i = 0; i < kr_i18n_language_count(); i++) {
            const KrLanguageInfo *language_info = kr_i18n_language(i);
            dlg_listbox_addwithid(ctrl, dlg, language_info->native_name,
                                 language_info->id);
            if (language_info->id == language->selected)
                dlg_listbox_select(ctrl, dlg, i);
        }
        dlg_update_done(ctrl, dlg);
    } else if (ctrl == language->selector && event == EVENT_SELCHANGE) {
        int row = dlg_listbox_index(ctrl, dlg);
        if (row < 0)
            return;
        language->selected = dlg_listbox_getid(ctrl, dlg, row);
    } else if (ctrl != language->selector && event == EVENT_ACTION) {
        int previous = kr_language();
        if (!kr_set_language(language->selected))
            dlg_error_msg(dlg, "Unable to save interface language");
        else if (language->selected != previous)
            dlg_end(dlg, 2); /* Rebuild the dialog with the same Conf. */
    }
}

void kr_add_language_controls(struct controlbox *b, struct controlset *s)
{
    dlgcontrol *c;
    LanguageControls *language = ctrl_alloc(b, sizeof(*language));
    language->selected = kr_language();
    ctrl_columns(s, 2, 80, 20);
    language->selector = ctrl_droplist(s, "Interface language", NO_SHORTCUT, 50,
                                      HELPCTX(no_help), language_handler, P(language));
    language->selector->column = 0;
    c = ctrl_pushbutton(s, "Apply", NO_SHORTCUT, HELPCTX(no_help), language_handler, P(language));
    c->column = 1;
    c->align_next_to = language->selector;
    ctrl_columns(s, 1, 100);
}
