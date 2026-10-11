#include "putty.h"
#include "dialog.h"
#include "kittyng.h"

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
        static const struct { int id; const char *name; } languages[] = {
            { 0, "English" },
            { 1, "\320\240\321\203\321\201\321\201\320\272\320\270\320\271" },
        };
        dlg_update_start(ctrl, dlg);
        dlg_listbox_clear(ctrl, dlg);
        for (size_t i = 0; i < lenof(languages); i++) {
            dlg_listbox_addwithid(ctrl, dlg, languages[i].name,
                                 languages[i].id);
            if (languages[i].id == language->selected)
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
