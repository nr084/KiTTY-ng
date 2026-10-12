/* Reuse KiTTY-ng translations and portable values; never translate payloads. */
#include "putty.h"
#include "cli-language.h"
#include "names.h"
#include "portable.h"
#include "storage.h"
#include <ctype.h>
static const struct {
    const char *en, *ru;
} translations[] = {
    {"Run command…", "Выполнить команду…"},
    {"Lock / unlock input", "Блокировать / разблокировать ввод"},
    {"Enable custom shortcuts", "Пользовательские сочетания клавиш"},
    {"Ctrl-click opens web links", "Ctrl+щелчок открывает веб-ссылки"},
    {"Automation", "Автоматизация"},
    {"File transfer", "Передача файлов"},
    {"Commands after login", "Команды после входа"},
    {"Startup automation", "Автоматизация после входа"},
    {"Automatic command", "Автокоманда"},
    {"UTF-8 startup script", "Скрипт UTF-8"},
    {"Initial delay (ms)", "Начальная задержка (мс)"},
    {"Delay between characters (ms)", "Задержка между символами (мс)"},
    {"Delay between commands (ms)", "Задержка между командами (мс)"},
    {"Reconnection", "Переподключение"},
    {"Reconnect delay (seconds; 0 disables)",
     "Задержка переподключения (сек; 0 — отключено)"},
    {"SCP / SFTP transfers", "Передача SCP / SFTP"},
    {"Remote directory", "Удалённый каталог"},
    {"Alternate destination", "Другой адрес назначения"},
    {"Additional kscp options", "Дополнительные параметры kscp"},
    {"Shared proxy profile", "Общий профиль прокси"},
    {"Proxy profile from Proxies", "Профиль из Proxies"},
    {"KiTTY-ng window options", "Параметры окна KiTTY-ng"},
    {"Start maximized", "Запускать развёрнутым"},
    {"Start fullscreen", "Запускать на весь экран"},
    {"Always on top", "Поверх окон"},
    {"Allow OSC 52 clipboard writes", "Разрешить запись в буфер через OSC 52"},
#include "../windows/kittyng-translations.h"
#include "cli-extra-translations.h"
#include "error-translations.h"
#include "translations.h"
};
static int language = -1;
void kr_mac_cli_set_language(const char *name) {
    language = !strcmp(name, "ru");
    setenv("KITTY_NG_LANGUAGE", language ? "ru" : "en", 1);
}
static int choice(void) {
    if (language >= 0)
        return language;
    const char *locale = getenv("LC_ALL");
    if (!locale || !*locale)
        locale = getenv("LC_MESSAGES");
    if (!locale || !*locale)
        locale = getenv("LANG");
    language = locale && !strncmp(locale, "ru", 2);
    const char *base = kr_mac_root();
    char *path = base ? dupcat(base, "/values.ini") : NULL;
    FILE *fp = path ? fopen(path, "rb") : NULL;
    sfree(path);
    if (fp) {
        strbuf *bytes = strbuf_new();
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), fp)) &&
               bytes->len < 1048576)
            put_data(bytes, buffer, n);
        fclose(fp);
        char *text = kr_text_from_bytes(bytes->u, bytes->len);
        strbuf_free(bytes);
        if (text) {
            char *cursor = text, *line;
            bool active = false;
            while ((line = strsep(&cursor, "\n"))) {
                while (isspace((unsigned char)*line))
                    line++;
                if (*line == '[')
                    active = !strncasecmp(line, "[values]", 8);
                else if (active && !strncmp(line, "UILanguage=", 11) &&
                         (line[11] == '0' || line[11] == '1'))
                    language = line[11] - '0';
            }
            free(text);
        }
    }
    const char *env = getenv("KITTY_NG_LANGUAGE");
    if (env && (!strcmp(env, "ru") || !strcmp(env, "en")))
        language = !strcmp(env, "ru");
    return language;
}
const char *kr_mac_translate(const char *text) {
    if (text && choice())
        for (size_t i = 0; i < lenof(translations); i++)
            if (!strcmp(text, translations[i].en))
                return translations[i].ru;
    return text;
}
int kr_mac_vfprintf(FILE *stream, const char *format, va_list ap) {
    return vfprintf(stream, kr_mac_translate(format), ap);
}
int kr_mac_fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = kr_mac_vfprintf(stream, format, ap);
    va_end(ap);
    return result;
}
int kr_mac_printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = kr_mac_vfprintf(stdout, format, ap);
    va_end(ap);
    return result;
}
int kr_mac_fputs(const char *text, FILE *stream) {
    return fputs(kr_mac_translate(text), stream);
}
char *kr_mac_dupvprintf(const char *format, va_list ap) {
    return dupvprintf(kr_mac_translate(format), ap);
}
char *kr_mac_dupprintf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char *result = kr_mac_dupvprintf(format, ap);
    va_end(ap);
    return result;
}
