/* Shared, profile-independent quick commands. No shell parsing or execution. */
#include "putty.h"
#include "misc.h"
#include "kittyng-quick-commands.h"
#include "../macos/names.h"
#include <ctype.h>
#include <errno.h>
#ifdef _WIN32
#include "kittyng-portable.h"
#else
#include "../macos/portable.h"
#include <unistd.h>
#include <sys/stat.h>
#endif

static bool fail(char **error, const char *text) {
    if (error) *error=dupstr(text);
    return false;
}
void kr_quick_free(KrQuickCommands *list) {
    for (size_t i=0;i<KR_QUICK_LIMIT;i++) {
        sfree(list->items[i].name);sfree(list->items[i].instruction);
    }
    memset(list,0,sizeof(*list));
}
static bool valid(const char *text, size_t limit) {
    return text && strlen(text)<=limit && !strpbrk(text,"\r\n") &&
        kr_name_valid_utf8((const unsigned char *)text,strlen(text));
}
static void assign(KrQuickCommands *list, const char *key, const char *value) {
    int index=0, used=0;
    if (sscanf(key,"cmd%d_%n",&index,&used)!=1 || !used || index<1 || index>KR_QUICK_LIMIT) return;
    KrQuickCommand *c=&list->items[index-1];const char *field=key+used;
    if (!strcmp(field,"name") && valid(value,KR_QUICK_NAME_LIMIT)) {
        sfree(c->name);c->name=dupstr(value);
    } else if (!strcmp(field,"instruction") && valid(value,KR_QUICK_TEXT_LIMIT)) {
        sfree(c->instruction);c->instruction=dupstr(value);
    } else if (!strcmp(field,"enter")) c->enter=!strcmp(value,"yes") || !strcmp(value,"1");
}
static bool section(const char *line) {
    while (*line==' ' || *line=='\t') line++;
    return !strncmp(line,"[QuickCommands]",15) &&
        (!line[15] || line[15]=='\n' || line[15]=='\r' || line[15]==' ' || line[15]=='\t');
}
static bool any_section(const char *line) {
    while (*line==' ' || *line=='\t') line++;
    return *line=='[';
}
static Filename *ini_path(void) {
#ifdef _WIN32
    const wchar_t *root=kr_portable_root();
    if (!root) return NULL;
    wchar_t *path=dupwcscat(root,L"\\kitty.ini");
    Filename *file=filename_from_wstr(path);sfree(path);return file;
#else
    const char *root=kr_mac_root();if (!root) return NULL;
    char *path=dupcat(root,"/kitty.ini");Filename *file=filename_from_str(path);sfree(path);return file;
#endif
}
static FILE *open_file(const Filename *file,const char *mode) {
#ifdef _WIN32
    wchar_t wide[8];MultiByteToWideChar(CP_UTF8,0,mode,-1,wide,lenof(wide));
    return _wfopen(file->wpath,wide);
#else
    return fopen(filename_to_str(file),mode);
#endif
}
static char *read_ini(const Filename *file,char **error) {
    FILE *fp=open_file(file,"rb");
    if (!fp) {
        if (errno==ENOENT) return dupstr("");
        fail(error,"Unable to read quick command settings.");return NULL;
    }
    strbuf *bytes=strbuf_new();char part[4096];size_t n;
    while ((n=fread(part,1,sizeof(part),fp)) && bytes->len<=4*1024*1024) put_data(bytes,part,n);
    bool ok=!ferror(fp) && bytes->len<=4*1024*1024;fclose(fp);
    char *text=ok?kr_text_from_bytes(bytes->u,bytes->len):NULL;strbuf_free(bytes);
    if (!text) fail(error,"Quick command settings are unreadable or too large.");
    return text;
}
static void parse(KrQuickCommands *list,char *text,bool *exists) {
    bool active=false;char *cursor=text;
    while (cursor && *cursor) {
        char *line=cursor,*end=strchr(cursor,'\n');if(end){*end=0;cursor=end+1;}else cursor=NULL;
        size_t len=strlen(line);if(len && line[len-1]=='\r')line[len-1]=0;
        if (any_section(line)) {active=section(line);if(active)*exists=true;continue;}
        if (!active) continue;
        while(*line==' ' || *line=='\t')line++;
        char *equal=strchr(line,'=');if (!equal || *line==';' || *line=='#')continue;
        *equal++=0;char *tail=line+strlen(line);while(tail>line && (tail[-1]==' ' || tail[-1]=='\t'))*--tail=0;
        assign(list,line,equal);
    }
}
static bool write_ini(const Filename *file,const KrQuickCommands *list,char **error) {
    char *old=read_ini(file,error);if(!old)return false;
    strbuf *text=strbuf_new();bool active=false;const char *p=old;
    while(*p){const char *end=strchr(p,'\n');size_t n=end?(size_t)(end-p)+1:strlen(p);
        if(any_section(p))active=section(p);
        if(!active)put_data(text,p,n);
        p+=n;
    }
    sfree(old);
    if(text->len && text->s[text->len-1]!='\n')put_byte(text,'\n');
    put_dataz(text,"[QuickCommands]\ninitialized=yes\n");
    for(int i=0;i<KR_QUICK_LIMIT;i++) {
        const KrQuickCommand *c=&list->items[i];if(!c->name || !*c->name || !c->instruction || !*c->instruction)continue;
        put_fmt(text,"cmd%d_name=%s\ncmd%d_instruction=%s\ncmd%d_enter=%s\n",i+1,c->name,i+1,c->instruction,i+1,c->enter?"yes":"no");
    }
    /* UTF-16LE keeps the whole INI readable by existing Win32 profile APIs.
     * The portable readers on Linux/macOS already accept this encoding. */
    strbuf *bytes=strbuf_new();put_byte(bytes,255);put_byte(bytes,254);
    for(const unsigned char *s=(const unsigned char *)text->s;*s;){
        unsigned ch=*s++;if(ch>=192){unsigned count=ch<224?1:ch<240?2:3;ch&=(1u<<(6-count))-1;while(count--)ch=(ch<<6)|(*s++&63);}
        if(ch>65535){ch-=65536;unsigned high=0xd800+(ch>>10);put_byte(bytes,high&255);put_byte(bytes,high>>8);ch=0xdc00+(ch&1023);}
        put_byte(bytes,ch&255);put_byte(bytes,ch>>8);
    }
    strbuf_free(text);
    bool ok=false;
#ifdef _WIN32
    wchar_t suffix[80];swprintf(suffix,lenof(suffix),L".quick-%lu-%llu.tmp",GetCurrentProcessId(),GetTickCount64());
    wchar_t *tmpname=dupwcscat(file->wpath,suffix);
    FILE *fp=_wfopen(tmpname,L"wbx");
    if(fp){ok=fwrite(bytes->s,1,bytes->len,fp)==bytes->len;if(fclose(fp))ok=false;
        if(ok)ok=MoveFileExW(tmpname,file->wpath,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!ok)DeleteFileW(tmpname);}
    sfree(tmpname);
#else
    char *tmpname=dupcat(filename_to_str(file),".quick-XXXXXX");int fd=mkstemp(tmpname);
    FILE *fp=fd<0?NULL:fdopen(fd,"wb");
    if(fp){ok=fwrite(bytes->s,1,bytes->len,fp)==bytes->len;if(fflush(fp)||fsync(fd))ok=false;if(fclose(fp))ok=false;
        if(ok)ok=!rename(tmpname,filename_to_str(file));if(!ok)unlink(tmpname);
    }else if(fd>=0){close(fd);unlink(tmpname);}sfree(tmpname);
#endif
    strbuf_free(bytes);
    return ok || fail(error,"Unable to save quick command settings.");
}
#ifdef _WIN32
static bool registry(KrQuickCommands *list,bool write,bool *exists,char **error) {
    HKEY key=NULL;LONG result=write?RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\KiTTY-NG\\QuickCommands",0,NULL,0,KEY_READ|KEY_WRITE,NULL,&key,NULL):RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\KiTTY-NG\\QuickCommands",0,KEY_READ,&key);
    if(result==ERROR_FILE_NOT_FOUND && !write)return true;
    if(result!=ERROR_SUCCESS)return fail(error,"Unable to access quick command settings.");
    *exists=true;bool ok=true;
    for(int i=0;i<KR_QUICK_LIMIT;i++)for(int field=0;field<3;field++) {
        const char *suffix=field==0?"name":field==1?"instruction":"enter";
        char name[40];snprintf(name,sizeof(name),"cmd%d_%s",i+1,suffix);wchar_t *wn=dup_mb_to_wc(CP_UTF8,name);
        if(write){KrQuickCommand *c=&list->items[i];const char *value=field==0?c->name:field==1?c->instruction:c->enter?"yes":"no";
            if(!c->name || !*c->name){LONG r=RegDeleteValueW(key,wn);ok&=r==ERROR_SUCCESS||r==ERROR_FILE_NOT_FOUND;}
            else {wchar_t *wv=dup_mb_to_wc(CP_UTF8,value?value:"");ok&=RegSetValueExW(key,wn,0,REG_SZ,(BYTE*)wv,(wcslen(wv)+1)*sizeof(wchar_t))==ERROR_SUCCESS;sfree(wv);}
        }else{DWORD type=0,size=0;if(RegQueryValueExW(key,wn,NULL,&type,NULL,&size)==ERROR_SUCCESS && type==REG_SZ && !(size % sizeof(wchar_t)) && size<=2*(KR_QUICK_TEXT_LIMIT+1)) {
            wchar_t *wv=snewn(size/2+1,wchar_t);memset(wv,0,size+2);
            if(RegQueryValueExW(key,wn,NULL,&type,(BYTE*)wv,&size)==ERROR_SUCCESS){char *value=dup_wc_to_mb(CP_UTF8,wv,"");assign(list,name,value);sfree(value);}sfree(wv);}}
        sfree(wn);
    }
    RegCloseKey(key);return ok || fail(error,"Unable to save quick command settings.");
}
#endif
static bool storage(KrQuickCommands *list,bool write,bool *exists,char **error) {
#ifdef _WIN32
    if(!kr_portable())return registry(list,write,exists,error);
#endif
    Filename *file=ini_path();if(!file)return fail(error,"Unable to locate quick command settings.");
    bool ok;
    if(write)ok=write_ini(file,list,error);
    else {char *text=read_ini(file,error);ok=text!=NULL;if(text){parse(list,text,exists);sfree(text);}}
    filename_free(file);return ok;
}
bool kr_quick_load(KrQuickCommands *list,const char *default_name,char **error) {
    memset(list,0,sizeof(*list));if(error)*error=NULL;bool exists=false;
    if(!storage(list,false,&exists,error)){kr_quick_free(list);return false;}
    if(!exists && default_name){list->items[0].name=dupstr(default_name);list->items[0].instruction=dupstr("apt -y update && apt -y upgrade && apt -y autoremove");list->items[0].enter=true;
        if(!storage(list,true,&exists,error)){kr_quick_free(list);return false;}}
    return true;
}
bool kr_quick_update(int index,const char *name,const char *instruction,bool enter,char **error) {
    if(error)*error=NULL;
    if(index<0 || index>=KR_QUICK_LIMIT || (name && (!*name || !instruction || !*instruction || !valid(name,KR_QUICK_NAME_LIMIT) || !valid(instruction,KR_QUICK_TEXT_LIMIT))))
        return fail(error,"Enter a name and a single-line command (name: 128 bytes, command: 65535 bytes maximum).");
    KrQuickCommands list;if(!kr_quick_load(&list,NULL,error))return false;
    KrQuickCommand *c=&list.items[index];sfree(c->name);sfree(c->instruction);
    c->name=name?dupstr(name):NULL;c->instruction=name?dupstr(instruction):NULL;c->enter=enter;
    bool exists=true,ok=storage(&list,true,&exists,error);kr_quick_free(&list);return ok;
}
