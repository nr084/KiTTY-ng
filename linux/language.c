/* Reuse KiTTY-ng translations and portable values; never translate payloads. */
#include "putty.h"
#include "kittyng-i18n.h"
#include "cli-language.h"
#include "names.h"
#include "portable.h"
#include "storage.h"
#include <ctype.h>
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
    return kr_i18n_translate(choice(), KR_TEXT_LINUX, text);
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
