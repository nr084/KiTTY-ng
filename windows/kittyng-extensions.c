#include "putty.h"
#include "kittyng-portable.h"
#include "kittyng-extensions.h"
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <math.h>

wchar_t *kr_command_text(const char *text, bool escapes, bool submit)
{
    strbuf *decoded = strbuf_new();
    bool previous_cr = false;
    for (size_t i = 0; text[i]; i++) {
        unsigned char c = text[i];
        if (escapes && c == '\\' && text[i+1]) {
            char next = text[i+1];
            if (next == 'n' || next == 'r') { c = next == 'n' ? '\n' : '\r'; i++; }
            else if (next == 't') { c = '\t'; i++; }
            else if (next == '\\') { c = '\\'; i++; }
        }
        if (c == '\r') {
            put_byte(decoded, c); previous_cr = true;
        } else if (c == '\n') {
            if (!previous_cr) put_byte(decoded, '\r');
            previous_cr = false;
        } else { put_byte(decoded, c); previous_cr = false; }
    }
    if (submit && decoded->len && decoded->s[decoded->len-1] != '\r') put_byte(decoded, '\r');
    UINT cp = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, decoded->s, -1, NULL, 0) ? CP_UTF8 : CP_ACP;
    wchar_t *result = dup_mb_to_wc(cp, decoded->s);
    smemclr(decoded->s, decoded->len); strbuf_free(decoded);
    return result;
}

/* Open visible HTTP(S) text only; shell/file URLs and OSC title commands
 * cannot turn a remote string into execution of a local application. */
char *kr_url_at(const char *line, size_t column)
{
    size_t length = strlen(line);
    if (column >= length) return NULL;
    for (size_t i = 0; i < length; i++) {
        if (_strnicmp(line+i, "http://", 7) && _strnicmp(line+i, "https://", 8)) continue;
        if (i && (isalnum((unsigned char)line[i-1]) || line[i-1] == '_')) continue;
        size_t end = i;
        while (line[end] && (unsigned char)line[end] > 32 && !strchr("<>\"'", line[end])) end++;
        while (end > i && strchr(".,;:!?)]}", line[end-1])) end--;
        size_t prefix = !_strnicmp(line+i, "https://", 8) ? 8 : 7;
        if (end <= i+prefix || column < i || column >= end) continue;
        strbuf *url = strbuf_new(); put_data(url, line+i, end-i); return strbuf_to_str(url);
    }
    return NULL;
}

bool kr_script_reference(const char *text)
{
    if (strpbrk(text, "\r\n")) return false;
    size_t n = strlen(text);
    const char *extensions[] = {".sh", ".ksh", ".sql", ".txt", ".ps1"};
    for (size_t i = 0; i < lenof(extensions); i++) {
        size_t suffix = strlen(extensions[i]);
        if (n > suffix && !_stricmp(text+n-suffix, extensions[i])) return true;
    }
    return false;
}

wchar_t *kr_clipboard_text(const char *osc)
{
    const char *separator = strchr(osc, ';');
    if (!separator || (separator != osc && (separator != osc+1 || osc[0] != 'c'))) return NULL;
    const char *encoded = separator + 1;
    size_t length = strlen(encoded);
    if (!length || length > 65532 || (length % 4) || !base64_valid(ptrlen_from_asciz(encoded))) return NULL;
    strbuf *decoded = base64_decode_sb(ptrlen_from_asciz(encoded));
    wchar_t *result = NULL;
    if (!memchr(decoded->s, 0, decoded->len) &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, decoded->s, -1, NULL, 0))
        result = dup_mb_to_wc(CP_UTF8, decoded->s);
    strbuf_free(decoded);
    return result;
}

int kr_scroll_lines(int setting, int rows)
{
    if (setting == -2) return max(1, rows);
    if (setting == -1) return max(1, rows/2);
    return setting > 0 ? min(setting, 10000) : 3;
}

char *kr_ini_string(const char *section, const char *name, const char *fallback)
{
    const wchar_t *root = kr_portable_root();
    wchar_t executable[32768];
    if (!root) {
        DWORD length = GetModuleFileNameW(NULL, executable, lenof(executable));
        if (!length || length >= lenof(executable)) return dupstr(fallback);
        wchar_t *slash = wcsrchr(executable, L'\\'); if (!slash) return dupstr(fallback);
        *slash = 0; root = executable;
    }
    wchar_t *file = snewn(wcslen(root)+12, wchar_t);
    swprintf(file, wcslen(root)+12, L"%ls\\kitty.ini", root);
    wchar_t *key = dup_mb_to_wc(CP_UTF8, name);
    wchar_t *group = dup_mb_to_wc(CP_UTF8, section), *default_value = dup_mb_to_wc(CP_UTF8, fallback);
    wchar_t *buffer = snewn(65536, wchar_t);
    DWORD count = GetPrivateProfileStringW(group, key, default_value, buffer, 65536, file);
    char *result = count >= 65535 ? dupstr(fallback) : dup_wc_to_mb(CP_UTF8, buffer, "");
    sfree(buffer); sfree(key); sfree(file); sfree(group); sfree(default_value); return result;
}

int kr_ini_int(const char *name, int fallback)
{
    char *value = kr_ini_string("KiTTY", name, ""), *end;
    errno = 0; long parsed = strtol(value, &end, 10);
    int result = *value && !*end && !errno && parsed >= INT_MIN && parsed <= INT_MAX ? (int)parsed : fallback;
    sfree(value); return result;
}

int kr_ini_milliseconds(const char *name, int fallback)
{
    char *text = kr_ini_string("KiTTY", name, ""), *end;
    errno = 0; double seconds = strtod(text, &end);
    int result = *text && !*end && !errno && isfinite(seconds) && seconds >= 0 && seconds <= 86400 ?
        (int)(seconds*1000) : fallback;
    sfree(text); return result;
}

UINT kr_parse_shortcut(const char *text)
{
    UINT modifiers = 0, key = 0;
    if (isdigit((unsigned char)text[0]) && text[1]) {
        char *end; unsigned long value = strtoul(text, &end, 10);
        if (*end || value > 15755) return 0;
        const unsigned sums[] = {8000, 4000, 2000, 1000, 500};
        const UINT flags[] = {MOD_WIN, 16, MOD_ALT, MOD_CONTROL, MOD_SHIFT};
        for (size_t i = 0; i < lenof(sums); i++) if (value >= sums[i]) { value -= sums[i]; modifiers |= flags[i]; }
        return value && value <= 255 ? value | (modifiers << 16) : 0;
    }
    while (*text) {
        while (*text == '+' || *text == ' ') text++;
        bool brace = *text == '{'; if (brace) text++;
        char token[32]; size_t n = 0;
        while (*text && *text != '}' && *text != '+' && *text != ' ') {
            if (n+1 >= sizeof(token)) return 0;
            token[n++] = toupper((unsigned char)*text++);
        }
        token[n] = 0;
        if (brace) { if (*text != '}') return 0; text++; }
        if (!strcmp(token, "CONTROL") || !strcmp(token, "CTRL")) modifiers |= MOD_CONTROL;
        else if (!strcmp(token, "SHIFT")) modifiers |= MOD_SHIFT;
        else if (!strcmp(token, "ALT")) modifiers |= MOD_ALT;
        else if (!strcmp(token, "WIN")) modifiers |= MOD_WIN;
        else if (!strcmp(token, "ALTGR")) modifiers |= 16;
        else {
            if (key) return 0;
            if (n == 1 && isalnum((unsigned char)token[0])) key = token[0];
            else if (token[0] == 'F' && n >= 2) {
                char *end; long f = strtol(token+1, &end, 10);
                if (*end || f < 1 || f > 24) return 0;
                key = VK_F1 + f - 1;
            } else {
                static const struct { const char *name; UINT key; } names[] = {
                    {"RETURN", VK_RETURN}, {"ESCAPE", VK_ESCAPE}, {"SPACE", VK_SPACE},
                    {"BACK", VK_BACK}, {"TAB", VK_TAB}, {"INS", VK_INSERT}, {"DEL", VK_DELETE},
                    {"HOME", VK_HOME}, {"END", VK_END}, {"UP", VK_UP}, {"DOWN", VK_DOWN},
                    {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"PRIOR", VK_PRIOR}, {"NEXT", VK_NEXT},
                    {"OEM_PLUS", VK_OEM_PLUS}, {"OEM_MINUS", VK_OEM_MINUS},
                    {"OEM_COMMA", VK_OEM_COMMA}, {"OEM_PERIOD", VK_OEM_PERIOD}
                };
                for (size_t i = 0; i < lenof(names); i++) if (!strcmp(token, names[i].name)) key = names[i].key;
                if (!key) return 0;
            }
        }
    }
    return key ? key | (modifiers << 16) : 0;
}

void kr_load_proxy(Conf *conf)
{
    const char *name = conf_get_str(conf, CONF_proxyselection);
    conf_set_str(conf, CONF_kr_proxy_error, "");
    if (!*name || !strcmp(name, "- Session defined proxy -")) return;
    if (!strcmp(name, "- No proxy -")) { conf_set_int(conf, CONF_proxy_type, PROXY_NONE); return; }
    strbuf *encoded = strbuf_new(); escape_registry_key(name, encoded);
    HKEY key = open_regkey_ro(HKEY_CURRENT_USER, PUTTY_REG_POS "\\Proxies", encoded->s);
    strbuf_free(encoded);
    DWORD method = 0, port = 0;
    int type;
    bool valid = key && get_reg_dword(key, "ProxyMethod", &method) && method <= 5 &&
        conf_enum_map_from_storage(conf_key_info[CONF_proxy_type].storage_enum, method, &type);
    if (valid) {
        conf_set_int(conf, CONF_proxy_type, type);
        conf_set_bool(conf, CONF_even_proxy_localhost, false);
        conf_set_int(conf, CONF_proxy_dns, AUTO);
        conf_set_int(conf, CONF_proxy_log_to_term, AUTO);
        static const struct { const char *name; int option; } strings[] = {
            {"ProxyHost", CONF_proxy_host}, {"ProxyUsername", CONF_proxy_username},
            {"ProxyPassword", CONF_proxy_password}, {"ProxyExcludeList", CONF_proxy_exclude_list},
            {"ProxyTelnetCommand", CONF_proxy_telnet_command}
        };
        for (size_t i = 0; i < lenof(strings); i++) {
            char *value = get_reg_sz(key, strings[i].name);
            conf_set_str(conf, strings[i].option, value ? value : "");
            if (value) {
                smemclr(value, strlen(value)); sfree(value);
            }
        }
        if (type != PROXY_NONE && type != PROXY_CMD) {
            valid = get_reg_dword(key, "ProxyPort", &port) && port > 0 && port <= 65535 &&
                *conf_get_str(conf, CONF_proxy_host);
            if (valid) conf_set_int(conf, CONF_proxy_port, port);
        }
        DWORD value;
        if (get_reg_dword(key, "ProxyDNS", &value)) {
            int dns;
            if (conf_enum_map_from_storage(conf_key_info[CONF_proxy_dns].storage_enum, value, &dns)) conf_set_int(conf, CONF_proxy_dns, dns);
            else valid = false;
        }
        if (get_reg_dword(key, "ProxyLocalhost", &value)) conf_set_bool(conf, CONF_even_proxy_localhost, value != 0);
        if (get_reg_dword(key, "ProxyLogToTerm", &value)) conf_set_int(conf, CONF_proxy_log_to_term, min(value, 2));
        if ((type == PROXY_CMD || type == PROXY_TELNET) && !*conf_get_str(conf, CONF_proxy_telnet_command)) valid = false;
    }
    if (key) close_regkey(key);
    if (!valid) conf_set_str(conf, CONF_kr_proxy_error, "Selected proxy profile is missing or invalid; connection is blocked.");
}

void kr_copy_proxy(Conf *destination, Conf *source)
{
    const int integers[] = {CONF_proxy_type, CONF_proxy_port, CONF_proxy_dns, CONF_proxy_log_to_term};
    const int strings[] = {CONF_proxy_host, CONF_proxy_username, CONF_proxy_password, CONF_proxy_exclude_list, CONF_proxy_telnet_command};
    for (size_t i = 0; i < lenof(integers); i++) conf_set_int(destination, integers[i], conf_get_int(source, integers[i]));
    for (size_t i = 0; i < lenof(strings); i++) conf_set_str(destination, strings[i], conf_get_str(source, strings[i]));
    conf_set_bool(destination, CONF_even_proxy_localhost, conf_get_bool(source, CONF_even_proxy_localhost));
}
