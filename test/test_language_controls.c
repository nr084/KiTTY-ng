/* Exercise production control callbacks and real preference storage. */
#include "putty.h"
#include "dialog.h"
#include "kittyng.h"
#include "kittyng-portable.h"
#ifdef NDEBUG
#error Language regression checks require assertions
#endif

const char *const appname = "language-test";
void modalfatalbox(const char *format, ...) { abort(); }
static int ids[8], count, selected, ended, errors;
void dlg_update_start(dlgcontrol *c, dlgparam *d) {}
void dlg_update_done(dlgcontrol *c, dlgparam *d) {}
void dlg_listbox_clear(dlgcontrol *c, dlgparam *d) { count = 0; selected = -1; }
void dlg_listbox_addwithid(dlgcontrol *c, dlgparam *d, const char *s, int id) {
    assert(count < lenof(ids)); ids[count++] = id;
}
void dlg_listbox_select(dlgcontrol *c, dlgparam *d, int row) { selected = row; }
int dlg_listbox_index(dlgcontrol *c, dlgparam *d) { return selected; }
int dlg_listbox_getid(dlgcontrol *c, dlgparam *d, int row) { assert(row >= 0 && row < count); return ids[row]; }
void dlg_end(dlgparam *d, int result) { ended = result; }
void dlg_error_msg(dlgparam *d, const char *message) { errors++; }

int main(int argc, char **argv)
{
    HKEY isolated = NULL;
    if (argc >= 4 && !strcmp(argv[1], "--registry")) {
        const char *prefix = "Software\\kitty-ng-language-test-";
        assert(!strncmp(argv[2], prefix, strlen(prefix)));
        assert(!kr_portable());
        assert(RegOpenKeyExA(HKEY_CURRENT_USER, argv[2], 0, KEY_ALL_ACCESS, &isolated) == ERROR_SUCCESS);
        assert(RegOverridePredefKey(HKEY_CURRENT_USER, isolated) == ERROR_SUCCESS);
    }
    const char *step = argv[argc - 1];
    int initial = kr_language();
    struct controlbox *box = ctrl_new_box();
    struct controlset *set = ctrl_getset(box, "Window/Appearance", "language", "Language");
    kr_add_language_controls(box, set);
    dlgcontrol *selector = NULL, *apply = NULL;
    for (int i = 0; i < set->ncontrols; i++) {
        if (set->ctrls[i]->type == CTRL_LISTBOX) selector = set->ctrls[i];
        if (set->ctrls[i]->type == CTRL_BUTTON) apply = set->ctrls[i];
    }
    assert(selector && apply && !strcmp(apply->label, "Apply"));
    selector->handler(selector, NULL, NULL, EVENT_REFRESH);
    assert(count == 2 && ids[selected] == initial);
    if (!strncmp(step, "read", 4)) {
        assert(initial == step[4] - '0');
    } else {
        int wanted = step[strlen(step) - 1] - '0';
        selected = wanted;
        selector->handler(selector, NULL, NULL, EVENT_SELCHANGE);
        assert(kr_language() == initial && !ended && !errors);
        /* Re-entering the panel must retain the un-applied selection. */
        selector->handler(selector, NULL, NULL, EVENT_REFRESH);
        assert(ids[selected] == wanted);
        if (!strncmp(step, "set", 3)) {
            apply->handler(apply, NULL, NULL, EVENT_ACTION);
            assert(kr_language() == wanted && !errors);
            assert(ended == (wanted == initial ? 0 : 2));
        }
    }
    ctrl_free_box(box);
    if (isolated) {
        assert(RegOverridePredefKey(HKEY_CURRENT_USER, NULL) == ERROR_SUCCESS);
        RegCloseKey(isolated);
    }
    printf("PASS: language %s\n", step);
    return 0;
}
