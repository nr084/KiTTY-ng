/* Catalog lookup is independent of preferences, OS locale, and UI widgets. */
#include "kittyng-i18n.h"
#include <string.h>
typedef struct { const char *key, *translation; } KrMessage;
#include "../locale/builtin.h"
#define COUNT(a) (sizeof(a) / sizeof(*(a)))
static const KrLanguageInfo languages[] = {
    { KR_LANG_EN, "en", "English" },
    { KR_LANG_RU, "ru", "\320\240\321\203\321\201\321\201\320\272\320\270\320\271" }
};
size_t kr_i18n_language_count(void) { return COUNT(languages); }
const KrLanguageInfo *kr_i18n_language(size_t index) {
    return index < COUNT(languages) ? &languages[index] : NULL;
}
int kr_i18n_language_id(const char *code) {
    if (code) for (size_t i = 0; i < COUNT(languages); i++)
        if (!strcmp(code, languages[i].code)) return languages[i].id;
    return -1;
}
static const KrMessage *overrides(KrTextContext context, size_t *count) {
    switch (context) {
      case KR_TEXT_LINUX: *count=COUNT(linux_messages); return linux_messages;
      case KR_TEXT_MAC_CLI: *count=COUNT(macos_cli_messages); return macos_cli_messages;
      case KR_TEXT_MAC_UI: *count=COUNT(macos_ui_messages); return macos_ui_messages;
      default: *count=0; return NULL;
    }
}
static const char *lookup(const KrMessage *items, size_t count, const char *key) {
    size_t lo=0, hi=count;
    while (lo < hi) {
        size_t mid=lo+(hi-lo)/2;
        int order=strcmp(key, items[mid].key);
        if (!order) return items[mid].translation;
        if (order < 0) hi=mid; else lo=mid+1;
    }
    return NULL;
}
const char *kr_i18n_translate(int language, KrTextContext context, const char *key) {
    if (!key || language != KR_LANG_RU) return key;
    size_t count;
    const KrMessage *items=overrides(context, &count);
    const char *result=lookup(items,count,key);
    if (!result) result=lookup(common_messages,COUNT(common_messages),key);
    return result ? result : key;
}
const char *kr_i18n_source(KrTextContext context, const char *text) {
    if (!text) return NULL;
    if (lookup(common_messages,COUNT(common_messages),text)) return text;
    size_t count;
    const KrMessage *items=overrides(context,&count);
    const char *found=NULL;
    for (size_t pass=0; pass<2; pass++) {
        for (size_t i=0; i<count; i++) {
            if (strcmp(text,items[i].translation)) continue;
            if (strcmp(text,kr_i18n_translate(KR_LANG_RU,context,items[i].key))) continue;
            /* Never guess when distinct source messages share a translation. */
            if (found && strcmp(found,items[i].key)) return text;
            found=items[i].key;
        }
        items=common_messages; count=COUNT(common_messages);
    }
    return found ? found : text;
}
