#include "putty.h"
#include "misc.h"
#include "kittyng-quick-commands.h"
#include <assert.h>
void modalfatalbox(const char *fmt, ...) { abort(); }
static wchar_t *test_root;
int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    test_root = dup_mb_to_wc(CP_UTF8, argv[1]);
    HKEY test_key = NULL;
    if (argc == 3) {
        wchar_t *path = dup_mb_to_wc(CP_UTF8, argv[2]);
        assert(RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_ALL_ACCESS, &test_key) == ERROR_SUCCESS);
        sfree(path);
        assert(RegOverridePredefKey(HKEY_CURRENT_USER, test_key) == ERROR_SUCCESS);
        assert(SetEnvironmentVariableW(L"KITTY_NG_PORTABLE_DIR", NULL));
    } else assert(SetEnvironmentVariableW(L"KITTY_NG_PORTABLE_DIR", test_root));
    KrQuickCommands q;
    char *error = NULL;
    assert(kr_quick_load(&q, "Обновить Ubuntu", &error));
    assert(!strcmp(q.items[0].name, "Обновить Ubuntu"));
    assert(q.items[0].enter);
    kr_quick_free(&q);
    assert(kr_quick_update(0, "Сервер & тест", "printf 'Привет = мир' && echo ok", false, &error));
    assert(kr_quick_load(&q, "Update Ubuntu", &error));
    assert(!strcmp(q.items[0].name, "Сервер & тест"));
    assert(!strcmp(q.items[0].instruction, "printf 'Привет = мир' && echo ok"));
    assert(!q.items[0].enter); kr_quick_free(&q);
    for (int i=1; i<KR_QUICK_LIMIT; i++)
        assert(kr_quick_update(i, "Another command", "echo unchanged", true, &error));
    assert(!kr_quick_update(20, "Overflow", "echo no", true, &error)); sfree(error);
    assert(!kr_quick_update(1, "Bad\nname", "echo no", true, &error)); sfree(error);
    assert(!kr_quick_update(1, "Bad", "echo no\n[KiTTY]\nnew=bad", true, &error)); sfree(error);
    for (int i=0; i<KR_QUICK_LIMIT; i++) assert(kr_quick_update(i, NULL, NULL, false, &error));
    assert(kr_quick_load(&q, "Update Ubuntu", &error));
    for (int i=0; i<KR_QUICK_LIMIT; i++) assert(!q.items[i].name);
    kr_quick_free(&q); sfree(test_root);
    if (test_key) { RegOverridePredefKey(HKEY_CURRENT_USER, NULL); RegCloseKey(test_key); }
    puts("PASS quick commands: UTF-8, literal command, 20 slots, validation, no reseeding");
    return 0;
}
