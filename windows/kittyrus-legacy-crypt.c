/* Portable implementation of KiTTY's legacy nbcrypt wire/storage format.
 * Interoperability is tested against cyd01/KiTTY's original binary library.
 * This format is reversible obfuscation; it does not protect files from readers.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "kittyrus-legacy-crypt.h"

static const char alphabet[] =
    "AZERTYUIOPQSDFGHJKLMWXCVBNazertyuiopqsdfghjklmwxcvbn0123456789+/";

static void shuffle(char pattern[64], const char *key, size_t keylen)
{
    if (!keylen) return;
    size_t k = 0;
    for (size_t r = 0; r < keylen / 2 / 64 + 1; r++) {
        for (uint32_t i = 0; i < 64; i++) {
            uint32_t j = (i + (uint32_t)(int32_t)(int8_t)key[k]) % 64;
            char swap = pattern[i]; pattern[i] = pattern[j]; pattern[j] = swap;
            if (++k == keylen) k = 0;
        }
    }
}

char *kr_legacy_encode(const char *plain, const char *key, const char seed[5])
{
    size_t n = strlen(plain), keylen = strlen(key);
    if (n > (SIZE_MAX - 6) / 5) return NULL;
    char *out = malloc(5 * n + 6);
    if (!out) return NULL;
    memcpy(out, seed, 5);
    size_t used = 5, since_shuffle = 0;
    char pattern[64]; memcpy(pattern, alphabet, 64);
    shuffle(pattern, seed, 5);
    for (size_t i = 0; i < n; i++) {
        unsigned value = (unsigned char)plain[i];
        while (value >= 63) {
            out[used++] = pattern[63];
            value -= 63;
            shuffle(pattern, key, keylen);
            since_shuffle = 0;
        }
        out[used++] = pattern[value];
        if (++since_shuffle == 64) {
            shuffle(pattern, key, keylen);
            since_shuffle = 0;
        }
    }
    out[used] = 0;
    return out;
}

char *kr_legacy_decode(const char *encoded, const char *key)
{
    size_t n = strlen(encoded), keylen = strlen(key);
    if (n < 5) return NULL;
    char *out = malloc(n - 4);
    if (!out) return NULL;
    char pattern[64]; memcpy(pattern, alphabet, 64);
    shuffle(pattern, encoded, 5);
    size_t used = 0, since_shuffle = 0;
    unsigned value = 0;
    for (size_t i = 5; i < n; i++) {
        if (encoded[i] == '\n') continue;
        if (encoded[i] == pattern[63]) {
            value += 63;
            if (value > 255) { free(out); return NULL; }
            shuffle(pattern, key, keylen);
            since_shuffle = 0;
            continue;
        }
        size_t pos = 0;
        while (pos < 63 && pattern[pos] != encoded[i]) pos++;
        if (pos == 63 || value + pos > 255 || !value && !pos) {
            free(out); return NULL;
        }
        out[used++] = (char)(value + pos);
        value = 0;
        if (++since_shuffle == 64) {
            shuffle(pattern, key, keylen);
            since_shuffle = 0;
        }
    }
    if (value) { free(out); return NULL; }
    out[used] = 0;
    return out;
}

void kr_legacy_mask(char *password)
{
    static const unsigned char mask[] = {
        0xc2,0xa4,0xc2,0xa5,0xc2,0xa9,0xc2,0xaa,
        0xc2,0xb3,0xc2,0xbc,0xc2,0xbd,0xc2,0xbe
    };
    size_t n = strlen(password);
    for (size_t i = 0; i < n; i++)
        if (((unsigned char)password[i] ^ mask[i % sizeof(mask)]) == 0)
            return; /* Matches KiTTY's all-or-nothing MASKPASS rule. */
    for (size_t i = 0; i < n; i++) password[i] ^= mask[i % sizeof(mask)];
}
