/*
 * KiTTY-ng FS SFTP core, original implementation.
 * Wire format: draft-ietf-secsh-filexfer-02 (SFTP v3).
 * Extension: OpenSSH PROTOCOL, posix-rename@openssh.com, revision 1.
 *
 * One outstanding request per client keeps ownership and cancellation simple.
 * Received sizes, names, handles, attribute flags and request IDs are validated
 * before use. A framing failure permanently invalidates this connection.
 */
#include "sftp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FXP_INIT = 1, FXP_VERSION = 2, FXP_OPEN = 3, FXP_CLOSE = 4,
    FXP_READ = 5, FXP_WRITE = 6, FXP_LSTAT = 7, FXP_SETSTAT = 9,
    FXP_OPENDIR = 11, FXP_READDIR = 12, FXP_REMOVE = 13,
    FXP_MKDIR = 14, FXP_RMDIR = 15, FXP_REALPATH = 16,
    FXP_STAT = 17, FXP_RENAME = 18,
    FXP_STATUS = 101, FXP_HANDLE = 102, FXP_DATA = 103,
    FXP_NAME = 104, FXP_ATTRS = 105, FXP_EXTENDED = 200
};

typedef struct Buffer {
    unsigned char *data;
    size_t size;
    size_t capacity;
    size_t pos;
    int bad;
} Buffer;

static void release(Buffer *b)
{
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static int error_text(SfClient *c, uint32_t status, const char *text)
{
    c->last_status = status;
    snprintf(c->error, sizeof(c->error), "%s", text);
    return SF_ERR;
}

static int protocol_error(SfClient *c, const char *text)
{
    c->alive = 0;
    return error_text(c, SF_FX_BAD_MESSAGE, text);
}

static int transport_error(SfClient *c, const char *text)
{
    c->alive = 0;
    return error_text(c, SF_FX_CONNECTION_LOST, text);
}

static int reserve(Buffer *b, size_t more)
{
    size_t capacity, required;
    unsigned char *p;
    if (b->bad || more > SF_MAX_PACKET || b->size > SF_MAX_PACKET - more) {
        b->bad = 1;
        return 0;
    }
    required = b->size + more;
    if (required <= b->capacity) return 1;
    capacity = b->capacity ? b->capacity : 128u;
    while (capacity < required) {
        if (capacity > SF_MAX_PACKET / 2u) {
            capacity = SF_MAX_PACKET;
            break;
        }
        capacity *= 2u;
    }
    p = (unsigned char *)realloc(b->data, capacity);
    if (!p) {
        b->bad = 1;
        return 0;
    }
    b->data = p;
    b->capacity = capacity;
    return 1;
}

static void put_bytes(Buffer *b, const void *data, size_t n)
{
    if (!reserve(b, n)) return;
    if (n) memcpy(b->data + b->size, data, n);
    b->size += n;
}

static void put_u8(Buffer *b, unsigned char v)
{
    put_bytes(b, &v, 1);
}

static void encode_u32(unsigned char p[4], uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static uint32_t decode_u32(const unsigned char p[4])
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_u32(Buffer *b, uint32_t v)
{
    unsigned char p[4];
    encode_u32(p, v);
    put_bytes(b, p, sizeof(p));
}

static void put_u64(Buffer *b, uint64_t v)
{
    put_u32(b, (uint32_t)(v >> 32));
    put_u32(b, (uint32_t)v);
}

static void put_string(Buffer *b, const void *p, size_t n)
{
    if (n > SF_MAX_PACKET) {
        b->bad = 1;
        return;
    }
    put_u32(b, (uint32_t)n);
    put_bytes(b, p, n);
}

static void put_path(Buffer *b, const char *path)
{
    size_t n;
    if (!path) {
        b->bad = 1;
        return;
    }
    /* Bounded strlen also rejects path arguments too large for this client. */
    for (n = 0; n <= SF_MAX_PATH_BYTES && path[n]; ++n) { }
    if (n > SF_MAX_PATH_BYTES) {
        b->bad = 1;
        return;
    }
    put_string(b, path, n);
}

static void put_handle(Buffer *b, const SfHandle *handle)
{
    if (!handle || handle->len > SF_MAX_HANDLE) {
        b->bad = 1;
        return;
    }
    put_string(b, handle->data, handle->len);
}

static void put_attrs(Buffer *b, const SfAttrs *a)
{
    uint32_t flags = a ? a->flags : 0;
    const uint32_t known = SF_ATTR_SIZE | SF_ATTR_UIDGID |
                           SF_ATTR_PERMISSIONS | SF_ATTR_ACMODTIME;
    if (flags & ~known) {
        b->bad = 1;
        return;
    }
    put_u32(b, flags);
    if (flags & SF_ATTR_SIZE) put_u64(b, a->size);
    if (flags & SF_ATTR_UIDGID) {
        put_u32(b, a->uid);
        put_u32(b, a->gid);
    }
    if (flags & SF_ATTR_PERMISSIONS) put_u32(b, a->permissions);
    if (flags & SF_ATTR_ACMODTIME) {
        put_u32(b, a->atime);
        put_u32(b, a->mtime);
    }
}

static uint32_t get_u32(Buffer *b)
{
    uint32_t v;
    if (b->bad || b->pos > b->size || b->size - b->pos < 4u) {
        b->bad = 1;
        return 0;
    }
    v = decode_u32(b->data + b->pos);
    b->pos += 4u;
    return v;
}

static uint64_t get_u64(Buffer *b)
{
    uint64_t hi = get_u32(b);
    uint64_t lo = get_u32(b);
    return (hi << 32) | lo;
}

static const unsigned char *get_string(Buffer *b, size_t *size)
{
    uint32_t n = get_u32(b);
    const unsigned char *p;
    *size = 0;
    if (b->bad || b->pos > b->size || n > b->size - b->pos) {
        b->bad = 1;
        return NULL;
    }
    p = b->data + b->pos;
    b->pos += n;
    *size = n;
    return p;
}

static char *get_name(Buffer *b)
{
    size_t n;
    const unsigned char *p = get_string(b, &n);
    char *name;
    if (b->bad || n > SF_MAX_PATH_BYTES || (n && memchr(p, 0, n))) {
        b->bad = 1;
        return NULL;
    }
    name = (char *)malloc(n + 1u);
    if (!name) {
        b->bad = 1;
        return NULL;
    }
    if (n) memcpy(name, p, n);
    name[n] = 0;
    return name;
}

static void get_attrs(Buffer *b, SfAttrs *a)
{
    uint32_t flags, count, i;
    size_t ignored;
    const uint32_t known = SF_ATTR_SIZE | SF_ATTR_UIDGID |
                           SF_ATTR_PERMISSIONS | SF_ATTR_ACMODTIME |
                           SF_ATTR_EXTENDED;
    memset(a, 0, sizeof(*a));
    flags = get_u32(b);
    if (flags & ~known) {
        b->bad = 1;
        return;
    }
    a->flags = flags & ~SF_ATTR_EXTENDED;
    if (flags & SF_ATTR_SIZE) a->size = get_u64(b);
    if (flags & SF_ATTR_UIDGID) {
        a->uid = get_u32(b);
        a->gid = get_u32(b);
    }
    if (flags & SF_ATTR_PERMISSIONS) a->permissions = get_u32(b);
    if (flags & SF_ATTR_ACMODTIME) {
        a->atime = get_u32(b);
        a->mtime = get_u32(b);
    }
    if (flags & SF_ATTR_EXTENDED) {
        count = get_u32(b);
        if (b->bad || count > (b->size - b->pos) / 8u) {
            b->bad = 1;
            return;
        }
        for (i = 0; i < count && !b->bad; ++i) {
            (void)get_string(b, &ignored);
            (void)get_string(b, &ignored);
        }
    }
}

static int send_packet(SfClient *c, Buffer *b)
{
    unsigned char size[4];
    if (!c->alive)
        return error_text(c, SF_FX_NO_CONNECTION, "SSH connection is not available");
    if (b->bad || b->size == 0 || b->size > SF_MAX_PACKET)
        return error_text(c, SF_FX_FAILURE, "Invalid or oversized SFTP request (or insufficient memory)");
    encode_u32(size, (uint32_t)b->size);
    if (!c->io.write_all(c->io.ctx, size, sizeof(size)) ||
        !c->io.write_all(c->io.ctx, b->data, b->size))
        return transport_error(c, "SSH transport write failed or was cancelled");
    return SF_OK;
}

static int receive_packet(SfClient *c, Buffer *b)
{
    unsigned char size[4];
    uint32_t n;
    memset(b, 0, sizeof(*b));
    if (!c->io.read_exact(c->io.ctx, size, sizeof(size)))
        return transport_error(c, "SSH transport read failed or was cancelled");
    n = decode_u32(size);
    if (n == 0 || n > SF_MAX_PACKET)
        return protocol_error(c, "Invalid SFTP packet length");
    b->data = (unsigned char *)malloc(n);
    if (!b->data) {
        c->alive = 0;
        return error_text(c, SF_FX_FAILURE, "Insufficient memory for SFTP response");
    }
    b->size = n;
    b->capacity = n;
    if (!c->io.read_exact(c->io.ctx, b->data, n)) {
        release(b);
        return transport_error(c, "Truncated SFTP response or connection cancelled");
    }
    return SF_OK;
}

static uint32_t request_begin(SfClient *c, Buffer *b, unsigned char type)
{
    uint32_t id = c->next_id++;
    if (!c->next_id) c->next_id = 1;
    memset(b, 0, sizeof(*b));
    c->last_status = SF_FX_OK;
    c->error[0] = 0;
    put_u8(b, type);
    put_u32(b, id);
    return id;
}

static int exchange(SfClient *c, Buffer *request, uint32_t id, Buffer *response)
{
    int result = send_packet(c, request);
    release(request);
    memset(response, 0, sizeof(*response));
    if (result != SF_OK) return SF_ERR;
    if (receive_packet(c, response) != SF_OK) return SF_ERR;
    if (response->size < 5u) {
        release(response);
        return protocol_error(c, "SFTP response has no request identifier");
    }
    response->pos = 1;
    if (get_u32(response) != id) {
        release(response);
        return protocol_error(c, "SFTP response request identifier does not match");
    }
    return SF_OK;
}

static const char *status_name(uint32_t status)
{
    switch (status) {
    case SF_FX_OK: return "Success";
    case SF_FX_EOF: return "End of file";
    case SF_FX_NO_SUCH_FILE: return "No such file";
    case SF_FX_PERMISSION_DENIED: return "Permission denied";
    case SF_FX_FAILURE: return "Server operation failed";
    case SF_FX_BAD_MESSAGE: return "Server rejected malformed message";
    case SF_FX_NO_CONNECTION: return "No connection";
    case SF_FX_CONNECTION_LOST: return "Connection lost";
    case SF_FX_OP_UNSUPPORTED: return "Operation unsupported by server";
    default: return "Unknown SFTP status";
    }
}

static int read_status(SfClient *c, Buffer *b, int allow_ok, int allow_eof)
{
    uint32_t status = get_u32(b);
    size_t msg_size, language_size, i, used;
    const unsigned char *msg = get_string(b, &msg_size);
    (void)get_string(b, &language_size);
    if (b->bad || b->pos != b->size)
        return protocol_error(c, "Malformed SFTP status response");
    if ((status == SF_FX_OK && !allow_ok) ||
        (status == SF_FX_EOF && !allow_eof))
        return protocol_error(c, "Unexpected SFTP status for this operation");
    c->last_status = status;
    if (status == SF_FX_OK) {
        c->error[0] = 0;
        return SF_OK;
    }
    snprintf(c->error, sizeof(c->error), "%s (%lu)",
             status_name(status), (unsigned long)status);
    used = strlen(c->error);
    if (msg_size && used + 2u < sizeof(c->error)) {
        c->error[used++] = ':';
        c->error[used++] = ' ';
        for (i = 0; i < msg_size && used + 1u < sizeof(c->error); ++i) {
            unsigned char ch = msg[i];
            c->error[used++] = ch >= 32u && ch != 127u ? (char)ch : ' ';
        }
        c->error[used] = 0;
    }
    return status == SF_FX_EOF ? SF_EOF : SF_ERR;
}

static int expect_status(SfClient *c, Buffer *b)
{
    int result;
    if (b->data[0] != FXP_STATUS)
        result = protocol_error(c, "Expected SFTP status response");
    else
        result = read_status(c, b, 1, 0);
    release(b);
    return result;
}

static int unexpected_or_status(SfClient *c, Buffer *b, int allow_eof)
{
    int result = b->data[0] == FXP_STATUS ? read_status(c, b, 0, allow_eof) :
                 protocol_error(c, "Unexpected SFTP response type");
    release(b);
    return result;
}

static int expect_handle(SfClient *c, Buffer *b, SfHandle *handle)
{
    const unsigned char *data;
    size_t size;
    if (b->data[0] != FXP_HANDLE) return unexpected_or_status(c, b, 0);
    data = get_string(b, &size);
    if (b->bad || b->pos != b->size || size > SF_MAX_HANDLE) {
        release(b);
        return protocol_error(c, "Invalid SFTP handle");
    }
    handle->len = (uint32_t)size;
    if (size) memcpy(handle->data, data, size);
    release(b);
    return SF_OK;
}

int sf_init(SfClient *c, SfIO io)
{
    Buffer b = {0}, r;
    uint32_t version;
    if (!c) return SF_ERR;
    memset(c, 0, sizeof(*c));
    c->io = io;
    c->next_id = 1;
    if (!io.read_exact || !io.write_all)
        return error_text(c, SF_FX_NO_CONNECTION, "Missing SSH transport callbacks");
    c->alive = 1;
    put_u8(&b, FXP_INIT);
    put_u32(&b, 3);
    if (send_packet(c, &b) != SF_OK) {
        release(&b);
        return SF_ERR;
    }
    release(&b);
    if (receive_packet(c, &r) != SF_OK) return SF_ERR;
    if (r.size < 5u || r.data[0] != FXP_VERSION) {
        release(&r);
        return protocol_error(c, "Server did not return SFTP version");
    }
    r.pos = 1;
    version = get_u32(&r);
    if (version != 3u) {
        release(&r);
        c->alive = 0;
        return error_text(c, SF_FX_OP_UNSUPPORTED, "Server does not support SFTP version 3");
    }
    while (r.pos < r.size && !r.bad) {
        size_t name_size, value_size;
        const unsigned char *name = get_string(&r, &name_size);
        const unsigned char *value = get_string(&r, &value_size);
        static const char extension[] = "posix-rename@openssh.com";
        if (!r.bad && name_size == sizeof(extension) - 1u &&
            !memcmp(name, extension, sizeof(extension) - 1u) &&
            value_size == 1u && value[0] == '1')
            c->posix_rename = 1;
    }
    if (r.bad || r.pos != r.size) {
        release(&r);
        return protocol_error(c, "Malformed SFTP version extensions");
    }
    release(&r);
    c->version = 3;
    return SF_OK;
}

int sf_realpath(SfClient *c, const char *path, char **canonical_path)
{
    Buffer b, r;
    uint32_t id, count;
    SfAttrs ignored_attrs;
    size_t ignored_size;
    char *name;
    if (!canonical_path) return error_text(c, SF_FX_FAILURE, "Missing realpath output");
    *canonical_path = NULL;
    id = request_begin(c, &b, FXP_REALPATH);
    put_path(&b, path);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    if (r.data[0] != FXP_NAME) return unexpected_or_status(c, &r, 0);
    count = get_u32(&r);
    if (r.bad || count != 1u) {
        release(&r);
        return protocol_error(c, "SFTP realpath did not return one path");
    }
    name = get_name(&r);
    (void)get_string(&r, &ignored_size);
    get_attrs(&r, &ignored_attrs);
    if (r.bad || r.pos != r.size || !name || !name[0]) {
        free(name);
        release(&r);
        return protocol_error(c, "Malformed SFTP realpath response");
    }
    release(&r);
    *canonical_path = name;
    return SF_OK;
}

int sf_stat(SfClient *c, const char *path, int follow_links, SfAttrs *attrs)
{
    Buffer b, r;
    uint32_t id;
    SfAttrs result;
    if (!attrs) return error_text(c, SF_FX_FAILURE, "Missing stat output");
    memset(attrs, 0, sizeof(*attrs));
    id = request_begin(c, &b, follow_links ? FXP_STAT : FXP_LSTAT);
    put_path(&b, path);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    if (r.data[0] != FXP_ATTRS) return unexpected_or_status(c, &r, 0);
    get_attrs(&r, &result);
    if (r.bad || r.pos != r.size) {
        release(&r);
        return protocol_error(c, "Malformed SFTP attributes");
    }
    release(&r);
    *attrs = result;
    return SF_OK;
}

int sf_opendir(SfClient *c, const char *path, SfHandle *handle)
{
    Buffer b, r;
    uint32_t id;
    if (!handle) return error_text(c, SF_FX_FAILURE, "Missing directory handle output");
    memset(handle, 0, sizeof(*handle));
    id = request_begin(c, &b, FXP_OPENDIR);
    put_path(&b, path);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    return expect_handle(c, &r, handle);
}

void sf_entries_free(SfEntry *entries, size_t count)
{
    size_t i;
    if (!entries) return;
    for (i = 0; i < count; ++i) free(entries[i].name);
    free(entries);
}

int sf_readdir(SfClient *c, const SfHandle *handle, SfEntry **entries, size_t *count)
{
    Buffer b, r;
    SfEntry *result;
    uint32_t id, n, i;
    size_t ignored;
    if (!entries || !count) return error_text(c, SF_FX_FAILURE, "Missing directory output");
    *entries = NULL;
    *count = 0;
    id = request_begin(c, &b, FXP_READDIR);
    put_handle(&b, handle);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    if (r.data[0] != FXP_NAME) return unexpected_or_status(c, &r, 1);
    n = get_u32(&r);
    /* Each entry needs at least name length, longname length and attr flags.
     * Zero entries would make callers loop forever: EOF must be STATUS EOF. */
    /* The 1 MiB packet bound also bounds allocation to less than 4 MiB
     * on both supported (32/64-bit) targets, so n*sizeof(entry) cannot wrap. */
    if (r.bad || n == 0 || n > (r.size - r.pos) / 12u) {
        release(&r);
        return protocol_error(c, "Invalid SFTP directory entry count");
    }
    result = (SfEntry *)calloc(n, sizeof(*result));
    if (!result) {
        release(&r);
        return error_text(c, SF_FX_FAILURE, "Insufficient memory for directory entries");
    }
    for (i = 0; i < n && !r.bad; ++i) {
        result[i].name = get_name(&r);
        (void)get_string(&r, &ignored); /* Human-readable longname is not parsed. */
        get_attrs(&r, &result[i].attrs);
        if (!result[i].name || !result[i].name[0]) r.bad = 1;
    }
    if (r.bad || r.pos != r.size) {
        sf_entries_free(result, n);
        release(&r);
        return protocol_error(c, "Malformed SFTP directory entry");
    }
    release(&r);
    *entries = result;
    *count = n;
    return SF_OK;
}

int sf_open(SfClient *c, const char *path, uint32_t flags, const SfAttrs *attrs,
            SfHandle *handle)
{
    Buffer b, r;
    uint32_t id;
    if (!handle) return error_text(c, SF_FX_FAILURE, "Missing file handle output");
    memset(handle, 0, sizeof(*handle));
    if (flags & ~(SF_FXF_READ | SF_FXF_WRITE | SF_FXF_APPEND | SF_FXF_CREAT |
                  SF_FXF_TRUNC | SF_FXF_EXCL))
        return error_text(c, SF_FX_FAILURE, "Invalid SFTP open flags");
    id = request_begin(c, &b, FXP_OPEN);
    put_path(&b, path);
    put_u32(&b, flags);
    put_attrs(&b, attrs);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    return expect_handle(c, &r, handle);
}

int sf_close(SfClient *c, const SfHandle *handle)
{
    Buffer b, r;
    uint32_t id = request_begin(c, &b, FXP_CLOSE);
    put_handle(&b, handle);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    return expect_status(c, &r);
}

int sf_read(SfClient *c, const SfHandle *handle, uint64_t offset, void *buffer,
            size_t capacity, size_t *received)
{
    Buffer b, r;
    uint32_t id;
    const unsigned char *data;
    size_t size;
    if (received) *received = 0;
    if (!received || !buffer || capacity == 0 || capacity > SF_MAX_DATA)
        return error_text(c, SF_FX_FAILURE, "Invalid SFTP read size or buffer");
    id = request_begin(c, &b, FXP_READ);
    put_handle(&b, handle);
    put_u64(&b, offset);
    put_u32(&b, (uint32_t)capacity);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    if (r.data[0] != FXP_DATA) return unexpected_or_status(c, &r, 1);
    data = get_string(&r, &size);
    if (r.bad || r.pos != r.size || size == 0 || size > capacity) {
        release(&r);
        return protocol_error(c, "Invalid SFTP read response size");
    }
    memcpy(buffer, data, size);
    *received = size;
    release(&r);
    return SF_OK;
}

int sf_write(SfClient *c, const SfHandle *handle, uint64_t offset,
             const void *buffer, size_t size)
{
    Buffer b, r;
    uint32_t id;
    if ((!buffer && size) || size > SF_MAX_DATA)
        return error_text(c, SF_FX_FAILURE, "Invalid SFTP write size or buffer");
    id = request_begin(c, &b, FXP_WRITE);
    put_handle(&b, handle);
    put_u64(&b, offset);
    put_string(&b, buffer, size);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    return expect_status(c, &r);
}

static int path_status(SfClient *c, unsigned char type, const char *path,
                       const SfAttrs *attrs)
{
    Buffer b, r;
    uint32_t id = request_begin(c, &b, type);
    put_path(&b, path);
    if (type == FXP_SETSTAT || type == FXP_MKDIR) put_attrs(&b, attrs);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    return expect_status(c, &r);
}

int sf_setstat(SfClient *c, const char *path, const SfAttrs *attrs)
{
    if (!attrs) return error_text(c, SF_FX_FAILURE, "Missing SFTP attributes");
    return path_status(c, FXP_SETSTAT, path, attrs);
}

int sf_mkdir(SfClient *c, const char *path, const SfAttrs *attrs)
{
    return path_status(c, FXP_MKDIR, path, attrs);
}

int sf_remove(SfClient *c, const char *path)
{
    return path_status(c, FXP_REMOVE, path, NULL);
}

int sf_rmdir(SfClient *c, const char *path)
{
    return path_status(c, FXP_RMDIR, path, NULL);
}

int sf_rename(SfClient *c, const char *old_path, const char *new_path, int overwrite)
{
    static const char extension[] = "posix-rename@openssh.com";
    Buffer b, r;
    uint32_t id;
    if (overwrite && !c->posix_rename)
        return error_text(c, SF_FX_OP_UNSUPPORTED,
                          "Safe replacement requires posix-rename@openssh.com");
    id = request_begin(c, &b, overwrite ? FXP_EXTENDED : FXP_RENAME);
    if (overwrite) put_string(&b, extension, sizeof(extension) - 1u);
    put_path(&b, old_path);
    put_path(&b, new_path);
    if (exchange(c, &b, id, &r) != SF_OK) return SF_ERR;
    return expect_status(c, &r);
}
