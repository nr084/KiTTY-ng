/* Tests real Windows APIs and pathutil.c. No filesystem contents are modified. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/pathutil.h"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s (Windows error %lu)\n", __LINE__, #expr, \
            (unsigned long)GetLastError()); ++failures; \
} } while (0)

static void expected_display(const char *raw, const wchar_t *expected)
{
    wchar_t displayed[260];
    int ok = kg_display_name(raw, displayed, 260);
    CHECK(ok);
    if (ok) CHECK(!wcscmp(displayed, expected));
}

static void roundtrip(const char *raw)
{
    wchar_t displayed[260];
    char *path;
    size_t n = strlen(raw);
    int ok = kg_display_name(raw, displayed, 260);
    CHECK(ok);
    if (!ok) return;
    path = kg_remote_path("/test", displayed);
    CHECK(path != NULL);
    if (path) CHECK(strlen(path) == n + 6 && !memcmp(path, "/test/", 6) &&
                    !memcmp(path + 6, raw, n));
    free(path);
}

static void rejected_remote(const wchar_t *name)
{
    char *path = kg_remote_path("/base", name);
    CHECK(path == NULL);
    free(path);
}

static void expected_remote(const char *base, const wchar_t *name, const char *expected)
{
    char *path = kg_remote_path(base, name);
    CHECK(path != NULL);
    if (path) CHECK(!strcmp(path, expected));
    free(path);
}

static void test_literal_names_and_unicode(void)
{
    const char *normal[] = {
        "ordinary.txt", "100%.txt", "100%20.txt", "literal%25",
        "%2F", "%00", "%2E%2E", "a b.txt", ".hidden", "COM0", "LPT0",
        "~KG~41", "name; 'single' & $shell ^ !.txt",
        "\xd0\xa2\xd0\xb5\xd1\x81\xd1\x82.txt",
        "emoji-\xf0\x9f\x98\x80.txt"
    };
    size_t i;
    for (i = 0; i < sizeof(normal) / sizeof(normal[0]); ++i) {
        wchar_t *wide = kg_wide(normal[i]);
        CHECK(wide != NULL);
        if (wide) expected_display(normal[i], wide);
        roundtrip(normal[i]);
        free(wide);
    }
    expected_remote("/home/user/", L"folder\\100%20.txt", "/home/user/folder/100%20.txt");
    expected_remote("/", L"\\plain", "/plain");
    expected_remote("/base", L"", "/base");
    expected_remote("/base", L"%2E%2E\\%2F", "/base/%2E%2E/%2F");
}

static void test_reserved_component_encoding(void)
{
    expected_display(":", L"~kg~3A");
    expected_display("a:b", L"~kg~613A62");
    expected_display("a\\b", L"~kg~615C62");
    expected_display("a.", L"~kg~612E");
    expected_display("a ", L"~kg~6120");
    expected_display("CON", L"~kg~434F4E");
    expected_display("con.txt", L"~kg~636F6E2E747874");
    expected_display("LPT9.dat", L"~kg~4C5054392E646174");
    expected_display("~kg~", L"~kg~7E6B677E");
    expected_display("~kg~41", L"~kg~7E6B677E3431");
    expected_display("\xff\xc0\xaf", L"~kg~FFC0AF");
    expected_display("\xed\xa0\x80", L"~kg~EDA080");
    expected_display("\x01", L"~kg~01");
    expected_display("\x7f", L"~kg~7F");
    roundtrip(":"); roundtrip("a:b"); roundtrip("a\\b"); roundtrip("a.");
    roundtrip("a "); roundtrip("CON"); roundtrip("con.txt"); roundtrip("LPT9.dat");
    roundtrip("~kg~"); roundtrip("~kg~41"); roundtrip("\xff\xc0\xaf");
    roundtrip("\xed\xa0\x80"); roundtrip("\x01"); roundtrip("\x7f");
    expected_remote("/base", L"~kg~7E6B677E3431", "/base/~kg~41");
    expected_remote("/base", L"folder\\~kg~613A62", "/base/folder/a:b");
}

static void test_each_nonzero_byte_roundtrip(void)
{
    unsigned int value;
    for (value = 1; value <= 255; ++value) {
        char raw[7] = {'x', (char)value, 'e', 'n', 'd', 0, 0};
        if (value == '/') continue; /* '/' is a separator, never a Unix filename byte. */
        roundtrip(raw);
    }
}

static void test_reject_traversal_and_noncanonical_aliases(void)
{
    const wchar_t *bad[] = {
        L".", L"..", L"a\\..\\b", L"a\\.\\b", L"a/b", L"~kg~2E", L"~kg~2E2E",
        L"~kg~00", L"~kg~2F", L"~kg~612F62",
        L"~kg~", L"~kg~3", L"~kg~GG", L"~kg~3a",
        L"~kg~41", L"~kg~6125", L"~kg~313030253230",
        L"folder\\\\file"
    };
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) rejected_remote(bad[i]);
}

static void test_name_capacity(void)
{
    wchar_t displayed[260], small[7];
    char normal[256], unsafe[129];
    memset(normal, 'x', 255); normal[255] = 0;
    CHECK(kg_display_name(normal, displayed, 260));
    CHECK(wcslen(displayed) == 255);
    roundtrip(normal);
    CHECK(!kg_display_name("x", small, 1));
    CHECK(kg_display_name("x", small, 2));
    CHECK(!kg_display_name(":", small, 6));
    CHECK(kg_display_name(":", small, 7));
    CHECK(!kg_display_name("", displayed, 260));
    memset(unsafe, ':', 127); unsafe[127] = 0;
    CHECK(kg_display_name(unsafe, displayed, 260));
    CHECK(wcslen(displayed) == 258);
    unsafe[127] = ':'; unsafe[128] = 0;
    CHECK(!kg_display_name(unsafe, displayed, 260));
}

static void test_encoding_api_rejections(void)
{
    static const wchar_t invalid_surrogate[] = {0xd800, 0};
    wchar_t *wide;
    char *utf;
    wide = kg_wide("\xc0\xaf");
    CHECK(wide == NULL); free(wide);
    wide = kg_wide("\xed\xa0\x80");
    CHECK(wide == NULL); free(wide);
    utf = kg_utf8(invalid_surrogate);
    CHECK(utf == NULL); free(utf);
}

static void expected_local(const wchar_t *input, const wchar_t *expected)
{
    wchar_t *path = kg_local_path(input);
    CHECK(path != NULL);
    if (path) CHECK(!wcscmp(path, expected));
    free(path);
}

static void rejected_local(const wchar_t *input)
{
    wchar_t *path = kg_local_path(input);
    CHECK(path == NULL);
    free(path);
}

static void test_local_namespaces_and_long_component(void)
{
    wchar_t input[280], expected[284];
    size_t i, prefix;
    expected_local(L"C:\\folder\\file.txt", L"\\\\?\\C:\\folder\\file.txt");
    expected_local(L"\\\\server\\share\\file", L"\\\\?\\UNC\\server\\share\\file");
    expected_local(L"\\\\?\\C:\\folder\\file", L"\\\\?\\C:\\folder\\file");
    expected_local(L"\\\\?\\UNC\\server\\share\\file", L"\\\\?\\UNC\\server\\share\\file");
    rejected_local(L"\\\\.\\PhysicalDrive0");
    rejected_local(L"\\\\.\\pipe\\example");
    rejected_local(L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\file");
    rejected_local(L"\\\\?\\PIPE\\example");
    wcscpy(input, L"C:\\tmp\\");
    prefix = wcslen(input);
    for (i = 0; i < 255; ++i) input[prefix + i] = L'a';
    input[prefix + 255] = 0;
    wcscpy(expected, L"\\\\?\\"); wcscat(expected, input);
    expected_local(input, expected);
}

static void test_quote_with_windows_parser(void)
{
    const wchar_t *args[] = {
        L"kg-test.exe", L"", L"plain", L"a b", L"\"",
        L"C:\\folder with space\\", L"trailing\\\\",
        L"a\\\"b", L"a\\\\\"b", L"a\\\\\\\"b",
        L"folder/host \"lab\"", L"%USERPROFILE% & | < > ^ ! $()",
        L"\x0422\x0435\x0441\x0442 \xd83d\xde00",
        L"line1\r\nline2", L"\\"
    };
    wchar_t command[4096] = {0};
    wchar_t **parsed;
    size_t used = 0, i;
    int count;
    for (i = 0; i < sizeof(args) / sizeof(args[0]); ++i)
        CHECK(kg_quote_arg(command, 4096, &used, args[i]));
    parsed = CommandLineToArgvW(command, &count);
    CHECK(parsed != NULL);
    if (!parsed) return;
    CHECK(count == (int)(sizeof(args) / sizeof(args[0])));
    if (count == (int)(sizeof(args) / sizeof(args[0]))) {
        for (i = 0; i < (size_t)count; ++i) CHECK(!wcscmp(parsed[i], args[i]));
    }
    LocalFree(parsed);
    {
        struct { wchar_t before, text[8], after; } bounded = {0x1234, {0}, 0x5678};
        size_t short_used = 0;
        CHECK(!kg_quote_arg(bounded.text, 8, &short_used, L"a very long argument"));
        CHECK(bounded.before == 0x1234 && bounded.after == 0x5678);
        CHECK(short_used < 8 && bounded.text[short_used] == 0);
    }
}

static void test_time_boundaries(void)
{
    const uint32_t seconds[] = {0u, 2147483647u, 2147483648u, 2147483649u, UINT32_MAX};
    size_t i;
    uint32_t result = 99;
    FILETIME ft;
    SYSTEMTIME st;
    ULARGE_INTEGER ticks;
    for (i = 0; i < sizeof(seconds) / sizeof(seconds[0]); ++i) {
        ft = kg_filetime(seconds[i]);
        CHECK(kg_unix_time(&ft, &result) && result == seconds[i]);
    }
    ft = kg_filetime(0);
    ticks.LowPart = ft.dwLowDateTime; ticks.HighPart = ft.dwHighDateTime;
    CHECK(ticks.QuadPart == UINT64_C(116444736000000000));
    --ticks.QuadPart;
    ft.dwLowDateTime = ticks.LowPart; ft.dwHighDateTime = ticks.HighPart;
    CHECK(!kg_unix_time(&ft, &result));
    ft = kg_filetime(2147483648u);
    CHECK(FileTimeToSystemTime(&ft, &st));
    CHECK(st.wYear == 2038 && st.wMonth == 1 && st.wDay == 19);
    ft = kg_filetime(UINT32_MAX);
    CHECK(FileTimeToSystemTime(&ft, &st));
    CHECK(st.wYear == 2106 && st.wMonth == 2 && st.wDay == 7 &&
          st.wHour == 6 && st.wMinute == 28 && st.wSecond == 15);
    ticks.LowPart = ft.dwLowDateTime; ticks.HighPart = ft.dwHighDateTime;
    ticks.QuadPart += UINT64_C(9999999);
    ft.dwLowDateTime = ticks.LowPart; ft.dwHighDateTime = ticks.HighPart;
    CHECK(kg_unix_time(&ft, &result) && result == UINT32_MAX);
    ++ticks.QuadPart;
    ft.dwLowDateTime = ticks.LowPart; ft.dwHighDateTime = ticks.HighPart;
    CHECK(!kg_unix_time(&ft, &result));
}

static void test_random_temp_suffix(void)
{
    char a[33], b[33];
    size_t i;
    int ok_a = kg_random_hex(a), ok_b = kg_random_hex(b);
    CHECK(ok_a && ok_b);
    if (!ok_a || !ok_b) return;
    CHECK(a[32] == 0 && b[32] == 0 && strcmp(a, b));
    for (i = 0; i < 32; ++i) CHECK(strchr("0123456789abcdef", a[i]) != NULL);
}

int main(void)
{
    test_literal_names_and_unicode();
    test_reserved_component_encoding();
    test_each_nonzero_byte_roundtrip();
    test_reject_traversal_and_noncanonical_aliases();
    test_name_capacity();
    test_encoding_api_rejections();
    test_local_namespaces_and_long_component();
    test_quote_with_windows_parser();
    test_time_boundaries();
    test_random_temp_suffix();
    printf("Path utilities: %u checks, %u failures.\n", checks, failures);
    return failures ? 1 : 0;
}
