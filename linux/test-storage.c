/* Storage-isolated Unix tests must not pull the production portable store. */
#include "putty.h"
#include "storage.h"
#include "portable.h"
const char *kr_mac_root(void) { return NULL; }
char *kr_mac_ini(const char *section, const char *key, const char *fallback) {
    return dupstr(fallback);
}
char *kr_mac_asset_path(const char *path) { return dupstr(path); }
void kr_mac_load_conf(settings_r *reader, Conf *conf) {}
char *kr_mac_prepare_password(Conf *conf) { return NULL; }
