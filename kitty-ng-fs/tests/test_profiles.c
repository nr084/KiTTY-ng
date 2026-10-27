/* Windows integration tests for the real profiles.c implementation.
 * Uses synthetic profiles only; does not read the user's KiTTY directory. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/profiles.h"

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

static int checks, failures, skips;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s (Win32 error %lu)\n", __LINE__, #condition, \
            (unsigned long)GetLastError()); failures++; } } while (0)

static const char ssh_profile[] =
    "HostName\\example.invalid\\\r\n"
    "Protocol\\ssh\\\r\n"
    "PortNumber\\2222\\\r\n"
    "Password\\SYNTHETIC-IGNORED-NOT-A-REAL-PASSWORD\\\r\n";

static wchar_t *join(const wchar_t *a, const wchar_t *b)
{
    size_t n = wcslen(a) + wcslen(b) + 2;
    wchar_t *p = (wchar_t *)malloc(n * sizeof(*p));
    if (!p) { fprintf(stderr, "fixture out of memory\n"); exit(2); }
    swprintf(p, n, L"%ls\\%ls", a, b);
    return p;
}

static wchar_t *mkdir_child(const wchar_t *a, const wchar_t *b)
{
    wchar_t *p = join(a, b);
    CHECK(CreateDirectoryW(p, NULL));
    return p;
}

static int write_data(const wchar_t *path, const void *data, size_t length)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    int ok;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(h, data, (DWORD)length, &written, NULL) && written == length;
    CloseHandle(h);
    return ok;
}

static void fixture(const wchar_t *dir, const wchar_t *name, const char *text)
{
    wchar_t *path = join(dir, name);
    CHECK(write_data(path, text, strlen(text)));
    free(path);
}

static void remove_tree(const wchar_t *directory)
{
    WIN32_FIND_DATAW entry;
    wchar_t *pattern = join(directory, L"*");
    HANDLE find = FindFirstFileW(pattern, &entry);
    free(pattern);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            wchar_t *path;
            if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
            path = join(directory, entry.cFileName);
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(path);
                else remove_tree(path);
            } else DeleteFileW(path);
            free(path);
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    RemoveDirectoryW(directory);
}

static const KgProfile *lookup(const KgProfiles *profiles, const wchar_t *load_name)
{
    size_t i;
    for (i = 0; i < profiles->count; i++)
        if (!wcscmp(profiles->items[i].load_name, load_name)) return &profiles->items[i];
    return NULL;
}

static void test_default_layout(const wchar_t *base)
{
    wchar_t error[512], longfolder[201], longfile[61], *root, *sessions, *group, *deep;
    KgProfiles profiles = {0};
    const KgProfile *found;
    size_t i;
    root = mkdir_child(base, L"default");
    sessions = mkdir_child(root, L"Sessions");
    fixture(sessions, L"Alpha", ssh_profile);
    fixture(sessions, L"Default%20Settings", ssh_profile);
    fixture(sessions, L"Telnet", "HostName\\telnet.invalid\\\nProtocol\\telnet\\\n");
    fixture(sessions, L"NoHost", "Protocol\\ssh\\\nHostName\\\\\n");
    fixture(sessions, L"NoProtocol", "HostName\\example.invalid\\\n");
    fixture(sessions, L"BadPort", "Protocol\\ssh\\\nHostName\\example.invalid\\\nPortNumber\\65536\\\n");
    fixture(sessions, L"Bad%ZZ", ssh_profile);
    fixture(sessions, L"Bad%FF", ssh_profile);
    fixture(sessions, L"Bad%00", ssh_profile);
    fixture(sessions, L"Wrong%2Ftarget", ssh_profile);
    fixture(sessions, L"temporary.tmp", ssh_profile);
    fixture(sessions, L"Ciphertext", "whole-file encrypted profile is not parsed");
    fixture(sessions, L"Collision", ssh_profile);
    fixture(sessions, L"%43ollision", ssh_profile);
    fixture(sessions, L"%41Only", ssh_profile);
    fixture(sessions, L"Shadow Name", ssh_profile);
    fixture(sessions, L"Shadow%20Name", "HostName\\wrong-target.invalid\\\nProtocol\\telnet\\\n");
    fixture(sessions, L"Fallback Name", ssh_profile);
    fixture(sessions, L"%D0%90rchive", ssh_profile);
    fixture(sessions, L"%D0%B0rchive", ssh_profile);
    fixture(sessions, KG_SETTINGS_NAME, ssh_profile);
    fixture(sessions, L"weird%25%3F%3A%5C%2A%22%3C%3E%7C%01.%20", ssh_profile);
    group = mkdir_child(sessions, L"\x0413\x0440\x0443\x043F\x043F\x0430");
    fixture(group, L"%D0%A1%D0%B5%D1%80%D0%B2%D0%B5%D1%80%20one", ssh_profile);
    fixture(group, L"Default%20Settings", ssh_profile);
    free(group);
    group = mkdir_child(sessions, L"Encoded%20Folder");
    fixture(group, L"Same", ssh_profile);
    free(group);
    group = mkdir_child(sessions, L"Encoded Folder");
    fixture(group, L"Same", "HostName\\canonical.invalid\\\nProtocol\\ssh\\\n");
    free(group);

    for (i = 0; i < 200; i++) longfolder[i] = L'a';
    longfolder[200] = 0;
    for (i = 0; i < 60; i++) longfile[i] = L'b';
    longfile[60] = 0;
    group = mkdir_child(sessions, longfolder);
    fixture(group, longfile, ssh_profile); /* v1.0 accepts long combined paths. */
    free(group);

    deep = join(sessions, L"deep");
    CHECK(CreateDirectoryW(deep, NULL));
    for (i = 1; i < 17; i++) {
        wchar_t *child = mkdir_child(deep, L"d");
        free(deep);
        deep = child;
    }
    fixture(deep, L"TooDeep", ssh_profile);
    free(deep);

    CHECK(kg_profiles_load(root, &profiles, error, 512));
    CHECK(profiles.count == 8);
    {
        wchar_t longlogical[264];
        swprintf(longlogical, 264, L"%ls/%ls", longfolder, longfile);
        found = lookup(&profiles, longlogical);
        CHECK(found != NULL && wcslen(found->display_name) > 255);
    }
    CHECK(error[0] != 0); /* invalid entries produce a useful warning */
    CHECK(wcsstr(error, L"1 folders use unsupported %XX") != NULL);
    found = lookup(&profiles, L"Alpha");
    CHECK(found != NULL);
    if (found) {
        CHECK(!wcscmp(found->display_name, L"Alpha"));
        CHECK(!wcscmp(found->host, L"example.invalid"));
        CHECK(found->port == 2222);
    }
    found = lookup(&profiles, L"\x0413\x0440\x0443\x043F\x043F\x0430/\x0421\x0435\x0440\x0432\x0435\x0440 one");
    CHECK(found != NULL);
    if (found) CHECK(!wcscmp(found->display_name,
        L"\x0413\x0440\x0443\x043F\x043F\x0430%2F\x0421\x0435\x0440\x0432\x0435\x0440 one"));
    found = lookup(&profiles, L"weird%?:\\*\"<>|\x0001. ");
    CHECK(found != NULL);
    if (found) CHECK(!wcscmp(found->display_name,
        L"weird%25%3F%3A%5C%2A%22%3C%3E%7C%01%2E%20"));
    CHECK(lookup(&profiles, L"Collision") != NULL);
    CHECK(lookup(&profiles, L"AOnly") == NULL);
    CHECK(lookup(&profiles, L"Shadow Name") == NULL);
    CHECK(lookup(&profiles, L"Fallback Name") != NULL);
    CHECK(lookup(&profiles, L"\x0410rchive") == NULL);
    CHECK(lookup(&profiles, L"\x0430rchive") == NULL);
    found = lookup(&profiles, KG_SETTINGS_NAME);
    CHECK(found != NULL);
    if (found) CHECK(!wcsncmp(found->display_name, L"%5B", 3));
    found = lookup(&profiles, L"Encoded Folder/Same");
    CHECK(found != NULL);
    if (found) CHECK(!wcscmp(found->host, L"canonical.invalid"));
    kg_profiles_free(&profiles);
    CHECK(profiles.items == NULL && profiles.count == 0);

    {
        wchar_t *link = join(sessions, L"Link"), *target = join(sessions, L"Alpha");
        if (CreateSymbolicLinkW(link, target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) &&
            (GetFileAttributesW(link) & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CHECK(kg_profiles_load(root, &profiles, error, 512));
            CHECK(profiles.count == 8 && lookup(&profiles, L"Link") == NULL);
            kg_profiles_free(&profiles);
        } else {
            printf("SKIP: host does not provide Windows file symlinks.\n");
            skips++;
        }
        DeleteFileW(link);
        free(link); free(target);
    }
    {
        wchar_t *link = join(sessions, L"Loop");
        if (CreateSymbolicLinkW(link, sessions,
                SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) &&
            (GetFileAttributesW(link) & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CHECK(kg_profiles_load(root, &profiles, error, 512));
            CHECK(profiles.count == 8);
            kg_profiles_free(&profiles);
        } else {
            printf("SKIP: host does not provide Windows directory symlinks.\n");
            skips++;
        }
        RemoveDirectoryW(link);
        free(link);
    }
    free(sessions); free(root);
}

static void test_suffixes(const wchar_t *base)
{
    wchar_t error[512], *root = mkdir_child(base, L"suffix"), *dir;
    KgProfiles profiles = {0};
    fixture(root, L"kitty.ini", "[KiTTY]\r\nfileextension=.old\r\n");
    fixture(root, L"putty.conf", "; synthetic fixture\nSessions=Profiles\nsessionsuffix=.ktx\n");
    dir = mkdir_child(root, L"Profiles");
    fixture(dir, L"Custom.ktx", ssh_profile);
    fixture(dir, L"Wrong.old", ssh_profile);
    fixture(dir, L"Case.KTX", ssh_profile); /* KiTTY-ng suffix comparison is case sensitive */
    fixture(dir, L"Double.ktx.ktx", ssh_profile); /* -load must not silently drop one suffix */
    CHECK(kg_profiles_load(root, &profiles, error, 512));
    CHECK(profiles.count == 1 && lookup(&profiles, L"Custom") != NULL);
    kg_profiles_free(&profiles);
    CHECK(SetEnvironmentVariableW(L"KG_PROFILE_TEST_DIRECTORY", dir));
    fixture(root, L"putty.conf", "Sessions=%KG_PROFILE_TEST_DIRECTORY%\r\nsessionsuffix=.ktx\r\n");
    CHECK(kg_profiles_load(root, &profiles, error, 512));
    CHECK(profiles.count == 1 && lookup(&profiles, L"Custom") != NULL);
    kg_profiles_free(&profiles);
    CHECK(SetEnvironmentVariableW(L"KG_PROFILE_TEST_DIRECTORY", NULL));
    free(dir); free(root);

    root = mkdir_child(base, L"ini-only");
    dir = mkdir_child(root, L"Sessions");
    fixture(root, L"kitty.ini", "[KiTTY]\r\nfileextension=.session\r\n");
    fixture(dir, L"Ini.session", ssh_profile);
    fixture(dir, L"Ignored", ssh_profile);
    CHECK(kg_profiles_load(root, &profiles, error, 512));
    CHECK(profiles.count == 1 && lookup(&profiles, L"Ini") != NULL);
    kg_profiles_free(&profiles);
    free(dir); free(root);
}

static void test_pe_selection(const wchar_t *base)
{
    wchar_t error[512], self[32768], result[32768], *root, *cli, *gui;
    HANDLE h;
    IMAGE_DOS_HEADER dos;
    LARGE_INTEGER offset;
    DWORD got;
    WORD wrong_machine;
    KgProfiles profiles = {0};
    root = mkdir_child(base, L"tools");
#if defined(_WIN64)
    cli = join(root, L"klink-x64.exe");
    gui = join(root, L"kitty-ng-x64.exe");
    wrong_machine = IMAGE_FILE_MACHINE_I386;
#else
    cli = join(root, L"klink-x86.exe");
    gui = join(root, L"kitty-ng-x86.exe");
    wrong_machine = IMAGE_FILE_MACHINE_AMD64;
#endif
    CHECK(!kg_find_tool(root, 0, result, 32768));
    CHECK(GetModuleFileNameW(NULL, self, 32768) > 0);
    {
        wchar_t *unsuffixed = join(root, L"klink.exe");
        CHECK(CopyFileW(self, unsuffixed, FALSE));
        CHECK(!kg_find_tool(root, 0, result, 32768));
        free(unsuffixed);
    }
    CHECK(CopyFileW(self, cli, FALSE));
    CHECK(CopyFileW(self, gui, FALSE));
    CHECK(kg_find_tool(root, 0, result, 32768));
    CHECK(!wcscmp(result, cli));
    CHECK(kg_find_tool(root, 1, result, 32768));
    CHECK(!wcscmp(result, gui));
    CHECK(!kg_find_tool(root, 0, result, 2));
    h = CreateFileW(cli, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK(ReadFile(h, &dos, sizeof(dos), &got, NULL) && got == sizeof(dos));
        offset.QuadPart = dos.e_lfanew + 4;
        CHECK(SetFilePointerEx(h, offset, NULL, FILE_BEGIN));
        CHECK(WriteFile(h, &wrong_machine, sizeof(wrong_machine), &got, NULL) && got == sizeof(wrong_machine));
        CloseHandle(h);
        CHECK(!kg_find_tool(root, 0, result, 32768));
    }
    CHECK(write_data(cli, "MZ not an executable", 20));
    CHECK(!kg_find_tool(root, 0, result, 32768));
    h = CreateFileW(gui, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        CHECK(ReadFile(h, &dos, sizeof(dos), &got, NULL) && got == sizeof(dos));
        offset.QuadPart = dos.e_lfanew + 4;
        CHECK(SetFilePointerEx(h, offset, NULL, FILE_BEGIN));
        CHECK(WriteFile(h, &wrong_machine, sizeof(wrong_machine), &got, NULL) && got == sizeof(wrong_machine));
        CloseHandle(h);
        CHECK(!kg_find_tool(root, 1, result, 32768));
    }
    CHECK(!kg_profiles_load(L"Z:\\this-kg-test-path-does-not-exist", &profiles, error, 512));
    CHECK(error[0] != 0 && profiles.count == 0 && profiles.items == NULL);
    free(cli); free(gui); free(root);
}

int main(void)
{
    wchar_t temp[32768], normal[32768], *base;
    DWORD n = GetTempPathW(32768, temp);
    if (!n || n >= 32768 || !GetTempFileNameW(temp, L"kgp", 0, normal) ||
        !DeleteFileW(normal)) {
        fprintf(stderr, "Cannot create temporary fixture root.\n");
        return 2;
    }
    /* Extended path permits the intentionally overlength-label fixture even
     * on Windows without a process-level longPathAware manifest. */
    base = (wchar_t *)malloc((wcslen(normal) + 5) * sizeof(*base));
    if (!base) return 2;
    swprintf(base, wcslen(normal) + 5, L"\\\\?\\%ls", normal);
    if (!CreateDirectoryW(base, NULL)) { free(base); return 2; }
    test_default_layout(base);
    test_suffixes(base);
    test_pe_selection(base);
    remove_tree(base);
    free(base);
    printf("Profiles: %d checks, %d failures, %d skipped capability tests.\n",
           checks, failures, skips);
    return failures ? 1 : 0;
}
