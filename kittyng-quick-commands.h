#ifndef KITTYNG_QUICK_COMMANDS_H
#define KITTYNG_QUICK_COMMANDS_H
#define KR_QUICK_LIMIT 20
#define KR_QUICK_NAME_LIMIT 128
#define KR_QUICK_TEXT_LIMIT 65535
typedef struct KrQuickCommand { char *name, *instruction; bool enter; } KrQuickCommand;
typedef struct KrQuickCommands { KrQuickCommand items[KR_QUICK_LIMIT]; } KrQuickCommands;
void kr_quick_free(KrQuickCommands *list);
bool kr_quick_load(KrQuickCommands *list, const char *default_name, char **error);
bool kr_quick_update(int index, const char *name, const char *instruction, bool enter, char **error);
#endif
