/* Plugin-local build adapter; the client source remains unchanged. */
#include "windows/kittyng-portable.c"
/* Called with all connections closed when changing the profile root. */
int kg_engine_set_root(const wchar_t *root)
{
    if (filekeys) return 0;
    initialized = true;
    sfree(portable_root); sfree(sessions_directory); sfree(hostkeys_directory);
    sfree(seed_path); sfree(proxies_directory);
    portable_root = dupwcs(root);
    session_suffix[0] = hostkey_suffix[0] = 0;
    load_legacy_paths();
    return 1;
}

void kg_engine_storage_cleanup(void)
{
    sfree(portable_root); sfree(sessions_directory); sfree(hostkeys_directory);
    sfree(seed_path); sfree(proxies_directory);
    portable_root = sessions_directory = hostkeys_directory = seed_path = proxies_directory = NULL;
}
