/*
 * Implement convenience wrappers on the awkward low-level functions
 * for accessing the Windows registry.
 */

#include "putty.h"
#include "kittyrus-portable.h"

HKEY open_regkey_fn(bool create, bool write, HKEY hk, const char *path, ...)
{
    HKEY toret = NULL;
    bool hk_needs_close = false;
    va_list ap;
    va_start(ap, path);

    if (kr_file_key(hk) || (hk == HKEY_CURRENT_USER && kr_portable() &&
        !strncmp(path, PUTTY_REG_POS, strlen(PUTTY_REG_POS)) &&
        (!path[strlen(PUTTY_REG_POS)] || path[strlen(PUTTY_REG_POS)] == '\\'))) {
        strbuf *joined = strbuf_new();
        for (; path; path = va_arg(ap, const char *)) {
            if (joined->len) put_byte(joined, '\\');
            put_dataz(joined, path);
        }
        HKEY result = kr_file_open(create, hk, joined->s);
        strbuf_free(joined);
        va_end(ap);
        return result;
    }

    for (; path; path = va_arg(ap, const char *)) {
        HKEY hk_sub = NULL;

        DWORD access = KEY_READ | (write ? KEY_WRITE : 0);
        LONG status;
        if (create)
            status = RegCreateKeyEx(
                hk, path, 0, NULL, REG_OPTION_NON_VOLATILE,
                access, NULL, &hk_sub, NULL);
        else
            status = RegOpenKeyEx(hk, path, 0, access, &hk_sub);

        if (status != ERROR_SUCCESS)
            goto out;

        if (hk_needs_close)
            RegCloseKey(hk);
        hk = hk_sub;
        hk_needs_close = true;
    }

    toret = hk;
    hk = NULL;
    hk_needs_close = false;

  out:
    va_end(ap);
    if (hk_needs_close)
        RegCloseKey(hk);
    return toret;
}

void close_regkey(HKEY key)
{
    if (kr_file_key(key)) { kr_file_close(key); return; }
    RegCloseKey(key);
}

void del_regkey(HKEY key, const char *name)
{
    if (kr_file_key(key)) { kr_file_delete(key, name); return; }
    RegDeleteKey(key, name);
}

char *enum_regkey(HKEY key, int index)
{
    if (kr_file_key(key)) return kr_file_enum(key, index);
    size_t regbuf_size = MAX_PATH + 1;
    char *regbuf = snewn(regbuf_size, char);

    while (1) {
        LONG status = RegEnumKey(key, index, regbuf, regbuf_size);
        if (status == ERROR_SUCCESS)
            return regbuf;
        if (status != ERROR_MORE_DATA) {
            sfree(regbuf);
            return NULL;
        }
        sgrowarray(regbuf, regbuf_size, regbuf_size);
    }
}

bool get_reg_dword(HKEY key, const char *name, DWORD *out)
{
    if (kr_file_key(key)) {
        char *value = kr_file_get(key, name);
        bool plain = kr_plain_key(key);
        bool ok = value && (plain || !strncmp(value, "d:", 2));
        if (ok) *out = (DWORD)strtoul(value + (plain ? 0 : 2), NULL, 10);
        sfree(value);
        return ok;
    }
    DWORD type, size;
    size = sizeof(*out);

    if (RegQueryValueEx(key, name, 0, &type,
                        (BYTE *)out, &size) != ERROR_SUCCESS ||
        size != sizeof(*out) || type != REG_DWORD)
        return false;
    else
        return true;
}

bool put_reg_dword(HKEY key, const char *name, DWORD value)
{
    if (kr_file_key(key)) {
        char *text = kr_plain_key(key) ? dupprintf("%ld", (long)(int32_t)value) :
            dupprintf("d:%lu", (unsigned long)value);
        bool ok = kr_file_put(key, name, text);
        sfree(text);
        return ok;
    }
    return RegSetValueEx(key, name, 0, REG_DWORD, (CONST BYTE *) &value,
                         sizeof(value)) == ERROR_SUCCESS;
}

char *get_reg_sz(HKEY key, const char *name)
{
    if (kr_file_key(key)) {
        char *text = kr_file_get(key, name);
        if (kr_plain_key(key)) return text;
        strbuf *value = text && !strncmp(text, "s:", 2) ?
            base64_decode_sb(ptrlen_from_asciz(text + 2)) : NULL;
        sfree(text);
        return value ? strbuf_to_str(value) : NULL;
    }
    DWORD type, size;

    if (RegQueryValueEx(key, name, 0, &type, NULL,
                        &size) != ERROR_SUCCESS || type != REG_SZ)
        return NULL;                   /* not a string */

    size_t allocsize = size+1;         /* allow for an extra NUL if needed */
    char *toret = snewn(allocsize, char);
    if (RegQueryValueEx(key, name, 0, &type, (BYTE *)toret,
                        &size) != ERROR_SUCCESS || type != REG_SZ) {
        sfree(toret);
        return NULL;
    }
    assert(size < allocsize);
    toret[size] = '\0'; /* add an extra NUL in case RegQueryValueEx
                         * didn't supply one */

    return toret;
}

bool put_reg_sz(HKEY key, const char *name, const char *str)
{
    if (kr_file_key(key)) {
        if (kr_plain_key(key)) return kr_file_put(key, name, str);
        strbuf *encoded = base64_encode_sb(ptrlen_from_asciz(str), 0);
        char *text = dupcat("s:", encoded->s);
        bool ok = kr_file_put(key, name, text);
        strbuf_free(encoded); sfree(text);
        return ok;
    }
    /* You have to store the trailing NUL as well */
    return RegSetValueEx(key, name, 0, REG_SZ, (CONST BYTE *)str,
                         1 + strlen(str)) == ERROR_SUCCESS;
}

/*
 * REG_MULTI_SZ items are stored as a concatenation of NUL-terminated
 * strings, terminated in turn with an empty string, i.e. a second
 * consecutive NUL.
 *
 * We represent these in their storage format, as a strbuf - but
 * *without* the second consecutive NUL.
 *
 * So you can build up a new MULTI_SZ value in a strbuf by calling
 * put_asciz once per output string and then put_reg_multi_sz; and you
 * can consume one by initialising a BinarySource to the result of
 * get_reg_multi_sz, and then calling get_asciz on it and assuming
 * that !get_err(src) means you have a real output string.
 *
 * Also, calling strbuf_to_str on one of these will give you back a
 * bare 'char *' with the same double-NUL termination, to pass back to
 * a caller.
 */
strbuf *get_reg_multi_sz(HKEY key, const char *name)
{
    if (kr_file_key(key)) {
        char *text = kr_file_get(key, name);
        strbuf *value = text && !strncmp(text, "m:", 2) ?
            base64_decode_sb(ptrlen_from_asciz(text + 2)) : NULL;
        sfree(text);
        return value;
    }
    DWORD type, size;

    if (RegQueryValueEx(key, name, 0, &type, NULL,
                        &size) != ERROR_SUCCESS || type != REG_MULTI_SZ)
        return NULL;                   /* not a string */

    strbuf *toret = strbuf_new();
    void *ptr = strbuf_append(toret, (size_t)size + 2);
    if (RegQueryValueEx(key, name, 0, &type, (BYTE *)ptr,
                        &size) != ERROR_SUCCESS || type != REG_MULTI_SZ) {
        strbuf_free(toret);
        return NULL;
    }
    strbuf_shrink_to(toret, size);
    /* Ensure we end with exactly one \0 */
    while (strbuf_chomp(toret, '\0'));
    put_byte(toret, '\0');
    return toret;
}

bool put_reg_multi_sz(HKEY key, const char *name, strbuf *str)
{
    if (kr_file_key(key)) {
        strbuf *encoded = base64_encode_sb(ptrlen_from_strbuf(str), 0);
        char *text = dupcat("m:", encoded->s);
        bool ok = kr_file_put(key, name, text);
        strbuf_free(encoded); sfree(text);
        return ok;
    }
    /*
     * Of course, to write our string list into the registry, we _do_
     * have to include both trailing NULs. But this is easy, because a
     * strbuf is also designed to hold a single string and make it
     * conveniently accessible in NUL-terminated form, so it stores a
     * NUL in its buffer just beyond its formal length. So we just
     * include that extra byte in the data we write.
     */
    return RegSetValueEx(key, name, 0, REG_MULTI_SZ, (CONST BYTE *)str->s,
                         str->len + 1) == ERROR_SUCCESS;
}

char *get_reg_sz_simple(HKEY key, const char *name, const char *leaf)
{
    HKEY subkey = open_regkey_ro(key, name);
    if (!subkey)
        return NULL;
    char *toret = get_reg_sz(subkey, leaf);
    close_regkey(subkey);
    return toret;
}
