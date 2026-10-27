/* Exercise the actual WFX DLL's embedded SSH authentication and trust UI. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <stdint.h>
#include "../src/wfx.h"
typedef int (WINAPI *Init)(int,tProgressProcW,tLogProcW,tRequestProcW);
typedef HANDLE (WINAPI *First)(wchar_t *,WIN32_FIND_DATAW *);
typedef int (WINAPI *Close)(HANDLE);
typedef BOOL (WINAPI *Disconnect)(wchar_t *);
static unsigned trust, secret, errors;
static int accept_key, cancel_poll;
static int WINAPI progress(int n,wchar_t *a,wchar_t *b,int p)
{ (void)n;(void)a;(void)b;(void)p; return cancel_poll; }
static void WINAPI logline(int n,int type,wchar_t *text)
{
    (void)n; char out[16384];
    if (WideCharToMultiByte(CP_UTF8,0,text,-1,out,sizeof(out),NULL,NULL)) printf("LOG %d %s\n",type,out);
}
static BOOL WINAPI prompt(int n,int kind,wchar_t *title,wchar_t *text,wchar_t *out,int cap)
{
    (void)n;(void)title;
    if (out && cap) out[0]=0;
    if (kind==9) {
        trust++;
        if (!wcsstr(text,L"SHA256:")) { fprintf(stderr,"Missing host fingerprint\n"); return FALSE; }
        return accept_key;
    }
    if (kind==2) {
        secret++;
        const wchar_t *answer = wcsstr(text,L"passphrase") ? L"fixture-passphrase" : L"test-only-password";
        if ((int)wcslen(answer)+1>cap) return FALSE;
        wcscpy(out,answer); return TRUE;
    }
    if (kind==8) errors++;
    return FALSE;
}
static LONG CALLBACK fault(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE module=NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)e->ExceptionRecord->ExceptionAddress,&module);
    printf("FAULT code=%lx relative=%llx\n",e->ExceptionRecord->ExceptionCode,
        (unsigned long long)((uintptr_t)e->ExceptionRecord->ExceptionAddress-(uintptr_t)module));
    void *stack[32]; USHORT n=CaptureStackBackTrace(0,32,stack,NULL);
    for(USHORT i=0;i<n;i++) {
        HMODULE m=NULL; wchar_t name[MAX_PATH];
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)stack[i],&m);
        GetModuleFileNameW(m,name,MAX_PATH);
        printf("FRAME %llx %ls\n",(unsigned long long)((uintptr_t)stack[i]-(uintptr_t)m),name);
    }
    fflush(stdout);return EXCEPTION_CONTINUE_SEARCH;
}
int wmain(int argc,wchar_t **argv)
{
    if(argc!=7 && argc!=8) return 2;
    AddVectoredExceptionHandler(1,fault);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    accept_key=_wtoi(argv[2]); int expected=_wtoi(argv[3]);
    unsigned want_trust=(unsigned)_wtoi(argv[4]), want_secret=(unsigned)_wtoi(argv[5]);
    cancel_poll=_wtoi(argv[6]);
    wchar_t cwd[32768],after[32768],env[32768],env_after[32768];
    GetCurrentDirectoryW(32768,cwd); env[0]=env_after[0]=0;
    GetEnvironmentVariableW(L"KITTY_NG_PORTABLE_DIR",env,32768);
    HMODULE dll=LoadLibraryExW(argv[1],NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!dll) {printf("Load failure %lu\n",GetLastError());return 1;}
    Init init; First first; Close close; Disconnect disconnect;
    FARPROC f=GetProcAddress(dll,"FsInitW"); memcpy(&init,&f,sizeof(init));
    f=GetProcAddress(dll,"FsFindFirstW");memcpy(&first,&f,sizeof(first));
    f=GetProcAddress(dll,"FsFindClose");memcpy(&close,&f,sizeof(close));
    f=GetProcAddress(dll,"FsDisconnectW");memcpy(&disconnect,&f,sizeof(disconnect));
    if(!init||!first||!close||!disconnect)return 1;
    init(37,progress,logline,prompt);
    WIN32_FIND_DATAW data;
    wchar_t path[1024]=L"\\TestServer\\";
    if(argc==8) { if(wcslen(argv[7])>=1024)return 2; wcscpy(path,argv[7]); }
    ULONGLONG started=GetTickCount64();
    HANDLE h=first(path,&data);
    int connected=h!=INVALID_HANDLE_VALUE || GetLastError()==ERROR_NO_MORE_FILES;
    if(h!=INVALID_HANDLE_VALUE)close(h);
    disconnect(path); FreeLibrary(dll);
    GetCurrentDirectoryW(32768,after);
    GetEnvironmentVariableW(L"KITTY_NG_PORTABLE_DIR",env_after,32768);
    int ok=connected==expected && trust==want_trust && secret==want_secret &&
           !wcscmp(cwd,after) && !wcscmp(env,env_after) && GetTickCount64()-started<15000;
    printf("AUTH connected=%d expected=%d trust=%u/%u secrets=%u/%u errors=%u elapsed=%llu result=%s\n",
        connected,expected,trust,want_trust,secret,want_secret,errors,
        (unsigned long long)(GetTickCount64()-started),ok?"PASS":"FAIL");
    return ok?0:1;
}
