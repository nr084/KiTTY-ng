/* Synthetic same-user Pageant named pipe: no real user's agent is contacted. */
#include "putty.h"
#include "pageant.h"
#include <stdio.h>
static HANDLE server, stop_event;
static int mode;
static ULONGLONG deadline;
static char pipename[128];
char *kg_test_agent_pipe_name(void) { return dupstr(pipename); }
int kg_engine_poll(void) { return GetTickCount64() < deadline; }
static DWORD WINAPI responder(void *unused)
{
    (void)unused;
    if (!ConnectNamedPipe(server,NULL) && GetLastError()!=ERROR_PIPE_CONNECTED) return 1;
    unsigned char buffer[64]; DWORD count=0;
    if (!ReadFile(server,buffer,sizeof(buffer),&count,NULL)) return 2;
    if (mode==1) { WaitForSingleObject(stop_event,2000); return 0; }
    unsigned char valid[]={0,0,0,1,5}, invalid[]={0x7f,0xff,0xff,0xff};
    unsigned char *reply=mode==2?invalid:valid;
    DWORD size=mode==2?4:5;
    /* Deliberately fragment both the header and body. */
    for(DWORD i=0;i<size;i++) {
        if(!WriteFile(server,reply+i,1,&count,NULL)) break;
        Sleep(2);
    }
    WaitForSingleObject(stop_event,2000);return 0;
}
int main(void)
{
    snprintf(pipename,sizeof(pipename),"\\\\.\\pipe\\kitty-ng-fs-test-%lu",(unsigned long)GetCurrentProcessId());
    int failures=0;
    for(mode=0;mode<3;mode++) {
        server=CreateNamedPipeA(pipename,PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,1,4096,4096,0,NULL);
        if(server==INVALID_HANDLE_VALUE)return 2;
        stop_event=CreateEvent(NULL,TRUE,FALSE,NULL);
        HANDLE thread=CreateThread(NULL,0,responder,NULL,0,NULL);
        if(!thread)return 2;
        strbuf *query=strbuf_new_for_agent_query(); put_byte(query,11);
        void *answer=NULL;int length=0;deadline=GetTickCount64()+200;
        agent_pending_query *pending=agent_query(query,&answer,&length,NULL,NULL);
        int ok=pending==NULL && (mode==0 ? length==5 && answer && ((unsigned char*)answer)[4]==5 : !answer && length==0);
        SetEvent(stop_event);if(WaitForSingleObject(thread,2000)!=WAIT_OBJECT_0)ok=0;
        CloseHandle(thread);CloseHandle(stop_event);CloseHandle(server);
        strbuf_free(query);sfree(answer);
        printf("AGENT mode=%d %s\n",mode,ok?"PASS":"FAIL");if(!ok)failures++;
    }
    return failures?1:0;
}
