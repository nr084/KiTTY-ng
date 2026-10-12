/* GTK actions reuse the portable KiTTY shortcut syntax and command parser. */
static void upload_menu(GtkMenuItem *, gpointer);
static void download_menu(GtkMenuItem *, gpointer);
void copy_all_menuitem(GtkMenuItem *, gpointer);
void clear_scrollback_menuitem(GtkMenuItem *, gpointer);
void reset_terminal_menuitem(GtkMenuItem *, gpointer);
void dup_session_menuitem(GtkMenuItem *, gpointer);
void new_session_menuitem(GtkMenuItem *, gpointer);
void change_settings_menuitem(GtkMenuItem *, gpointer);
void event_log_menuitem(GtkMenuItem *, gpointer);
static void lock_menu(GtkMenuItem *item, gpointer data) {
    GtkFrontend *inst = data;
    inst->input_locked = !inst->input_locked;
}
static void gtk_seat_session_started(Seat *seat);
static void top_menu(GtkMenuItem *item, gpointer data) {
    GtkFrontend *inst = data;
    bool top = !conf_get_bool(inst->conf, CONF_alwaysontop);
    conf_set_bool(inst->conf, CONF_alwaysontop, top);
    gtk_window_set_keep_above(GTK_WINDOW(inst->window), top);
}
static void minimize_menu(GtkMenuItem *item, gpointer data) {
    GtkFrontend *inst = data;
    gtk_window_iconify(GTK_WINDOW(inst->window));
}
static void fullscreen_menu(GtkMenuItem *item, gpointer data) {
    GtkFrontend *inst = data;
    if (gdk_window_get_state(gtk_widget_get_window(inst->window)) &
        GDK_WINDOW_STATE_FULLSCREEN)
        gtk_window_unfullscreen(GTK_WINDOW(inst->window));
    else
        gtk_window_fullscreen(GTK_WINDOW(inst->window));
}
static void startup_menu(GtkMenuItem *item, gpointer data) {
    GtkFrontend *inst = data;
    if (inst->backend && backend_sendok(inst->backend))
        gtk_seat_session_started(&inst->seat);
}
static void command_menu(GtkMenuItem *item, gpointer data) {
    GtkFrontend *inst = data;
    if (inst->input_locked || !inst->backend || !backend_sendok(inst->backend))
        return;
    GtkWidget *d = gtk_dialog_new_with_buttons(
        kr_mac_translate("Run command…"), GTK_WINDOW(inst->window),
        GTK_DIALOG_MODAL, kr_mac_translate("Cancel"), GTK_RESPONSE_CANCEL,
        kr_mac_translate("Send"), GTK_RESPONSE_OK, NULL);
    GtkWidget *e = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))),
                      e);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char *text =
            kr_mac_command_text(gtk_entry_get_text(GTK_ENTRY(e)), true, true);
        term_keyinput(inst->term, CS_UTF8, text, strlen(text));
        smemclr(text, strlen(text));
        sfree(text);
    }
    gtk_widget_destroy(d);
}
static unsigned linux_key(GdkEventKey *event) {
    guint key = gdk_keyval_to_upper(event->keyval);
    unsigned mods = 0;
    if (event->state & GDK_MOD1_MASK)
        mods |= 1;
    if (event->state & GDK_CONTROL_MASK)
        mods |= 2;
    if (event->state & GDK_SHIFT_MASK)
        mods |= 4;
    if (event->state & GDK_SUPER_MASK)
        mods |= 8;
    if (event->state & GDK_MOD5_MASK)
        mods = (mods & ~3U) | 16;
    if (key >= GDK_KEY_F1 && key <= GDK_KEY_F24)
        key = 112 + key - GDK_KEY_F1;
    else {
        switch (key) {
        case GDK_KEY_Return:
            key = 13;
            break;
        case GDK_KEY_Escape:
            key = 27;
            break;
        case GDK_KEY_BackSpace:
            key = 8;
            break;
        case GDK_KEY_Tab:
            key = 9;
            break;
        case GDK_KEY_Insert:
            key = 45;
            break;
        case GDK_KEY_Delete:
            key = 46;
            break;
        case GDK_KEY_Home:
            key = 36;
            break;
        case GDK_KEY_End:
            key = 35;
            break;
        case GDK_KEY_Up:
            key = 38;
            break;
        case GDK_KEY_Down:
            key = 40;
            break;
        case GDK_KEY_Left:
            key = 37;
            break;
        case GDK_KEY_Right:
            key = 39;
            break;
        case GDK_KEY_Page_Up:
            key = 33;
            break;
        case GDK_KEY_Page_Down:
            key = 34;
            break;
        case GDK_KEY_plus:
        case GDK_KEY_equal:
            key = 187;
            break;
        case GDK_KEY_minus:
            key = 189;
            break;
        case GDK_KEY_comma:
            key = 188;
            break;
        case GDK_KEY_period:
            key = 190;
            break;
        default:
            if (key > 255)
                return 0;
        }
    }
    return key | (mods << 16);
}
static bool kr_linux_shortcut(GtkFrontend *inst, GdkEventKey *event) {
    if (event->type != GDK_KEY_PRESS ||
        !conf_get_bool(inst->conf, CONF_kr_shortcuts))
        return false;
    unsigned code = linux_key(event);
    static const struct {
        const char *name, *fallback;
        void (*action)(GtkMenuItem *, gpointer);
    } actions[] = {{"script", "{CONTROL}{F2}", command_menu},
                   {"sendfile", "{CONTROL}{F3}", upload_menu},
                   {"getfile", "{CONTROL}{F4}", download_menu},
                   {"protect", "{CONTROL}{F9}", lock_menu},
                   {"tray", "{CONTROL}{F6}", minimize_menu},
                   {"visible", "{CONTROL}{F7}", top_menu},
                   {"autocommand", "{SHIFT}{F12}", startup_menu},
                   {"fullscreen", "", fullscreen_menu},
                   {"eventlog", "", event_log_menuitem},
                   {"copyall", "", copy_all_menuitem},
                   {"duplicate", "", dup_session_menuitem},
                   {"opennew", "", new_session_menuitem},
                   {"changesettings", "", change_settings_menuitem},
                   {"clearscrollback", "", clear_scrollback_menuitem},
                   {"resetterminal", "", reset_terminal_menuitem}};
    for (size_t i = 0; i < lenof(actions); i++) {
        char *value =
            kr_mac_ini("Shortcuts", actions[i].name, actions[i].fallback);
        unsigned binding = kr_mac_parse_shortcut(value);
        sfree(value);
        if (binding && binding == code) {
            if (!inst->input_locked || actions[i].action == lock_menu)
                actions[i].action(NULL, inst);
            return true;
        }
    }
    char *list = kr_mac_ini("Shortcuts", "list", ""), *cursor = list, *name;
    bool used = false;
    int count = 0;
    while ((name = strsep(&cursor, " ;\t\r\n")) && count++ < 512) {
        if (!*name || kr_mac_parse_shortcut(name) != code)
            continue;
        char *value = kr_mac_ini("Shortcuts", name, "");
        if (*value) {
            used = true;
            if (!inst->input_locked) {
                char *text = kr_mac_command_text(value, true, false);
                term_keyinput(inst->term, CS_UTF8, text, strlen(text));
                smemclr(text, strlen(text));
                sfree(text);
            }
        }
        smemclr(value, strlen(value));
        sfree(value);
        break;
    }
    sfree(list);
    return used;
}
static bool kr_linux_link(GtkFrontend *inst, GdkEventButton *event) {
    if (!conf_get_bool(inst->conf, CONF_kr_hyperlinks))
        return false;
    int col = (event->x - inst->window_border) / inst->font_width,
        row = (event->y - inst->window_border) / inst->font_height;
    if (row < 0 || row >= inst->term->rows || col < 0 ||
        col >= inst->term->cols)
        return false;
    termline *line = inst->term->disptext[row];
    GString *text = g_string_new(NULL);
    size_t target = 0;
    for (int i = 0; i < inst->term->cols; i++) {
        unsigned c = line->chars[i].chr;
        if (i == col)
            target = text->len;
        if (c == UCSWIDE)
            continue;
        switch (c & CSET_MASK) {
        case CSET_ASCII:
            c = inst->ucsdata.unitab_line[c & 255];
            break;
        case CSET_LINEDRW:
            c = inst->ucsdata.unitab_xterm[c & 255];
            break;
        case CSET_SCOACS:
            c = inst->ucsdata.unitab_scoacs[c & 255];
            break;
        }
        /* Font-encoding mode can leave the ASCII direct-character marker.
         * Decode it before constructing a Unicode URI, as clipboard code does.
         */
        switch (c & CSET_MASK) {
        case CSET_ACP:
            c = inst->ucsdata.unitab_font[c & 255];
            break;
        case CSET_OEMCP:
            c = inst->ucsdata.unitab_oemcp[c & 255];
            break;
        }
        if (DIRECT_CHAR(c) || DIRECT_FONT(c))
            c &= 255;
        if (g_unichar_validate(c))
            g_string_append_unichar(text, c);
    }
    GRegex *regex =
        g_regex_new("https?://[^\\s<>\"']+", G_REGEX_CASELESS, 0, NULL);
    GMatchInfo *match = NULL;
    bool opened = false;
    g_regex_match(regex, text->str, 0, &match);
    while (g_match_info_matches(match)) {
        int start, end;
        g_match_info_fetch_pos(match, 0, &start, &end);
        while (end > start && strchr(".,;:!?)]}", text->str[end - 1]))
            end--;
        if (target >= (size_t)start && target < (size_t)end) {
            char *url = g_strndup(text->str + start, end - start);
            GUri *uri = g_uri_parse(url, 0, NULL);
            if (uri && g_uri_get_host(uri) && *g_uri_get_host(uri)) {
                GError *error = NULL;
                GAppLaunchContext *context = g_app_launch_context_new();
                const char *bundled_env[] = {"LD_LIBRARY_PATH",
                                             "GIO_MODULE_DIR",
                                             "GIO_EXTRA_MODULES",
                                             "GSETTINGS_SCHEMA_DIR",
                                             "APPDIR",
                                             "APPIMAGE"};
                for (size_t n = 0; n < lenof(bundled_env); n++)
                    g_app_launch_context_unsetenv(context, bundled_env[n]);
                opened =
                    g_app_info_launch_default_for_uri(url, context, &error);
                g_object_unref(context);
                if (error)
                    g_error_free(error);
            }
            if (uri)
                g_uri_unref(uri);
            g_free(url);
            break;
        }
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(regex);
    g_string_free(text, TRUE);
    return opened;
}
