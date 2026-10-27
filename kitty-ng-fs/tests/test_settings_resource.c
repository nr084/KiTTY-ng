/* Inspect the compiled settings dialog and VERSIONINFO in both DLLs.
 * LOAD_LIBRARY_AS_DATAFILE does not execute either plugin's entry point and
 * permits a 64-bit test to inspect the x86 DLL. This is not a GUI click test. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winver.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/resource.h"

typedef struct ResourceReader {
    const unsigned char *bytes;
    size_t size, at;
    int ok;
} ResourceReader;

typedef struct ResourceString {
    WORD ordinal;
    wchar_t text[256];
} ResourceString;

typedef struct Control {
    DWORD id, style;
    ResourceString window_class, title;
} Control;

static int checks, failures;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s (Win32 error %lu)\n", __LINE__, #condition, \
            (unsigned long)GetLastError()); failures++; } } while (0)

static WORD read16(ResourceReader *r)
{
    WORD value;
    if (!r->ok || r->at > r->size || r->size - r->at < 2) { r->ok = 0; return 0; }
    value = (WORD)(r->bytes[r->at] | ((WORD)r->bytes[r->at + 1] << 8));
    r->at += 2;
    return value;
}

static DWORD read32(ResourceReader *r)
{
    DWORD lo = read16(r), hi = read16(r);
    return lo | (hi << 16);
}

static void skip(ResourceReader *r, size_t count)
{
    if (!r->ok || r->at > r->size || count > r->size - r->at) r->ok = 0;
    else r->at += count;
}

static ResourceString read_string(ResourceReader *r)
{
    ResourceString result;
    WORD value;
    size_t used = 0;
    memset(&result, 0, sizeof(result));
    value = read16(r);
    if (value == 0xffff) { result.ordinal = read16(r); return result; }
    while (r->ok && value) {
        if (used + 1 < sizeof(result.text) / sizeof(result.text[0]))
            result.text[used++] = (wchar_t)value;
        else r->ok = 0;
        value = read16(r);
    }
    result.text[used] = 0;
    return result;
}

static int class_is(const ResourceString *value, WORD ordinal, const wchar_t *name)
{
    return value->ordinal == ordinal ||
        (value->text[0] && CompareStringOrdinal(value->text, -1, name, -1, TRUE) == CSTR_EQUAL);
}

static const Control *find_control(const Control *controls, size_t count, DWORD id)
{
    size_t i;
    const Control *found = NULL;
    for (i = 0; i < count; i++) {
        if (controls[i].id == id) {
            if (found) return NULL; /* duplicate IDs are also an error */
            found = &controls[i];
        }
    }
    return found;
}

static void check_edit(const Control *controls, size_t count, DWORD id, DWORD required_style)
{
    const Control *c = find_control(controls, count, id);
    CHECK(c != NULL);
    if (!c) return;
    CHECK(class_is(&c->window_class, 0x81, L"Edit"));
    CHECK((c->style & (WS_VISIBLE | WS_TABSTOP | required_style)) ==
          (WS_VISIBLE | WS_TABSTOP | required_style));
    CHECK(!(c->style & WS_DISABLED));
}

static void check_button(const Control *controls, size_t count, DWORD id, DWORD type)
{
    const Control *c = find_control(controls, count, id);
    CHECK(c != NULL);
    if (!c) return;
    CHECK(class_is(&c->window_class, 0x80, L"Button"));
    CHECK((c->style & 0x0fu) == type);
    CHECK((c->style & (WS_VISIBLE | WS_TABSTOP)) == (WS_VISIBLE | WS_TABSTOP));
    CHECK(!(c->style & WS_DISABLED));
    CHECK(c->title.text[0] != 0);
}

static void inspect_dialog(const wchar_t *path)
{
    HMODULE module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    HRSRC resource;
    HGLOBAL loaded;
    const unsigned char *data;
    DWORD size, style;
    WORD version, signature, item_count;
    ResourceReader reader;
    ResourceString dialog_title;
    Control controls[128];
    size_t i;
    const Control *browse, *author, *homepage;
    CHECK(module != NULL);
    if (!module) return;
    resource = FindResourceW(module, MAKEINTRESOURCEW(IDD_SETTINGS), MAKEINTRESOURCEW(5)); /* RT_DIALOG */
    CHECK(resource != NULL);
    if (!resource) { FreeLibrary(module); return; }
    size = SizeofResource(module, resource);
    CHECK(size >= 26);
    loaded = LoadResource(module, resource);
    CHECK(loaded != NULL);
    data = loaded ? (const unsigned char *)LockResource(loaded) : NULL;
    CHECK(data != NULL);
    if (!data || size < 26) { FreeLibrary(module); return; }
    reader.bytes = data; reader.size = size; reader.at = 0; reader.ok = 1;
    version = read16(&reader);
    signature = read16(&reader);
    CHECK(version == 1 && signature == 0xffff); /* DLGTEMPLATEEX */
    if (version != 1 || signature != 0xffff) { FreeLibrary(module); return; }
    (void)read32(&reader); /* helpID */
    (void)read32(&reader); /* extended style */
    style = read32(&reader);
    item_count = read16(&reader);
    skip(&reader, 8); /* dialog x/y/cx/cy */
    (void)read_string(&reader); /* menu */
    (void)read_string(&reader); /* dialog class */
    dialog_title = read_string(&reader);
    CHECK(!wcscmp(dialog_title.text, L"KiTTY-ng FS 1.1.1 — настройки"));
    if (style & DS_SETFONT) {
        skip(&reader, 6); /* pointsize, weight, italic, charset */
        (void)read_string(&reader); /* typeface */
    }
    CHECK(item_count == 13);
    if (!reader.ok || !item_count || item_count > 128) { FreeLibrary(module); return; }
    memset(controls, 0, sizeof(controls));
    for (i = 0; i < item_count && reader.ok; i++) {
        WORD extra;
        skip(&reader, (4 - (reader.at & 3)) & 3); /* DWORD alignment */
        (void)read32(&reader); /* helpID */
        (void)read32(&reader); /* extended style */
        controls[i].style = read32(&reader);
        skip(&reader, 8); /* item x/y/cx/cy */
        controls[i].id = read32(&reader);
        controls[i].window_class = read_string(&reader);
        controls[i].title = read_string(&reader);
        extra = read16(&reader);
        skip(&reader, extra); /* DLGTEMPLATEEX creation-data bytes */
    }
    CHECK(reader.ok && i == item_count);
    if (reader.ok && i == item_count) {
        check_edit(controls, item_count, IDC_ROOT, ES_AUTOHSCROLL);
        check_button(controls, item_count, IDC_BROWSE, BS_PUSHBUTTON);
        browse = find_control(controls, item_count, IDC_BROWSE);
        CHECK(browse && !wcscmp(browse->title.text, L"\x041E\x0431\x0437\x043E\x0440\x2026"));
        check_edit(controls, item_count, IDC_REMOTE_ROOT, ES_AUTOHSCROLL);
        check_edit(controls, item_count, IDC_TIMEOUT, ES_NUMBER);
        author = find_control(controls, item_count, IDC_AUTHOR);
        CHECK(author != NULL);
        if (author) {
            CHECK(class_is(&author->window_class, 0x82, L"Static"));
            CHECK((author->style & WS_VISIBLE) && !(author->style & WS_DISABLED));
            CHECK(!wcscmp(author->title.text, L"Автор: nr084"));
        }
        check_button(controls, item_count, IDC_HOMEPAGE, BS_PUSHBUTTON);
        homepage = find_control(controls, item_count, IDC_HOMEPAGE);
        CHECK(homepage && !wcscmp(homepage->title.text,
              L"Домашняя страница: https://github.com/nr084/KiTTY-ng"));
        check_button(controls, item_count, IDOK, BS_DEFPUSHBUTTON);
        check_button(controls, item_count, IDCANCEL, BS_PUSHBUTTON);
    }
    printf("Inspected compiled settings dialog: %u controls, %lu resource bytes.\n",
           (unsigned)item_count, (unsigned long)size);
    FreeLibrary(module);
}

static void check_version_string(void *block, const wchar_t *field, const wchar_t *expected)
{
    wchar_t query[128];
    void *value = NULL;
    UINT length = 0;
    BOOL found;
    swprintf(query, sizeof(query) / sizeof(query[0]), L"\\StringFileInfo\\040904b0\\%ls", field);
    found = VerQueryValueW(block, query, &value, &length);
    CHECK(found && value && length && ((const wchar_t *)value)[length - 1] == 0);
    if (found && value && length && ((const wchar_t *)value)[length - 1] == 0)
        CHECK(!wcscmp((const wchar_t *)value, expected));
}

static void inspect_version(const wchar_t *path)
{
    DWORD ignored, size = GetFileVersionInfoSizeW(path, &ignored);
    void *block;
    void *fixed = NULL;
    UINT length = 0;
    BOOL loaded, found;
    const VS_FIXEDFILEINFO *info;
    CHECK(size > 0);
    if (!size) return;
    block = malloc(size);
    CHECK(block != NULL);
    if (!block) return;
    loaded = GetFileVersionInfoW(path, 0, size, block);
    CHECK(loaded);
    if (!loaded) { free(block); return; }
    found = VerQueryValueW(block, L"\\", &fixed, &length);
    CHECK(found && fixed && length >= sizeof(VS_FIXEDFILEINFO));
    if (found && fixed && length >= sizeof(VS_FIXEDFILEINFO)) {
        info = (const VS_FIXEDFILEINFO *)fixed;
        CHECK(info->dwSignature == VS_FFI_SIGNATURE);
        CHECK(info->dwFileVersionMS == 0x00010001u && info->dwFileVersionLS == 0x00010000u);
        CHECK(info->dwProductVersionMS == 0x00010001u && info->dwProductVersionLS == 0x00010000u);
    }
    check_version_string(block, L"CompanyName", L"nr084");
    check_version_string(block, L"Author", L"nr084");
    check_version_string(block, L"Comments", L"Author: nr084. Homepage: https://github.com/nr084/KiTTY-ng");
    check_version_string(block, L"LegalCopyright", L"Copyright (c) 2026 nr084. MIT License.");
    check_version_string(block, L"URLInfoAbout", L"https://github.com/nr084/KiTTY-ng");
    check_version_string(block, L"FileVersion", L"1.1.1");
    check_version_string(block, L"ProductVersion", L"1.1.1");
    printf("Inspected VERSIONINFO: 1.1.1.0, author nr084, homepage https://github.com/nr084/KiTTY-ng.\n");
    free(block);
}

int wmain(int argc, wchar_t **argv)
{
    int i;
    if (argc != 3) {
        fprintf(stderr, "Usage: test_settings_resource.exe <x86.wfx> <x64.wfx64>\n");
        return 2;
    }
    for (i = 1; i < argc; i++) { inspect_dialog(argv[i]); inspect_version(argv[i]); }
    printf("Settings resources: %d checks, %d failures; both DLLs loaded as data only.\n",
           checks, failures);
    return failures ? 1 : 0;
}
