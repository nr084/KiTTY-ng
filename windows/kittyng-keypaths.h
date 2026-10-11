#ifndef KITTYNG_KEYPATHS_H
#define KITTYNG_KEYPATHS_H
bool kr_key_path_setting(const char *name);
bool kr_portable_path_setting(const char *name);
char *kr_portable_save_key_path(const Filename *filename);
Filename *kr_portable_load_key_path(const char *stored);
#endif
