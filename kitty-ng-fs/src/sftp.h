/* KiTTY-ng FS: bounded, synchronous SFTP version 3 client. */
#ifndef KITTYNGFS_SFTP_H
#define KITTYNGFS_SFTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF_MAX_PACKET (1024u * 1024u)
#define SF_MAX_DATA 32768u
#define SF_READ_WINDOW 32u
#define SF_READ_BATCH (SF_MAX_DATA * SF_READ_WINDOW)
#define SF_MAX_HANDLE 256u
#define SF_MAX_PATH_BYTES 32768u

#define SF_OK 0
#define SF_EOF 1
#define SF_ERR (-1)

#define SF_FX_OK 0u
#define SF_FX_EOF 1u
#define SF_FX_NO_SUCH_FILE 2u
#define SF_FX_PERMISSION_DENIED 3u
#define SF_FX_FAILURE 4u
#define SF_FX_BAD_MESSAGE 5u
#define SF_FX_NO_CONNECTION 6u
#define SF_FX_CONNECTION_LOST 7u
#define SF_FX_OP_UNSUPPORTED 8u

#define SF_ATTR_SIZE 0x00000001u
#define SF_ATTR_UIDGID 0x00000002u
#define SF_ATTR_PERMISSIONS 0x00000004u
#define SF_ATTR_ACMODTIME 0x00000008u
#define SF_ATTR_EXTENDED 0x80000000u

#define SF_FXF_READ 0x00000001u
#define SF_FXF_WRITE 0x00000002u
#define SF_FXF_APPEND 0x00000004u
#define SF_FXF_CREAT 0x00000008u
#define SF_FXF_TRUNC 0x00000010u
#define SF_FXF_EXCL 0x00000020u

/* Callbacks must transfer exactly size bytes and return nonzero on success.
 * They may impose timeouts and invoke the caller's cancellation mechanism.
 * A client must be accessed by only one thread at a time. */
typedef struct SfIO {
    void *ctx;
    int (*read_exact)(void *ctx, void *buffer, size_t size);
    int (*write_all)(void *ctx, const void *buffer, size_t size);
} SfIO;

typedef struct SfClient {
    SfIO io;
    uint32_t next_id;
    uint32_t last_status;
    int version;
    int alive;
    int posix_rename;
    char error[512];
} SfClient;

typedef struct SfHandle {
    uint32_t len;
    unsigned char data[SF_MAX_HANDLE];
} SfHandle;

typedef struct SfAttrs {
    uint32_t flags;
    uint64_t size;
    uint32_t uid;
    uint32_t gid;
    uint32_t permissions;
    uint32_t atime;
    uint32_t mtime;
} SfAttrs;

typedef struct SfEntry {
    char *name;
    SfAttrs attrs;
} SfEntry;

/* All operations return SF_OK or SF_ERR; only read/readdir may return SF_EOF.
 * On failure consult error and last_status. Invalid framing or lost transport
 * sets alive=0 and the transport must be re-created before further use.
 * sf_init initializes the whole client and performs the version handshake.
 * No client destructor is needed: the transport is owned by its caller. */
int sf_init(SfClient *client, SfIO io);
int sf_realpath(SfClient *client, const char *path, char **canonical_path);
int sf_stat(SfClient *client, const char *path, int follow_links, SfAttrs *attrs);
int sf_opendir(SfClient *client, const char *path, SfHandle *handle);
int sf_readdir(SfClient *client, const SfHandle *handle,
               SfEntry **entries, size_t *count);
void sf_entries_free(SfEntry *entries, size_t count);
int sf_open(SfClient *client, const char *path, uint32_t flags,
            const SfAttrs *attrs, SfHandle *handle);
int sf_close(SfClient *client, const SfHandle *handle);
int sf_read(SfClient *client, const SfHandle *handle, uint64_t offset,
            void *buffer, size_t capacity, size_t *received);
int sf_read_batch(SfClient *client, const SfHandle *handle, uint64_t offset,
                  void *buffer, size_t capacity, size_t *received);
int sf_write(SfClient *client, const SfHandle *handle, uint64_t offset,
             const void *buffer, size_t size);
int sf_setstat(SfClient *client, const char *path, const SfAttrs *attrs);
int sf_mkdir(SfClient *client, const char *path, const SfAttrs *attrs);
int sf_remove(SfClient *client, const char *path);
int sf_rmdir(SfClient *client, const char *path);
/* overwrite=0 uses standard SFTP RENAME, which must not replace an existing
 * target. overwrite=1 requires advertised posix-rename@openssh.com version 1.
 * No delete-before-rename fallback is performed. */
int sf_rename(SfClient *client, const char *old_path, const char *new_path,
              int overwrite);

/* canonical_path and entry name buffers use malloc. Release canonical_path
 * with free(), and directory results with sf_entries_free(). Path arguments
 * are raw UTF-8 bytes, never shell commands. */

#ifdef __cplusplus
}
#endif
#endif
