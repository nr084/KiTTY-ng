#include "kittyng-quick-commands.h"
static void quick_command_activate(GtkMenuItem *item, gpointer data)
{
    GtkFrontend *inst = data;
    if (inst->input_locked || !inst->backend || !backend_sendok(inst->backend)) return;
    const char *text = g_object_get_data(G_OBJECT(item), "quick-instruction");
    if (!text) return;
    term_keyinput(inst->term, CS_UTF8, text, strlen(text));
    if (g_object_get_data(G_OBJECT(item), "quick-enter"))
        term_keyinput(inst->term, CS_UTF8, "\r", 1);
}
static void quick_commands_show(GtkWidget *menu, gpointer data)
{
    GtkFrontend *inst = data;
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *p = children; p; p = p->next) gtk_widget_destroy(p->data);
    g_list_free(children);
    KrQuickCommands commands;
    char *error = NULL;
    bool loaded = kr_quick_load(&commands, kr_mac_translate("Update Ubuntu"), &error);
    int count = 0;
    if (loaded) for (int i = 0; i < KR_QUICK_LIMIT; i++) {
        KrQuickCommand *c = &commands.items[i];
        if (!c->name || !c->instruction) continue;
        GtkWidget *item = gtk_menu_item_new_with_label(c->name);
        g_object_set_data_full(G_OBJECT(item), "quick-instruction", g_strdup(c->instruction), g_free);
        g_object_set_data(G_OBJECT(item), "quick-enter", GINT_TO_POINTER(c->enter));
        g_signal_connect(item, "activate", G_CALLBACK(quick_command_activate), inst);
        gtk_widget_set_sensitive(item, !inst->input_locked && inst->backend && backend_sendok(inst->backend));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        gtk_widget_show(item); count++;
    }
    if (!count) {
        GtkWidget *item = gtk_menu_item_new_with_label(kr_mac_translate(error ? error : "No quick commands"));
        gtk_widget_set_sensitive(item, false);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item); gtk_widget_show(item);
    }
    sfree(error); kr_quick_free(&commands);
}
