#ifndef KG_WFX_H
#define KG_WFX_H
#include <windows.h>

/* C declarations of the documented Total Commander filesystem ABI.
 * https://ghisler.github.io/WFX-SDK/contents.htm */
#define FS_FILE_OK 0
#define FS_FILE_EXISTS 1
#define FS_FILE_NOTFOUND 2
#define FS_FILE_READERROR 3
#define FS_FILE_WRITEERROR 4
#define FS_FILE_USERABORT 5
#define FS_FILE_NOTSUPPORTED 6
#define FS_EXEC_OK 0
#define FS_EXEC_ERROR 1
#define FS_EXEC_YOURSELF (-1)
#define FS_EXEC_SYMLINK (-2)
#define FS_COPYFLAGS_OVERWRITE 1
#define FS_COPYFLAGS_RESUME 2
#define FS_COPYFLAGS_MOVE 4
#define FS_ICONFLAG_SMALL 1
#define FS_ICONFLAG_BACKGROUND 2
#define FS_ICON_USEDEFAULT 0
#define FS_ICON_EXTRACTED 1
#define FS_ICON_EXTRACTED_DESTROY 2
#define FS_ICON_DELAYED 3
#define MSGTYPE_CONNECT 1
#define MSGTYPE_DISCONNECT 2
#define MSGTYPE_DETAILS 3
#define MSGTYPE_TRANSFERCOMPLETE 4
#define MSGTYPE_CONNECTCOMPLETE 5
#define MSGTYPE_IMPORTANTERROR 6
#define MSGTYPE_OPERATIONCOMPLETE 7

typedef struct { DWORD SizeLow, SizeHigh; FILETIME LastWriteTime; int Attr; } RemoteInfoStruct;
typedef struct {
    int size; DWORD PluginInterfaceVersionLow, PluginInterfaceVersionHi;
    char DefaultIniName[MAX_PATH];
} FsDefaultParamStruct;
typedef int (WINAPI *tProgressProc)(int, char *, char *, int);
typedef int (WINAPI *tProgressProcW)(int, wchar_t *, wchar_t *, int);
typedef void (WINAPI *tLogProc)(int, int, char *);
typedef void (WINAPI *tLogProcW)(int, int, wchar_t *);
typedef BOOL (WINAPI *tRequestProc)(int, int, char *, char *, char *, int);
typedef BOOL (WINAPI *tRequestProcW)(int, int, wchar_t *, wchar_t *, wchar_t *, int);
#endif
