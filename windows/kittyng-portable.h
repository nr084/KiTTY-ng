#ifndef KITTYNG_PORTABLE_H
#define KITTYNG_PORTABLE_H
bool kr_portable(void);
const wchar_t *kr_portable_root(void);
const wchar_t *kr_portable_seedpath(void);
bool kr_file_key(HKEY key);
bool kr_plain_key(HKEY key);
HKEY kr_file_open(bool create, HKEY parent, const char *path);
void kr_file_close(HKEY key);
void kr_file_delete(HKEY key, const char *name);
char *kr_file_enum(HKEY key, int index);
char *kr_session_folder_enum(int index);
typedef struct kr_profile_update {
    unsigned updated, unchanged, failed;
    char *first_error; /* UTF-8 filename; caller frees it. */
} kr_profile_update;
kr_profile_update kr_update_portable_profiles(void);
char *kr_file_get(HKEY key, const char *name);
bool kr_file_put(HKEY key, const char *name, const char *value);
#endif
