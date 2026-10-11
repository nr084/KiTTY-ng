#ifndef KITTYNG_LEGACY_CRYPT_H
#define KITTYNG_LEGACY_CRYPT_H
/* KiTTY's reversible password format, NOT the bcrypt password hash. */
char *kr_legacy_encode(const char *plain, const char *key, const char seed[5]);
char *kr_legacy_decode(const char *encoded, const char *key);
void kr_legacy_mask(char *password);
#endif
