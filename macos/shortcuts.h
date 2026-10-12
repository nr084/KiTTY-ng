/* Portable KiTTY syntax; no AppKit/Win32 dependency. Derived from Windows parser. */
#include <ctype.h>
static unsigned kr_mac_parse_shortcut(const char *text)
{
    unsigned modifiers = 0, key = 0;
    if (isdigit((unsigned char)text[0]) && text[1]) {
        char *end; unsigned long value = strtoul(text, &end, 10);
        if (*end || value > 15755) return 0;
        const unsigned sums[] = {8000, 4000, 2000, 1000, 500};
        const unsigned flags[] = {8, 16, 1, 2, 4};
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
        if (!strcmp(token, "CONTROL") || !strcmp(token, "CTRL")) modifiers |= 2;
        else if (!strcmp(token, "SHIFT")) modifiers |= 4;
        else if (!strcmp(token, "ALT")) modifiers |= 1;
        else if (!strcmp(token, "WIN")) modifiers |= 8;
        else if (!strcmp(token, "ALTGR")) modifiers |= 16;
        else {
            if (key) return 0;
            if (n == 1 && isalnum((unsigned char)token[0])) key = token[0];
            else if (token[0] == 'F' && n >= 2) {
                char *end; long f = strtol(token+1, &end, 10);
                if (*end || f < 1 || f > 24) return 0;
                key = 112 + f - 1;
            } else {
                static const struct { const char *name; unsigned key; } names[] = {
                    {"RETURN", 13}, {"ESCAPE", 27}, {"SPACE", 32},
                    {"BACK", 8}, {"TAB", 9}, {"INS", 45}, {"DEL", 46},
                    {"HOME", 36}, {"END", 35}, {"UP", 38}, {"DOWN", 40},
                    {"LEFT", 37}, {"RIGHT", 39}, {"PRIOR", 33}, {"NEXT", 34},
                    {"OEM_PLUS", 187}, {"OEM_MINUS", 189},
                    {"OEM_COMMA", 188}, {"OEM_PERIOD", 190}
                };
                for (size_t i = 0; i < lenof(names); i++) if (!strcmp(token, names[i].name)) key = names[i].key;
                if (!key) return 0;
            }
        }
    }
    return key ? key | (modifiers << 16) : 0;
}
