/*
 * Wrapper around the Windows FormatMessage system for retrieving the
 * text of a system error code, with a simple API similar to strerror.
 *
 * Works by keeping a tree234 containing mappings from system error
 * codes to strings. Entries allocated in this tree are simply never
 * freed.
 *
 * Also, the returned string has its trailing newline removed (so it
 * can go in places like the Event Log that never want a newline), and
 * is prefixed with the error number (so that if a user sends an error
 * report containing a translated error message we can't read, we can
 * still find out what the error actually was).
 */

#include "putty.h"
#include "kittyng.h"

struct errstring {
    int error;
    char *text;
};

static int errstring_find(void *av, void *bv)
{
    int *a = (int *)av;
    struct errstring *b = (struct errstring *)bv;
    if (*a < b->error)
        return -1;
    if (*a > b->error)
        return +1;
    return 0;
}
static int errstring_compare(void *av, void *bv)
{
    struct errstring *a = (struct errstring *)av;
    return errstring_find(&a->error, bv);
}

static tree234 *errstrings[2];

const char *win_strerror(int error)
{
    struct errstring *es;
    int language = kr_language();

    if (!errstrings[language])
        errstrings[language] = newtree234(errstring_compare);

    es = find234(errstrings[language], &error, errstring_find);

    if (!es) {
        wchar_t wide[65536]; /* maximum size for FormatMessage is 64K */

        es = snew(struct errstring);
        es->error = error;
        if (!FormatMessageW((FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS), NULL, error,
                           MAKELANGID(language ? LANG_RUSSIAN : LANG_ENGLISH, SUBLANG_DEFAULT),
                           wide, lenof(wide)-1, NULL) &&
            !FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, error, 0, wide, lenof(wide)-1, NULL)) {
            swprintf(wide, lenof(wide),
                     L"(FormatMessage: %u)", (unsigned int)GetLastError());
        } else {
            int len = wcslen(wide);
            while (len > 0 && (wide[len-1] == L'\n' || wide[len-1] == L'\r'))
                wide[--len] = 0;
        }
        char *msgtext = dup_wc_to_mb(language ? CP_UTF8 : CP_ACP, wide, "");
        es->text = dupprintf("Error %d: %s", error, msgtext);
        sfree(msgtext);
        add234(errstrings[language], es);
    }

    return es->text;
}
