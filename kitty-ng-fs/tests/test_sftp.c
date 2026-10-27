/* Deterministic protocol tests. No server, credentials or network required.
 * gcc -std=c99 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
 *     -g src/sftp.c tests/test_sftp.c -o test_sftp && ./test_sftp
 */
#include "../src/sftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Bytes { unsigned char *data; size_t size; } Bytes;
typedef struct Fake {
    Bytes input, output;
    size_t cursor;
    int fail_read, fail_write;
} Fake;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)
static int tests_run;

static void append(Bytes *b, const void *p, size_t n)
{
    unsigned char *q;
    if (!n) return;
    q = (unsigned char *)realloc(b->data, b->size + n);
    CHECK(q != NULL);
    b->data = q;
    memcpy(b->data + b->size, p, n);
    b->size += n;
}
static void byte(Bytes *b, unsigned char v) { append(b, &v, 1); }
static void u32(Bytes *b, uint32_t v)
{
    unsigned char p[4];
    p[0] = (unsigned char)(v / 16777216u);
    p[1] = (unsigned char)(v / 65536u);
    p[2] = (unsigned char)(v / 256u);
    p[3] = (unsigned char)v;
    append(b, p, 4);
}
static void u64(Bytes *b, uint64_t v)
{
    u32(b, (uint32_t)(v / UINT64_C(4294967296)));
    u32(b, (uint32_t)v);
}
static uint32_t at32(const unsigned char *p)
{
    return (uint32_t)p[0] * 16777216u + (uint32_t)p[1] * 65536u +
           (uint32_t)p[2] * 256u + (uint32_t)p[3];
}
static void string_n(Bytes *b, const void *p, size_t n)
{
    u32(b, (uint32_t)n); append(b, p, n);
}
static void string(Bytes *b, const char *s) { string_n(b, s, strlen(s)); }
static void packet(Fake *f, Bytes *payload)
{
    u32(&f->input, (uint32_t)payload->size);
    append(&f->input, payload->data, payload->size);
    free(payload->data);
    memset(payload, 0, sizeof(*payload));
}
static Bytes reply(unsigned char type, uint32_t id)
{
    Bytes b = {0}; byte(&b, type); u32(&b, id); return b;
}
static void status(Fake *f, uint32_t id, uint32_t code, const char *message)
{
    Bytes p = reply(101, id);
    u32(&p, code); string(&p, message); string(&p, "en"); packet(f, &p);
}
static void handle_reply(Fake *f, uint32_t id, const void *h, size_t n)
{
    Bytes p = reply(102, id); string_n(&p, h, n); packet(f, &p);
}
static int fake_read(void *ctx, void *buffer, size_t size)
{
    Fake *f = (Fake *)ctx;
    if (f->fail_read || size > f->input.size - f->cursor) return 0;
    if (size) memcpy(buffer, f->input.data + f->cursor, size);
    f->cursor += size;
    return 1;
}
static int fake_write(void *ctx, const void *buffer, size_t size)
{
    Fake *f = (Fake *)ctx;
    if (f->fail_write) return 0;
    append(&f->output, buffer, size);
    return 1;
}
static SfIO io(Fake *f)
{
    SfIO result;
    result.ctx = f; result.read_exact = fake_read; result.write_all = fake_write;
    return result;
}
static void version(Fake *f, int ext)
{
    Bytes p = {0}; byte(&p, 2); u32(&p, 3);
    if (ext) {
        string(&p, "unrelated@example.org"); string(&p, "anything");
        string(&p, "posix-rename@openssh.com"); string(&p, "1");
    }
    packet(f, &p);
}
static void start(Fake *f, SfClient *c, int ext)
{
    memset(f, 0, sizeof(*f)); version(f, ext);
    CHECK(sf_init(c, io(f)) == SF_OK);
    CHECK(c->alive && c->version == 3);
}
static void finish(Fake *f)
{
    free(f->input.data); free(f->output.data); memset(f, 0, sizeof(*f));
}
static const unsigned char *request(Fake *f, size_t index, size_t *length)
{
    size_t pos = 0, i;
    for (i = 0; ; ++i) {
        uint32_t n;
        CHECK(pos + 4 <= f->output.size);
        n = at32(f->output.data + pos);
        CHECK(n <= f->output.size - pos - 4);
        if (i == index) { *length = n; return f->output.data + pos + 4; }
        pos += 4u + n;
    }
}

static void test_handshake(void)
{
    static const unsigned char expected[] = {0,0,0,5,1,0,0,0,3};
    Fake f; SfClient c;
    start(&f, &c, 1);
    CHECK(c.next_id == 1 && c.posix_rename == 1);
    CHECK(f.output.size == sizeof(expected));
    CHECK(!memcmp(f.output.data, expected, sizeof(expected)));
    CHECK(f.cursor == f.input.size);
    finish(&f);
}
static void test_open_read_write_close(void)
{
    Fake f; SfClient c; SfHandle h; Bytes p;
    size_t n, received; const unsigned char *q; unsigned char output[8];
    const unsigned char handle[] = {0,1,255,2};
    const unsigned char content[] = {0,255,128,'a','\n'};
    const uint64_t offset = UINT64_C(0x0102030405060708);
    start(&f, &c, 0);
    handle_reply(&f, 1, handle, sizeof(handle));
    CHECK(sf_open(&c, "/file 'with' $shell & punctuation", SF_FXF_READ | SF_FXF_WRITE,
                  NULL, &h) == SF_OK);
    CHECK(h.len == sizeof(handle) && !memcmp(h.data, handle, sizeof(handle)));
    q = request(&f, 1, &n);
    CHECK(q[0] == 3 && at32(q + 1) == 1);
    CHECK(at32(q + 5) == strlen("/file 'with' $shell & punctuation"));
    CHECK(at32(q + n - 8) == (SF_FXF_READ | SF_FXF_WRITE));
    CHECK(at32(q + n - 4) == 0);
    p = reply(103, 2); string_n(&p, content, sizeof(content)); packet(&f, &p);
    CHECK(sf_read(&c, &h, offset, output, sizeof(output), &received) == SF_OK);
    CHECK(received == sizeof(content) && !memcmp(content, output, received));
    q = request(&f, 2, &n);
    CHECK(q[0] == 5 && at32(q + 1) == 2 && at32(q + 5) == sizeof(handle));
    CHECK(!memcmp(q + 9, handle, sizeof(handle)));
    CHECK(!memcmp(q + 9 + sizeof(handle), "\x01\x02\x03\x04\x05\x06\x07\x08", 8));
    CHECK(at32(q + n - 4) == sizeof(output));
    status(&f, 3, SF_FX_OK, "");
    CHECK(sf_write(&c, &h, offset, content, sizeof(content)) == SF_OK);
    q = request(&f, 3, &n);
    CHECK(q[0] == 6 && at32(q + 1) == 3);
    CHECK(!memcmp(q + 9 + sizeof(handle), "\x01\x02\x03\x04\x05\x06\x07\x08", 8));
    CHECK(at32(q + n - sizeof(content) - 4) == sizeof(content));
    CHECK(!memcmp(q + n - sizeof(content), content, sizeof(content)));
    status(&f, 4, SF_FX_OK, "");
    CHECK(sf_close(&c, &h) == SF_OK);
    CHECK(c.alive && f.cursor == f.input.size);
    finish(&f);
}
static void test_stat_and_attributes(void)
{
    Fake f; SfClient c; SfAttrs a; Bytes p; size_t n; const unsigned char *q;
    start(&f, &c, 0);
    p = reply(105, 1); u32(&p, 15u | SF_ATTR_EXTENDED);
    u64(&p, UINT64_C(0x100000007)); u32(&p, 1001); u32(&p, 2002);
    u32(&p, 0100640u); u32(&p, 1700000000u); u32(&p, 1800000000u);
    u32(&p, 1); string(&p, "extended-attribute");
    string_n(&p, "\0\xff\x01", 3); packet(&f, &p);
    CHECK(sf_stat(&c, "/large", 1, &a) == SF_OK);
    CHECK(a.flags == 15u && a.size == UINT64_C(0x100000007));
    CHECK(a.uid == 1001 && a.gid == 2002 && a.permissions == 0100640u);
    CHECK(a.atime == 1700000000u && a.mtime == 1800000000u);
    q = request(&f, 1, &n); CHECK(q[0] == 17 && n == 15);
    p = reply(105, 2); u32(&p, SF_ATTR_PERMISSIONS); u32(&p, 0120777u); packet(&f, &p);
    CHECK(sf_stat(&c, "/link", 0, &a) == SF_OK);
    q = request(&f, 2, &n); CHECK(q[0] == 7 && a.permissions == 0120777u);
    finish(&f);
}
static void test_realpath(void)
{
    Fake f; SfClient c; Bytes p; char *path = NULL;
    start(&f, &c, 0); p = reply(104, 1); u32(&p, 1);
    string(&p, "/home/user space/\xd1\x82\xd0\xb5\xd1\x81\xd1\x82");
    string(&p, "longname ignored"); u32(&p, 0); packet(&f, &p);
    CHECK(sf_realpath(&c, ".", &path) == SF_OK);
    CHECK(!strcmp(path, "/home/user space/\xd1\x82\xd0\xb5\xd1\x81\xd1\x82"));
    free(path); finish(&f);
}
static void test_directory(void)
{
    Fake f; SfClient c; SfHandle h; SfEntry *entries; size_t count; Bytes p;
    start(&f, &c, 0);
    handle_reply(&f, 1, "dir", 3); CHECK(sf_opendir(&c, "/dir", &h) == SF_OK);
    p = reply(104, 2); u32(&p, 2);
    string(&p, "."); string(&p, "ignored");
    u32(&p, SF_ATTR_PERMISSIONS); u32(&p, 0040755u);
    string(&p, "file \xd1\x91 ' &.txt"); string(&p, "irrelevant -rwx------ text");
    u32(&p, SF_ATTR_SIZE | SF_ATTR_PERMISSIONS | SF_ATTR_ACMODTIME);
    u64(&p, UINT64_C(0x200000003)); u32(&p, 0100644u); u32(&p, 11); u32(&p, 12);
    packet(&f, &p);
    CHECK(sf_readdir(&c, &h, &entries, &count) == SF_OK && count == 2);
    CHECK(!strcmp(entries[0].name, ".") && entries[0].attrs.permissions == 0040755u);
    CHECK(!strcmp(entries[1].name, "file \xd1\x91 ' &.txt"));
    CHECK(entries[1].attrs.size == UINT64_C(0x200000003));
    CHECK(entries[1].attrs.mtime == 12);
    sf_entries_free(entries, count);
    status(&f, 3, SF_FX_EOF, "Finished");
    CHECK(sf_readdir(&c, &h, &entries, &count) == SF_EOF);
    CHECK(!entries && count == 0 && c.alive && c.last_status == SF_FX_EOF);
    finish(&f);
}
static void test_mutations(void)
{
    Fake f; SfClient c; SfAttrs a; const unsigned char *q; size_t n;
    start(&f, &c, 1); memset(&a, 0, sizeof(a));
    a.flags = SF_ATTR_PERMISSIONS | SF_ATTR_ACMODTIME;
    a.permissions = 0600; a.atime = 1; a.mtime = UINT32_MAX;
    status(&f, 1, SF_FX_OK, ""); CHECK(sf_setstat(&c, "/a", &a) == SF_OK);
    q = request(&f, 1, &n);
    CHECK(q[0] == 9 && at32(q + n - 16) == a.flags);
    CHECK(at32(q + n - 12) == 0600 && at32(q + n - 4) == UINT32_MAX);
    status(&f, 2, SF_FX_OK, ""); CHECK(sf_mkdir(&c, "/b", NULL) == SF_OK);
    q = request(&f, 2, &n); CHECK(q[0] == 14 && at32(q + n - 4) == 0);
    status(&f, 3, SF_FX_OK, ""); CHECK(sf_remove(&c, "/c") == SF_OK);
    q = request(&f, 3, &n); CHECK(q[0] == 13);
    status(&f, 4, SF_FX_OK, ""); CHECK(sf_rmdir(&c, "/d") == SF_OK);
    q = request(&f, 4, &n); CHECK(q[0] == 15);
    status(&f, 5, SF_FX_OK, ""); CHECK(sf_rename(&c, "/e", "/f", 0) == SF_OK);
    q = request(&f, 5, &n); CHECK(q[0] == 18 && at32(q + 5) == 2);
    status(&f, 6, SF_FX_OK, ""); CHECK(sf_rename(&c, "/e", "/f", 1) == SF_OK);
    q = request(&f, 6, &n);
    CHECK(q[0] == 200 && at32(q + 5) == strlen("posix-rename@openssh.com"));
    CHECK(!memcmp(q + 9, "posix-rename@openssh.com", strlen("posix-rename@openssh.com")));
    finish(&f);
}
static void test_no_destructive_rename_fallback(void)
{
    Fake f; SfClient c; size_t sent;
    start(&f, &c, 0); sent = f.output.size;
    CHECK(sf_rename(&c, "/source", "/target", 1) == SF_ERR);
    CHECK(c.last_status == SF_FX_OP_UNSUPPORTED && c.alive);
    CHECK(sent == f.output.size); finish(&f);
}
static void test_server_error_keeps_connection(void)
{
    Fake f; SfClient c; SfAttrs a;
    start(&f, &c, 0);
    status(&f, 1, SF_FX_PERMISSION_DENIED, "No\npermission\r\033");
    CHECK(sf_stat(&c, "/private", 1, &a) == SF_ERR);
    CHECK(c.alive && c.last_status == SF_FX_PERMISSION_DENIED);
    CHECK(strstr(c.error, "Permission denied") && !strchr(c.error, '\n'));
    CHECK(!strchr(c.error, '\033'));
    status(&f, 2, SF_FX_OK, "");
    CHECK(sf_remove(&c, "/allowed") == SF_OK);
    CHECK(c.error[0] == 0 && c.last_status == SF_FX_OK); finish(&f);
}
static void test_read_eof(void)
{
    Fake f; SfClient c; SfHandle h = {1, {9}};
    unsigned char data[32]; size_t received = 99;
    start(&f, &c, 0); status(&f, 1, SF_FX_EOF, "");
    CHECK(sf_read(&c, &h, 999, data, sizeof(data), &received) == SF_EOF);
    CHECK(received == 0 && c.alive); finish(&f);
}
static void test_wrong_id_poisoning(void)
{
    Fake f; SfClient c; SfAttrs a; Bytes p; size_t sent;
    start(&f, &c, 0); p = reply(105, 999); u32(&p, 0); packet(&f, &p);
    CHECK(sf_stat(&c, "/a", 1, &a) == SF_ERR);
    CHECK(!c.alive && c.last_status == SF_FX_BAD_MESSAGE);
    sent = f.output.size;
    CHECK(sf_remove(&c, "/b") == SF_ERR && f.output.size == sent); finish(&f);
}
static void test_bad_packet_lengths(void)
{
    uint32_t lengths[] = {0, SF_MAX_PACKET + 1u, UINT32_MAX}; size_t i;
    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        Fake f; SfClient c; SfAttrs a; start(&f, &c, 0);
        u32(&f.input, lengths[i]);
        CHECK(sf_stat(&c, "/a", 1, &a) == SF_ERR);
        CHECK(!c.alive && c.last_status == SF_FX_BAD_MESSAGE);
        CHECK(f.cursor == f.input.size); finish(&f);
    }
}
static void test_truncated_response(void)
{
    Fake f; SfClient c; SfAttrs a; start(&f, &c, 0);
    u32(&f.input, 20); byte(&f.input, 105);
    CHECK(sf_stat(&c, "/a", 1, &a) == SF_ERR);
    CHECK(!c.alive && c.last_status == SF_FX_CONNECTION_LOST); finish(&f);
}
static void test_malformed_names(void)
{
    int variant;
    for (variant = 0; variant < 4; ++variant) {
        Fake f; SfClient c; SfHandle h = {1,{0}};
        SfEntry *entries = NULL; size_t count = 99; Bytes p;
        start(&f, &c, 0); p = reply(104, 1);
        if (variant == 0) u32(&p, UINT32_MAX);
        else if (variant == 1) {
            u32(&p, 1); string_n(&p, "a\0b", 3); string(&p, ""); u32(&p, 0);
        } else if (variant == 2) {
            u32(&p, 1); string(&p, "a"); string(&p, ""); u32(&p, 0x40);
        } else {
            u32(&p, 1); string(&p, "a"); string(&p, ""); u32(&p, SF_ATTR_SIZE);
            u32(&p, 99); /* Missing half of 64-bit size. */
        }
        packet(&f, &p);
        CHECK(sf_readdir(&c, &h, &entries, &count) == SF_ERR);
        CHECK(!entries && count == 0 && !c.alive && c.last_status == SF_FX_BAD_MESSAGE);
        finish(&f);
    }
}
static void test_oversized_handle_and_data(void)
{
    Fake f; SfClient c; SfHandle h; Bytes p;
    unsigned char huge_handle[SF_MAX_HANDLE + 1u], output[2]; size_t received;
    memset(huge_handle, 0xaa, sizeof(huge_handle)); start(&f, &c, 0);
    handle_reply(&f, 1, huge_handle, sizeof(huge_handle));
    CHECK(sf_opendir(&c, "/a", &h) == SF_ERR);
    CHECK(!c.alive && c.last_status == SF_FX_BAD_MESSAGE); finish(&f);
    start(&f, &c, 0); h.len = 1; h.data[0] = 0;
    p = reply(103, 1); string(&p, "three"); packet(&f, &p);
    CHECK(sf_read(&c, &h, 0, output, sizeof(output), &received) == SF_ERR);
    CHECK(!c.alive && received == 0); finish(&f);
}
static void test_unexpected_status_and_trailing_bytes(void)
{
    Fake f; SfClient c; SfAttrs a; Bytes p;
    start(&f, &c, 0);
    status(&f, 1, SF_FX_OK, ""); /* STAT requires ATTRS, not OK without data. */
    CHECK(sf_stat(&c, "/a", 1, &a) == SF_ERR && !c.alive); finish(&f);
    start(&f, &c, 0); p = reply(105, 1); u32(&p, 0); byte(&p, 0); packet(&f, &p);
    CHECK(sf_stat(&c, "/a", 1, &a) == SF_ERR && !c.alive); finish(&f);
}
static void test_invalid_local_inputs_do_not_send(void)
{
    Fake f; SfClient c; SfHandle h = {1,{0}}; SfAttrs a = {0};
    unsigned char data[2]; size_t before, received;
    char *oversized = (char *)malloc(SF_MAX_PATH_BYTES + 2u);
    CHECK(oversized); memset(oversized, 'x', SF_MAX_PATH_BYTES + 1u);
    oversized[SF_MAX_PATH_BYTES + 1u] = 0;
    start(&f, &c, 0); before = f.output.size;
    CHECK(sf_read(&c, &h, 0, data, 0, &received) == SF_ERR);
    CHECK(sf_read(&c, &h, 0, data, SF_MAX_DATA + 1u, &received) == SF_ERR);
    CHECK(sf_write(&c, &h, 0, data, SF_MAX_DATA + 1u) == SF_ERR);
    CHECK(sf_remove(&c, oversized) == SF_ERR);
    a.flags = SF_ATTR_EXTENDED; CHECK(sf_setstat(&c, "/a", &a) == SF_ERR);
    CHECK(f.output.size == before && c.alive); free(oversized); finish(&f);
}
static void test_transport_failures(void)
{
    Fake f; SfClient c; start(&f, &c, 0); f.fail_write = 1;
    CHECK(sf_remove(&c, "/a") == SF_ERR);
    CHECK(!c.alive && c.last_status == SF_FX_CONNECTION_LOST); finish(&f);
    start(&f, &c, 0); f.fail_read = 1;
    CHECK(sf_remove(&c, "/a") == SF_ERR);
    CHECK(!c.alive && c.last_status == SF_FX_CONNECTION_LOST); finish(&f);
}
static void test_version_rejections(void)
{
    Fake f = {0}; SfClient c; Bytes p = {0};
    byte(&p, 2); u32(&p, 4); packet(&f, &p);
    CHECK(sf_init(&c, io(&f)) == SF_ERR && !c.alive);
    CHECK(c.last_status == SF_FX_OP_UNSUPPORTED); finish(&f);
    byte(&p, 2); u32(&p, 3); string(&p, "extension-without-value"); packet(&f, &p);
    CHECK(sf_init(&c, io(&f)) == SF_ERR && !c.alive);
    CHECK(c.last_status == SF_FX_BAD_MESSAGE); finish(&f);
}
static uint32_t random_state = UINT32_C(0x67aa0813);
static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void test_bounded_malformed_corpus(void)
{
    unsigned int i;
    /* Deterministic hostile responses exercise real parsers. ASan/UBSan
     * expose memory and integer mistakes without contacting any server. */
    for (i = 0; i < 2000u; ++i) {
        Fake f; SfClient c; SfAttrs attrs; SfHandle h = {1,{0}};
        SfEntry *entries = NULL; size_t count = 0, j, n = next_random() % 128u;
        Bytes p; start(&f, &c, 0);
        p = reply((unsigned char)(i % 2u ? 105 : 104), 1);
        for (j = 0; j < n; ++j) byte(&p, (unsigned char)next_random());
        packet(&f, &p);
        if (i % 2u) (void)sf_stat(&c, "/fuzz", 1, &attrs);
        else (void)sf_readdir(&c, &h, &entries, &count);
        sf_entries_free(entries, count); finish(&f);
    }
}

static void data_reply(Fake *f, uint32_t id, unsigned char value, size_t n)
{
    Bytes b = reply(103, id); unsigned char *data = malloc(n ? n : 1);
    CHECK(data); memset(data, value, n); string_n(&b, data, n); free(data); packet(f, &b);
}
static void test_batch_reads(void)
{
    Fake f; SfClient c; SfHandle h = {1,{0}}; size_t got, n;
    unsigned char *data = malloc(SF_READ_BATCH); CHECK(data);
    start(&f, &c, 0);
    /* Reverse reply order, >4GiB offsets, all requests sent before receiving. */
    for (unsigned i=SF_READ_WINDOW; i; --i) data_reply(&f, i, (unsigned char)i, SF_MAX_DATA);
    CHECK(sf_read_batch(&c, &h, UINT64_C(0x100000007), data, SF_READ_BATCH, &got)==SF_OK);
    CHECK(got==SF_READ_BATCH);
    for (unsigned i=0;i<SF_READ_WINDOW;++i) CHECK(data[i*SF_MAX_DATA]==i+1);
    const unsigned char *r=request(&f,1,&n);
    CHECK(r[0]==5 && at32(r+10)==1 && at32(r+14)==7);
    finish(&f);
    start(&f,&c,0);data_reply(&f,2,'B',SF_MAX_DATA);data_reply(&f,1,'A',17);
    CHECK(sf_read_batch(&c,&h,0,data,2*SF_MAX_DATA,&got)==SF_OK && got==17);
    CHECK(f.cursor==f.input.size && c.alive); /* surplus data drained, no skipped hole */
    status(&f,3,SF_FX_EOF,"");
    CHECK(sf_read_batch(&c,&h,17,data,1,&got)==SF_EOF && !got);finish(&f);
    start(&f,&c,0);status(&f,2,SF_FX_EOF,"");data_reply(&f,1,'A',11);
    CHECK(sf_read_batch(&c,&h,0,data,2*SF_MAX_DATA,&got)==SF_OK && got==11);finish(&f);
    start(&f,&c,0);data_reply(&f,1,'A',1);data_reply(&f,1,'A',1);
    CHECK(sf_read_batch(&c,&h,0,data,2*SF_MAX_DATA,&got)==SF_ERR && !c.alive);finish(&f);
    start(&f,&c,0);data_reply(&f,100,'A',1);
    CHECK(sf_read_batch(&c,&h,0,data,1,&got)==SF_ERR && !c.alive);finish(&f);
    start(&f,&c,0);data_reply(&f,1,'A',2);
    CHECK(sf_read_batch(&c,&h,0,data,1,&got)==SF_ERR && !c.alive);finish(&f);
    start(&f,&c,0);status(&f,1,SF_FX_PERMISSION_DENIED,"denied");
    CHECK(sf_read_batch(&c,&h,0,data,2*SF_MAX_DATA,&got)==SF_ERR && !c.alive);finish(&f);
    start(&f,&c,0);c.next_id=UINT32_MAX;data_reply(&f,1,'B',1);data_reply(&f,UINT32_MAX,'A',SF_MAX_DATA);
    CHECK(sf_read_batch(&c,&h,0,data,SF_MAX_DATA+1,&got)==SF_OK && got==SF_MAX_DATA+1);finish(&f);
    start(&f,&c,0);size_t before=f.output.size;
    CHECK(sf_read_batch(&c,&h,UINT64_MAX-1,data,2,&got)==SF_ERR && c.alive && before==f.output.size);finish(&f);
    free(data);
}
#define RUN(test) do { test(); ++tests_run; } while (0)
int main(void)
{
    RUN(test_batch_reads);
    RUN(test_handshake);
    RUN(test_open_read_write_close);
    RUN(test_stat_and_attributes);
    RUN(test_realpath);
    RUN(test_directory);
    RUN(test_mutations);
    RUN(test_no_destructive_rename_fallback);
    RUN(test_server_error_keeps_connection);
    RUN(test_read_eof);
    RUN(test_wrong_id_poisoning);
    RUN(test_bad_packet_lengths);
    RUN(test_truncated_response);
    RUN(test_malformed_names);
    RUN(test_oversized_handle_and_data);
    RUN(test_unexpected_status_and_trailing_bytes);
    RUN(test_invalid_local_inputs_do_not_send);
    RUN(test_transport_failures);
    RUN(test_version_rejections);
    RUN(test_bounded_malformed_corpus);
    printf("SFTP: %d test groups passed (including 2000 malformed responses).\n", tests_run);
    return 0;
}
