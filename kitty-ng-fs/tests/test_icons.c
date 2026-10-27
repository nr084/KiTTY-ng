/* Exercise v1 icon exports and virtual group navigation on a standalone
 * compiled DLL. No real credentials/server are used. A synthetic Klink copy
 * records any accidental launch, so group browsing/icon extraction must stay
 * entirely local. The x86 test must run in an x86 process; resource extraction
 * alone is not claimed as x86 execution.
 *
 * Build: <target>-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -municode
 *   tests/test_icons.c -o build/test-icons.exe -lshell32 -lgdi32 -luser32
 * Run: test-icons.exe FULL_PATH_TO_MATCHING_WFX
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/wfx.h"

#ifndef FS_ICONFLAG_SMALL
#define FS_ICONFLAG_SMALL 1
#define FS_ICONFLAG_BACKGROUND 2
#define FS_ICON_USEDEFAULT 0
#define FS_ICON_EXTRACTED_DESTROY 2
#endif

#define CAP 4096
typedef int (WINAPI *InitWFn)(int, tProgressProcW, tLogProcW, tRequestProcW);
typedef HANDLE (WINAPI *FirstWFn)(wchar_t *, WIN32_FIND_DATAW *);
typedef BOOL (WINAPI *NextWFn)(HANDLE, WIN32_FIND_DATAW *);
typedef int (WINAPI *CloseFn)(HANDLE);
typedef int (WINAPI *IconWFn)(wchar_t *, int, HICON *);
typedef int (WINAPI *IconAFn)(char *, int, HICON *);
typedef int (WINAPI *ExecuteWFn)(HWND, wchar_t *, wchar_t *);
typedef int (WINAPI *ExecuteAFn)(HWND, char *, char *);
typedef int (WINAPI *GetWFn)(wchar_t *, wchar_t *, int, RemoteInfoStruct *);
typedef BOOL (WINAPI *DeleteWFn)(wchar_t *);
typedef int (WINAPI *RenameWFn)(wchar_t *, wchar_t *, BOOL, BOOL, RemoteInfoStruct *);
typedef struct {
    InitWFn init;
    FirstWFn first;
    NextWFn next;
    CloseFn close;
    IconWFn icon;
    IconAFn icon_a;
    ExecuteWFn execute;
    ExecuteAFn execute_a;
    GetWFn get;
    DeleteWFn delete_file;
    RenameWFn rename;
} Api;

static Api api;
static int checks, failures, connects;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr,"FAIL line %d: %s (Windows %lu)\n", __LINE__, #c, \
            (unsigned long)GetLastError()); fflush(stderr); } } while (0)

static void join(wchar_t out[CAP], const wchar_t *a, const wchar_t *b)
{
    int n = swprintf(out, CAP, L"%ls\\%ls", a, b);
    if (n < 0 || n >= CAP) { fprintf(stderr,"Fixture path too long\n"); exit(2); }
}

static void extended_path(wchar_t out[CAP], const wchar_t *path)
{
    int n;
    if (!wcsncmp(path, L"\\\\?\\", 4)) n = swprintf(out, CAP, L"%ls", path);
    else if (!wcsncmp(path, L"\\\\", 2)) n = swprintf(out, CAP, L"\\\\?\\UNC\\%ls", path + 2);
    else n = swprintf(out, CAP, L"\\\\?\\%ls", path);
    if (n < 0 || n >= CAP) { fprintf(stderr,"Fixture path too long\n"); exit(2); }
}

static int write_bytes(const wchar_t *path, const void *bytes, size_t size)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD done;
    int ok;
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = size <= UINT32_MAX && WriteFile(file, bytes, (DWORD)size, &done, NULL) && done == size;
    CloseHandle(file); return ok;
}

static int utf16_ini(const wchar_t *path, const wchar_t *kitty)
{
    wchar_t text[CAP * 2];
    int n = swprintf(text, CAP * 2, L"\xfeff[KiTTYngFS]\r\nPortableDir=%ls\r\nRemoteRoot=/\r\nTimeoutSeconds=5\r\n", kitty);
    return n > 0 && n < CAP * 2 && write_bytes(path, text, (size_t)n * sizeof(wchar_t));
}

static int profile_file(const wchar_t *directory, const wchar_t *name)
{
    static const char content[] = "HostName\\192.0.2.1\\\nProtocol\\ssh\\\nPortNumber\\2222\\\nUserName\\fixture\\\n";
    wchar_t path[CAP]; join(path, directory, name);
    return write_bytes(path, content, sizeof(content) - 1);
}

static void remove_tree(const wchar_t *directory)
{
    WIN32_FIND_DATAW d;
    wchar_t glob[CAP], child[CAP];
    HANDLE h;
    join(glob, directory, L"*"); h = FindFirstFileW(glob, &d);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(d.cFileName,L".") || !wcscmp(d.cFileName,L"..")) continue;
            join(child, directory, d.cFileName);
            if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) remove_tree(child);
            else DeleteFileW(child);
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    RemoveDirectoryW(directory);
}

static int WINAPI progress(int nr, wchar_t *source, wchar_t *target, int percent)
{ (void)nr; (void)source; (void)target; (void)percent; return 0; }
static void WINAPI logger(int nr, int type, wchar_t *message)
{ (void)nr; (void)message; if (type == MSGTYPE_CONNECT) connects++; }
static BOOL WINAPI request(int nr, int type, wchar_t *title, wchar_t *text, wchar_t *out, int maxlen)
{ (void)nr; (void)type; (void)title; (void)text; if (maxlen) *out=0; return FALSE; }

static int load(HMODULE module, const char *name, void *dest, size_t size)
{
    FARPROC proc = GetProcAddress(module, name);
    if (!proc || size != sizeof(proc)) { fprintf(stderr,"Missing export: %s\n",name); return 0; }
    memcpy(dest, &proc, size); return 1;
}
#define LOAD(field,name) do { if (!load(module,name,&api.field,sizeof(api.field))) goto cleanup; } while (0)

static int entry_count(const wchar_t *name, const wchar_t *wanted, DWORD *attributes)
{
    wchar_t path[CAP];
    WIN32_FIND_DATAW d;
    HANDLE h;
    int count = 0;
    wcscpy(path, name); h = api.first(path, &d);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (!wanted || !wcscmp(d.cFileName, wanted)) { count++; if (attributes) *attributes = d.dwFileAttributes; }
    } while (api.next(h, &d));
    api.close(h); return count;
}

static uint64_t icon_fingerprint(HICON icon, int expected_size)
{
    ICONINFO info;
    BITMAP bitmap;
    BITMAPINFO bi;
    HDC dc;
    unsigned char *pixels = NULL;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t n, i;
    int transparent = 0, visible = 0;
    memset(&info,0,sizeof(info));
    CHECK(GetIconInfo(icon, &info));
    if (!icon || !info.hbmColor) return 0;
    memset(&bitmap,0,sizeof(bitmap));
    CHECK(GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap) == sizeof(bitmap));
    CHECK(bitmap.bmWidth == expected_size && bitmap.bmHeight == expected_size);
    n = (size_t)bitmap.bmWidth * (size_t)bitmap.bmHeight * 4;
    if (n > 1024 * 1024 || !(pixels = malloc(n))) { hash = 0; goto done; }
    memset(&bi,0,sizeof(bi)); bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = bitmap.bmWidth; bi.bmiHeader.biHeight = -bitmap.bmHeight;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    CHECK(dc != NULL);
    if (!dc) { hash=0; goto done; }
    {
        int lines=GetDIBits(dc,info.hbmColor,0,(UINT)bitmap.bmHeight,pixels,&bi,DIB_RGB_COLORS);
        CHECK(lines == bitmap.bmHeight);
        if (lines != bitmap.bmHeight) { DeleteDC(dc); hash=0; goto done; }
    }
    DeleteDC(dc);
    for (i=0; i<n; i++) { hash ^= pixels[i]; hash *= UINT64_C(1099511628211); }
    for (i=3; i<n; i+=4) { if (!pixels[i]) transparent++; else visible++; }
    CHECK(transparent > 0 && visible > 0);
done:
    free(pixels); DeleteObject(info.hbmColor); if (info.hbmMask) DeleteObject(info.hbmMask);
    return hash;
}

static uint64_t extracted(const wchar_t *name, int flags)
{
    wchar_t path[CAP], original[CAP];
    HICON icon = NULL;
    uint64_t result = 0;
    int status;
    wcscpy(path,name); wcscpy(original,name);
    status = api.icon(path,flags,&icon);
    CHECK(status == FS_ICON_EXTRACTED_DESTROY && icon != NULL);
    CHECK(!wcscmp(path,original));
    if (icon) { result = icon_fingerprint(icon, flags & FS_ICONFLAG_SMALL ? 16 : 32); CHECK(DestroyIcon(icon)); }
    return result;
}

static void default_icon(const wchar_t *name, int flags)
{
    wchar_t path[CAP]; HICON icon = (HICON)(UINT_PTR)1;
    wcscpy(path,name);
    CHECK(api.icon(path,flags,&icon) == FS_ICON_USEDEFAULT);
    CHECK(icon == NULL);
}

static void redirected(const wchar_t *name, const wchar_t *expected)
{
    wchar_t path[MAX_PATH + 1];
    wmemset(path, 0x55aa, MAX_PATH + 1);
    wcscpy(path, name);
    CHECK(api.execute(NULL, path, L"oPeN") == FS_EXEC_SYMLINK);
    CHECK(!wcscmp(path, expected));
    CHECK(path[MAX_PATH] == 0x55aa);
}

static void redirected_ansi(const char *name, const char *expected)
{
    char path[MAX_PATH + 1];
    memset(path, 0xa5, sizeof(path));
    strcpy(path, name);
    CHECK(api.execute_a(NULL, path, "open") == FS_EXEC_SYMLINK);
    CHECK(!strcmp(path, expected));
    CHECK((unsigned char)path[MAX_PATH] == 0xa5);
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t temp[CAP], base[CAP], extended_base[CAP], kitty[CAP], sessions[CAP], group[CAP], nested[CAP];
    wchar_t plugin[CAP], ini[CAP], self[CAP], kli[CAP], marker[CAP];
    wchar_t long_group[101], long_nested[101], boundary_leaf[56], oversized_leaf[57];
    wchar_t long_directory[CAP], long_subdirectory[CAP], long_parent[CAP];
    wchar_t boundary_path[CAP], oversized_path[CAP], boundary_target[CAP];
    HMODULE module = NULL;
    HICON shell_small = NULL, shell_large = NULL, ansi_icon = NULL;
    uint64_t root16, ssh16;
    DWORD attrs = 0;
    int result = 2;
    if (argc >= 3 && !wcscmp(argv[1],L"--portable-dir")) {
        join(marker,argv[2],L"unwanted-klink-started"); write_bytes(marker,"started",7); return 97;
    }
    if (argc != 2) { fprintf(stderr,"Usage: test-icons.exe FULL_PLUGIN_PATH\n"); return 2; }
    if (!GetTempPathW(CAP,temp) || !GetTempFileNameW(temp,L"kgi",0,base) || !DeleteFileW(base) || !CreateDirectoryW(base,NULL)) return 2;
    extended_path(extended_base,base);
    join(kitty,extended_base,L"kitty"); CHECK(CreateDirectoryW(kitty,NULL));
    join(sessions,kitty,L"Sessions"); CHECK(CreateDirectoryW(sessions,NULL));
    join(group,sessions,L"Office"); CHECK(CreateDirectoryW(group,NULL));
    join(nested,group,L"Nested"); CHECK(CreateDirectoryW(nested,NULL));
    /* The root SSH name sorts before Office alphabetically: the DIRECTORY
     * distinction, rather than an incidental name order, must group folders. */
    CHECK(profile_file(sessions,L"mail.example.ru")); CHECK(profile_file(nested,L"nested.host"));
    CHECK(profile_file(sessions,L"AServer")); CHECK(profile_file(group,L"Server")); CHECK(profile_file(nested,L"Another"));
    wmemset(long_group,L'Z',100); long_group[100]=0;
    wmemset(long_nested,L'N',100); long_nested[100]=0;
    wmemset(boundary_leaf,L'B',55); boundary_leaf[55]=0;
    wmemset(oversized_leaf,L'C',56); oversized_leaf[56]=0;
    join(long_directory,sessions,long_group); CHECK(CreateDirectoryW(long_directory,NULL));
    join(long_subdirectory,long_directory,long_nested); CHECK(CreateDirectoryW(long_subdirectory,NULL));
    CHECK(profile_file(long_subdirectory,boundary_leaf)); CHECK(profile_file(long_subdirectory,oversized_leaf));
    swprintf(long_parent,CAP,L"\\%ls\\%ls",long_group,long_nested);
    join(boundary_path,long_parent,boundary_leaf); join(oversized_path,long_parent,oversized_leaf);
    swprintf(boundary_target,CAP,L"%ls\\",boundary_path);
    CHECK(wcslen(boundary_target) == MAX_PATH-1 && wcslen(oversized_path) == MAX_PATH-1);
    join(ini,base,L"kittyngfs.ini"); CHECK(utf16_ini(ini,kitty));
#if defined(_WIN64)
    join(plugin,base,L"kitty-ng-fs.wfx64"); join(kli,kitty,L"klink-x64.exe");
#else
    join(plugin,base,L"kitty-ng-fs.wfx"); join(kli,kitty,L"klink-x86.exe");
#endif
    CHECK(CopyFileW(argv[1],plugin,FALSE));
    CHECK(GetModuleFileNameW(NULL,self,CAP) > 0); CHECK(CopyFileW(self,kli,FALSE));
    join(marker,kitty,L"unwanted-klink-started");
    module = LoadLibraryW(plugin); CHECK(module != NULL); if (!module) goto cleanup;
    LOAD(init,"FsInitW"); LOAD(first,"FsFindFirstW"); LOAD(next,"FsFindNextW"); LOAD(close,"FsFindClose");
    LOAD(icon,"FsExtractCustomIconW"); LOAD(icon_a,"FsExtractCustomIcon");
    LOAD(execute,"FsExecuteFileW"); LOAD(execute_a,"FsExecuteFile");
    LOAD(get,"FsGetFileW"); LOAD(delete_file,"FsDeleteFileW"); LOAD(rename,"FsRenMovFileW");
    CHECK(api.init(1,progress,logger,request) == 0);
    CHECK(entry_count(L"\\",L"Office",&attrs) == 1 && (attrs & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(entry_count(L"\\",L"AServer",&attrs) == 1 && attrs == FILE_ATTRIBUTE_NORMAL);
    CHECK(entry_count(L"\\",L"Office%2FServer",NULL) == 0);
    CHECK(entry_count(L"\\Office",L"Nested",&attrs) == 1 && (attrs & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(entry_count(L"\\Office",L"Server",&attrs) == 1 && attrs == FILE_ATTRIBUTE_NORMAL);
    CHECK(entry_count(L"\\Office\\Nested",L"Another",&attrs) == 1 && attrs == FILE_ATTRIBUTE_NORMAL);
    CHECK(entry_count(long_parent,boundary_leaf,&attrs) == 1 && attrs == FILE_ATTRIBUTE_NORMAL);
    CHECK(entry_count(long_parent,oversized_leaf,&attrs) == 1 && attrs == FILE_ATTRIBUTE_NORMAL);
    CHECK(entry_count(L"\\",L"mail\x00b7" L"example\x00b7ru",&attrs) == 1 && attrs == FILE_ATTRIBUTE_NORMAL);
    CHECK(entry_count(L"\\",L"mail.example.ru",NULL) == 0);
    redirected(L"\\mail.example.ru",L"\\mail\x00b7" L"example\x00b7ru\\");
    redirected(L"\\Office\\Nested\\nested.host",L"\\Office\\Nested\\nested\x00b7host\\");
    redirected(L"\\AServer",L"\\AServer\\");
    redirected(L"\\Office\\Server",L"\\Office\\Server\\");
    redirected(L"\\Office\\Nested\\Another",L"\\Office\\Nested\\Another\\");
    redirected(L"\\Office%2FServer",L"\\Office\\Server\\");
    redirected(boundary_path,boundary_target);
    redirected_ansi("\\AServer","\\AServer\\");
    redirected_ansi("\\Office\\Nested\\Another","\\Office\\Nested\\Another\\");
    redirected_ansi("\\Office%2FServer","\\Office\\Server\\");
    {
        wchar_t path[MAX_PATH+1], original[MAX_PATH+1];
        char ansi[MAX_PATH+1], ansi_original[MAX_PATH+1];
        wmemset(path,0x55aa,MAX_PATH+1); wcscpy(path,oversized_path); memcpy(original,path,sizeof(path));
        SetLastError(ERROR_SUCCESS);
        CHECK(api.execute(NULL,path,L"open") == FS_EXEC_ERROR);
        CHECK(GetLastError() == ERROR_FILENAME_EXCED_RANGE);
        CHECK(!memcmp(path,original,sizeof(path)));
        memset(ansi,0xa5,sizeof(ansi));
        CHECK(WideCharToMultiByte(CP_ACP,0,oversized_path,-1,ansi,MAX_PATH,NULL,NULL) == MAX_PATH);
        memcpy(ansi_original,ansi,sizeof(ansi));
        CHECK(api.execute_a(NULL,ansi,"open") == FS_EXEC_ERROR);
        CHECK(!memcmp(ansi,ansi_original,sizeof(ansi)));
    }
    {
        wchar_t source[] = L"\\AServer", target[] = L"\\Office\\Server", destination[CAP];
        RemoteInfoStruct info = {0};
        join(destination,base,L"must-not-download-a-session");
        CHECK(api.get(source,destination,0,&info) != FS_FILE_OK);
        CHECK(GetFileAttributesW(destination) == INVALID_FILE_ATTRIBUTES);
        CHECK(!api.delete_file(source));
        CHECK(api.rename(source,target,TRUE,FALSE,&info) == FS_FILE_NOTSUPPORTED);
        CHECK(entry_count(L"\\",L"AServer",&attrs) == 1 && attrs == FILE_ATTRIBUTE_NORMAL);
    }
    CHECK(connects == 0 && GetFileAttributesW(marker) == INVALID_FILE_ATTRIBUTES);

    /* Request the two sizes separately. Wine 9's paired extraction creates
     * both handles from the selected large-frame resource and rescales it,
     * whereas this icon set deliberately contains hand-tuned small artwork.
     * The single-size calls check the actual first group and exact frames. */
    CHECK(ExtractIconExW(plugin,0,&shell_large,NULL,1) > 0);
    CHECK(ExtractIconExW(plugin,0,NULL,&shell_small,1) > 0);
    CHECK(shell_large != NULL && shell_small != NULL);
    root16 = extracted(L"\\",FS_ICONFLAG_SMALL);
    CHECK(icon_fingerprint(shell_small,16) == root16);
    CHECK(icon_fingerprint(shell_large,32) == extracted(L"\\",0));
    ssh16 = extracted(L"\\AServer",FS_ICONFLAG_SMALL);
    CHECK(ssh16 != 0 && root16 != ssh16);
    CHECK(extracted(L"\\Office\\Server",FS_ICONFLAG_SMALL|FS_ICONFLAG_BACKGROUND) == ssh16);
    CHECK(extracted(L"\\Office\\Nested\\Another",FS_ICONFLAG_SMALL) == ssh16);
    CHECK(extracted(L"\\Office%2FServer",FS_ICONFLAG_SMALL) == ssh16);
    CHECK(extracted(L"\\[Настройки KiTTY-ng]",FS_ICONFLAG_SMALL) != ssh16);
    CHECK(extracted(L"\\Office\\Server",0) != 0);
    default_icon(L"\\Office",FS_ICONFLAG_SMALL);
    default_icon(L"\\Office\\Nested",FS_ICONFLAG_SMALL|FS_ICONFLAG_BACKGROUND);
    default_icon(L"\\AServer\\etc",FS_ICONFLAG_SMALL);
    default_icon(L"\\Office\\Server\\etc\\passwd",FS_ICONFLAG_SMALL|FS_ICONFLAG_BACKGROUND);
    default_icon(L"\\Missing",FS_ICONFLAG_SMALL);
    {
        char path[] = "\\Office\\Server";
        CHECK(api.icon_a(path,FS_ICONFLAG_SMALL,&ansi_icon) == FS_ICON_EXTRACTED_DESTROY);
        CHECK(ansi_icon != NULL);
        if (ansi_icon) { CHECK(icon_fingerprint(ansi_icon,16) == ssh16); CHECK(DestroyIcon(ansi_icon)); ansi_icon=NULL; }
    }
    CHECK(connects == 0 && GetFileAttributesW(marker) == INVALID_FILE_ATTRIBUTES);
    result = failures ? 1 : 0;
cleanup:
    if (shell_small) DestroyIcon(shell_small);
    if (shell_large) DestroyIcon(shell_large);
    if (ansi_icon) DestroyIcon(ansi_icon);
    if (module) FreeLibrary(module);
    remove_tree(extended_base);
    printf("Icons and virtual groups: %d checks, %d failures; no real SSH server used.\n",checks,failures);
    return result;
}
