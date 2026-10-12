/* GUI prompts for bundled CLI tools: stdout pipe only, no secret arguments. */
#include "putty.h"
#include "helper.h"
#include "cli-language.h"
#include <mach-o/dyld.h>
#include <crt_externs.h>
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
bool kr_mac_gui_prompts(void){const char *value=getenv("KITTY_NG_GUI_PROMPTS");return value&&!strcmp(value,"1");}
void kr_mac_begin_tool(void){if(kr_mac_gui_prompts())setpgid(0,0);}
static char *gui_executable(void){uint32_t size=0;_NSGetExecutablePath(NULL,&size);char *path=snewn(size,char);if(_NSGetExecutablePath(path,&size)){sfree(path);return NULL;}char *bundle=strstr(path,".app/Contents/Helpers/");if(!bundle){sfree(path);return NULL;}bundle[strlen(".app/Contents/")]=0;char *program=dupcat(path,"MacOS/kitty-ng");sfree(path);return program;}
void kr_mac_exec_askpass(const char *question){char *program=gui_executable();if(program)execl(program,program,"--askpass","secret",question,(char*)NULL);sfree(program);}
char *kr_mac_dialog_text(SeatDialogText *text){strbuf *out=strbuf_new();for(size_t i=0;i<text->nitems;i++)if(text->items[i].type!=SDT_BATCH_ABORT){put_dataz(out,kr_mac_translate(text->items[i].text));put_dataz(out,"\n\n");}return strbuf_to_str(out);}
char *kr_mac_askpass(const char *mode,const char *question){
    char *program=gui_executable();if(!program)return NULL;
    struct stat st;if(lstat(program,&st)||!S_ISREG(st.st_mode)||access(program,X_OK)){sfree(program);return NULL;}
    int fd[2];if(pipe(fd)){sfree(program);return NULL;}fcntl(fd[0],F_SETFD,FD_CLOEXEC);fcntl(fd[1],F_SETFD,FD_CLOEXEC);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_adddup2(&actions,fd[1],STDOUT_FILENO);posix_spawn_file_actions_addclose(&actions,fd[0]);posix_spawn_file_actions_addclose(&actions,fd[1]);
    char *argv[]={program,"--askpass",(char*)mode,(char*)question,NULL};pid_t pid;
    int error=posix_spawn(&pid,program,&actions,NULL,argv,*_NSGetEnviron());posix_spawn_file_actions_destroy(&actions);close(fd[1]);sfree(program);
    if(error){close(fd[0]);return NULL;}
    strbuf *out=strbuf_new();char buffer[1024];bool valid=true;
    for(;;){ssize_t n=read(fd[0],buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<0){valid=false;break;}if(!n)break;if(out->len+(size_t)n>65536){valid=false;break;}put_data(out,buffer,n);}
    close(fd[0]);smemclr(buffer,sizeof(buffer));int status;pid_t waited;do{waited=waitpid(pid,&status,0);}while(waited<0&&errno==EINTR);
    valid=valid&&waited==pid&&WIFEXITED(status)&&!WEXITSTATUS(status)&&!memchr(out->s,0,out->len);
    char *result=valid?dupstr(out->s):NULL;smemclr(out->s,out->len);strbuf_free(out);return result;
}
