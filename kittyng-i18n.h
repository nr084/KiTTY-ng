/* Shared UTF-8 translation engine. No platform UI or dynamic loading. */
#ifndef KITTYNG_I18N_H
#define KITTYNG_I18N_H
#include <stddef.h>
typedef enum { KR_LANG_EN = 0, KR_LANG_RU = 1 } KrLanguage;
typedef enum { KR_TEXT_WINDOWS, KR_TEXT_LINUX, KR_TEXT_MAC_CLI,
               KR_TEXT_MAC_UI } KrTextContext;
typedef struct { int id; const char *code, *native_name; } KrLanguageInfo;
size_t kr_i18n_language_count(void);
const KrLanguageInfo *kr_i18n_language(size_t index);
int kr_i18n_language_id(const char *code);
const char *kr_i18n_translate(int language, KrTextContext context, const char *key);
/* Compatibility only. New UI bindings must retain the original message key. */
const char *kr_i18n_source(KrTextContext context, const char *text);
#endif
