/*
 * Implementation of Filename for Unix, including f_open().
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "putty.h"

Filename *filename_from_str(const char *str)
{
    Filename *fn = snew(Filename);
    fn->path = dupstr(str);
    return fn;
}

Filename *filename_copy(const Filename *fn)
{
    return filename_from_str(fn->path);
}

const char *filename_to_str(const Filename *fn)
{
    return fn->path;
}

bool filename_equal(const Filename *f1, const Filename *f2)
{
    return !strcmp(f1->path, f2->path);
}

bool filename_is_null(const Filename *fn)
{
    return !fn->path[0];
}

void filename_free(Filename *fn)
{
    sfree(fn->path);
    sfree(fn);
}

void filename_serialise(BinarySink *bs, const Filename *f)
{
    put_asciz(bs, f->path);
}
Filename *filename_deserialise(BinarySource *src)
{
    return filename_from_str(get_asciz(src));
}

char filename_char_sanitise(char c)
{
    if (c == '/')
        return '.';
    return c;
}

FILE *f_open(const Filename *filename, char const *mode, bool is_private)
{
    if (!is_private) {
        return fopen(filename->path, mode);
    } else {
        int fd;
        assert(mode[0] == 'w');        /* is_private is meaningless for read,
                                          and tricky for append */
        fd = open(filename->path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0)
            return NULL;
#ifdef __APPLE__
        if (fchmod(fd,0600)<0) {close(fd);return NULL;}
#endif
        FILE *fp=fdopen(fd,mode);if(!fp)close(fd);return fp;
    }
}
