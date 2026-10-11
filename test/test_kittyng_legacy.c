#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kittyng-legacy-crypt.h"

static char *unhex(const char *hex)
{
    size_t n = strlen(hex) / 2;
    char *out = malloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        unsigned value;
        if (sscanf(hex + 2 * i, "%2x", &value) != 1) abort();
        out[i] = (char)value;
    }
    out[n] = 0; return out;
}

int main(void)
{
    char plainhex[8192],keyhex[8192],encoded[8192];
    unsigned count = 0;
    while (scanf("%8191s %8191s %8191s", plainhex, keyhex, encoded) == 3) {
        char *plain = unhex(plainhex), *key = unhex(!strcmp(keyhex, "-") ? "" : keyhex);
        char *decoded = kr_legacy_decode(encoded, key);
        char *back = kr_legacy_encode(plain, key, encoded);
        if (!decoded || strcmp(decoded, plain) || !back || strcmp(back, encoded)) {
            fprintf(stderr, "Legacy fixture %u failed\n", count); return 1;
        }
        kr_legacy_mask(decoded); kr_legacy_mask(decoded);
        if (strcmp(decoded, plain)) return 2;
        free(plain); free(key); free(decoded); free(back); count++;
    }
    if (!count || kr_legacy_decode("bad", "key") ||
        kr_legacy_decode("abcde!!!!!!!!!!!", "key")) return 3;
    printf("PASS: %u original KiTTY crypto fixtures\n", count);
    return 0;
}
