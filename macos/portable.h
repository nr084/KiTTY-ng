#ifndef KITTY_NG_MAC_PORTABLE_H
#define KITTY_NG_MAC_PORTABLE_H
/* Shared Windows/macOS portable root is the directory containing the .app. */
const char *kr_mac_root(void);
char *kr_mac_directory_at(const char *base,const char *key);
settings_r *kr_mac_copy_reader(settings_r *reader);
void kr_mac_override_read(settings_r *reader,const char *key,const char *value);
char *kr_mac_profile_path(const char *name);
bool kr_mac_write_succeeded(void);
int kr_mac_upgrade_profiles(void);
void kr_mac_set_root(const char *path);
char *kr_mac_ini(const char *section, const char *name, const char *fallback);
char *kr_mac_asset_path(const char *stored);
void kr_mac_load_conf(settings_r *reader, Conf *conf);
char *kr_mac_prepare_password(Conf *conf);
char *kr_mac_session_name_at(const char *base,const char *filename);
char *kr_mac_decode_name(const char *value);
settings_r *kr_mac_snapshot_settings(Conf *conf,void (*serialize)(settings_w*,Conf*));
char *kr_mac_settings_text(settings_r *reader);
#endif
