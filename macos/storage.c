/* macOS adapter for original KiTTY portable storage. Does not use ~/.putty.
 * Unknown profile lines are retained byte-for-byte; Mac fonts have separate keys.
 */
#include "putty.h"
#include "storage.h"
#include "ssh.h"
#include "portable.h"
#include "../windows/kittyng-legacy-crypt.h"
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include "names.h"

static char *duplicate_part(const char *text,size_t n){char *copy=snewn(n+1,char);memcpy(copy,text,n);copy[n]=0;return copy;}
typedef struct MacValue { char *key, *value; struct MacValue *next; bool written; } MacValue;
struct settings_r { strbuf *data; char *path; };
struct settings_w { settings_r *old; MacValue *values; char *path; };
struct settings_e { char **names; size_t count, index; };
struct host_ca_enum { settings_e *entries; };
static char *root;
static bool profile_write_ok;
static const char *hex = "0123456789ABCDEF";
static char *join(const char *parent, const char *leaf) { return dupcat(parent,"/",leaf); }
static bool inside_bundle(const char *path){return strstr(path,".app/")||strlen(path)>=4&&!strcmp(path+strlen(path)-4,".app");}
void kr_mac_set_root(const char *path) {
    char *resolved=realpath(path,NULL); if (!resolved) return; if(inside_bundle(resolved)){free(resolved);return;}
    sfree(root); root=dupstr(resolved); free(resolved);
    setenv("KITTY_NG_PORTABLE_DIR",root,1);
}
const char *kr_mac_root(void) {
    if (root) return root;
    const char *chosen=getenv("KITTY_NG_PORTABLE_DIR");
    int argc=*_NSGetArgc(); char **argv=*_NSGetArgv();
    for (int i=1;i+1<argc;i++) if (!strcmp(argv[i],"--portable-dir")) chosen=argv[++i];
    if (chosen && *chosen) {kr_mac_set_root(chosen);return root;}
    if (!root) {
        uint32_t size=0; _NSGetExecutablePath(NULL,&size); char *path=snewn(size,char);
        if (!_NSGetExecutablePath(path,&size)) {
            char *bundle=strstr(path,".app/Contents/");
            if (bundle) { char *slash=bundle; while (slash>path && slash[-1]!='/') slash--; if(slash>path) slash[-1]=0; }
            else { char *slash=strrchr(path,'/'); if(slash) *slash=0; }
            kr_mac_set_root(path);
        }
        sfree(path);
    }
    return root;
}
static char *read_ini_file(const char *path){FILE *fp=fopen(path,"rb");if(!fp)return NULL;strbuf *data=strbuf_new();unsigned char chunk[4096];size_t n;bool valid=true;while(!feof(fp)&&!ferror(fp)&&(n=fread(chunk,1,sizeof(chunk),fp))){if(data->len+n>1048576){valid=false;break;}put_data(data,chunk,n);}valid=valid&&!ferror(fp);fclose(fp);char *text=valid?kr_text_from_bytes(data->u,data->len):NULL;strbuf_free(data);return text;}
static char *ini_at(const char *base,const char *section,const char *name,const char *fallback) {
    if(!base)return dupstr(fallback);
    char *path=join(base,"kitty.ini"),*text=read_ini_file(path),*cursor=text;sfree(path);
    bool active=false; char *result=dupstr(fallback), *line;
    while(cursor && (line=strsep(&cursor,"\n"))) {
        char *p=line;while(isspace((unsigned char)*p))p++;
        char *end=p+strlen(p);while(end>p && isspace((unsigned char)end[-1]))*--end=0;
        if(*p=='[' && end>p+1 && end[-1]==']'){end[-1]=0;active=!strcasecmp(p+1,section);}
        else if(active && *p!=';' && *p!='#') {
            char *equal=strchr(p,'='); if(equal){*equal++=0;char *k=p+strlen(p);while(k>p&&isspace((unsigned char)k[-1]))*--k=0;
                while(isspace((unsigned char)*equal))equal++;if(!strcasecmp(p,name)){sfree(result);result=dupstr(equal);}}
        }
    }
    sfree(text);return result;
}
char *kr_mac_ini(const char *section,const char *name,const char *fallback) {return ini_at(kr_mac_root(),section,name,fallback);}
static char *decode(const char *value,size_t len) {
    strbuf *out=strbuf_new();
    for(size_t i=0;i<len;i++) {
        if(value[i]=='%'&&i+2<len&&isxdigit((unsigned char)value[i+1])&&isxdigit((unsigned char)value[i+2])) {
            char digits[]={value[i+1],value[i+2],0};unsigned byte=strtoul(digits,NULL,16);
            if(!byte){strbuf_free(out);return NULL;}put_byte(out,byte);i+=2;
        }else put_byte(out,value[i]);
    }
    char *result=dupstr(out->s);strbuf_free(out);return result;
}
char *kr_mac_decode_name(const char *value){char *raw=decode(value,strlen(value));if(!raw)return NULL;char *utf8=kr_name_from_bytes(raw);sfree(raw);return utf8;}
static char *encode(const char *value) {
    strbuf *out=strbuf_new();
    for(const unsigned char *p=(const unsigned char *)value;*p;p++)
        if(*p<=32||*p>=127||strchr("\\*?:/\"<>|%",*p))put_fmt(out,"%%%02X",*p);else put_byte(out,*p);
    char *result=dupstr(out->s);strbuf_free(out);return result;
}
static char *component(const char *value) {
    strbuf *out=strbuf_new();
    for(const unsigned char *p=(const unsigned char *)value;*p;p++)
        if(*p<32||strchr("<>:\"/\\|",*p)||(*p=='.'&&(p==(const unsigned char*)value||!p[1])))put_fmt(out,"%%%02X",*p);else put_byte(out,*p);
    char *result=dupstr(out->s);strbuf_free(out);return result;
}
static bool safe_name(const char *name) {
    if(!name || !*name)return false;
    return strcmp(name,".")&&strcmp(name,"..")&&!strchr(name,'\\')&&!strchr(name,'/');
}
static char *configuration(const char *base,const char *key,const char *fallback){
    char *path=join(base,"putty.conf"),*result=dupstr(fallback),*line,*text=read_ini_file(path),*cursor=text;sfree(path);
    while(cursor&&(line=strsep(&cursor,"\n"))){char *p=line;while(isspace((unsigned char)*p))p++;char *end=p+strlen(p);while(end>p&&isspace((unsigned char)end[-1]))*--end=0;char *eq=strchr(p,'=');if(eq&&*p!=';'&&*p!='#'){*eq++=0;end=p+strlen(p);while(end>p&&isspace((unsigned char)end[-1]))*--end=0;if(!strcmp(p,key)){sfree(result);result=dupstr(eq);}}}sfree(text);return result;
}
char *kr_mac_directory_at(const char *base,const char *key){
    if(!base)return NULL;char *value=configuration(base,!strcmp(key,"seedfile")?"macseedfile":key,!strcmp(key,"seedfile")?"kitty-ng-macos.rnd":key);
    if(isalpha((unsigned char)value[0])&&value[1]==':'){sfree(value);return NULL;}
    for(char *p=value;*p;p++)if(*p=='\\')*p='/';char *result=value[0]=='/'?dupstr(value):join(base,value);sfree(value);char *resolved=realpath(result,NULL);bool forbidden=inside_bundle(result)||(resolved&&inside_bundle(resolved));free(resolved);if(forbidden){sfree(result);return NULL;}return result;
}
static char *suffix_at(const char *base,bool hostkey){char *ini=ini_at(base,"KiTTY",hostkey?"hostkeyextension":"fileextension","");char *suffix=configuration(base,hostkey?"keysuffix":"sessionsuffix",ini);sfree(ini);if(strpbrk(suffix,"/\\:<>\"|")){sfree(suffix);return dupstr("");}return suffix;}
char *kr_mac_session_name_at(const char *base,const char *filename){
    if(!base||!safe_name(filename))return NULL;
    char *suffix=suffix_at(base,false);size_t n=strlen(filename),k=strlen(suffix);char *result=NULL;
    if(n>k&&(!k||!strcmp(filename+n-k,suffix))){char *stem=duplicate_part(filename,n-k);result=kr_mac_decode_name(stem);sfree(stem);}
    sfree(suffix);return result;
}
static char *session_path(const char *name) {
    const char *base=kr_mac_root();if(!base || strstr(base,"/AppTranslocation/"))return NULL;
    if(!name||!*name)name="Default Settings";
    char *path=kr_mac_directory_at(base,"Sessions"),*copy=dupstr(name),*part,*cursor=copy;
    if(!path){sfree(copy);return NULL;}
    while((part=strsep(&cursor,"/"))) {
        if(!safe_name(part)){sfree(copy);sfree(path);return NULL;}
        char *encoded=encode(part),*suffix=cursor?dupstr(""):suffix_at(base,false),*leaf=dupcat(encoded,suffix),*next=join(path,leaf);sfree(leaf);sfree(encoded);
        struct stat st;
        if(lstat(next,&st)<0) {char *raw_leaf=dupcat(part,suffix),*raw=join(path,raw_leaf);sfree(raw_leaf);if(!lstat(raw,&st)){sfree(next);next=raw;}else sfree(raw);}
        if(lstat(next,&st)<0){DIR *directory=opendir(path);struct dirent *entry;while(directory&&(entry=readdir(directory))){char *logical=cursor?kr_mac_decode_name(entry->d_name):kr_mac_session_name_at(base,entry->d_name);bool match=logical&&!strcmp(logical,part);sfree(logical);if(match){char *existing=join(path,entry->d_name);sfree(next);next=existing;break;}}if(directory)closedir(directory);}
        if(!lstat(next,&st)&&S_ISLNK(st.st_mode)){sfree(suffix);sfree(next);sfree(copy);sfree(path);return NULL;}
        sfree(suffix);sfree(path);path=next;
    }
    sfree(copy);return path;
}
char *kr_mac_profile_path(const char *name){return session_path(name);}
bool kr_mac_write_succeeded(void){return profile_write_ok;}
static bool recognised_data(strbuf *data){
    if(memchr(data->s,0,data->len))return false;
    for(size_t start=0;start<data->len;){size_t end=start;while(end<data->len&&data->s[end]!='\n')end++;size_t trim=end;while(trim>start&&data->s[trim-1]=='\r')trim--;
        const char *separator=memchr(data->s+start,'\\',trim-start);
        if(separator&&separator>data->s+start&&separator<data->s+trim-1&&data->s[trim-1]=='\\')return true;start=end+1;}
    return false;
}
static settings_r *read_path(const char *path) {
    if(!path)return NULL;struct stat st;
    if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size>16777216)return NULL;
    FILE *fp=fopen(path,"rb");if(!fp)return NULL;
    strbuf *data=strbuf_new();char chunk[4096];size_t n;
    bool valid=true;while(!feof(fp)&&!ferror(fp)&&(n=fread(chunk,1,sizeof(chunk),fp))){if(data->len+n>16777216){valid=false;break;}put_data(data,chunk,n);}
    bool ok=valid&&!ferror(fp);fclose(fp);if(!ok){strbuf_free(data);return NULL;}
    settings_r *r=snew(settings_r);r->data=data;r->path=dupstr(path);return r;
}
settings_r *open_settings_r(const char *name) {
    char *path=session_path(name);settings_r *r=read_path(path);sfree(path);
    if(r&&!recognised_data(r->data)){close_settings_r(r);return NULL;}
    return r;
}
settings_r *kr_mac_copy_reader(settings_r *base){settings_r *r=snew(settings_r);r->data=strbuf_new();r->path=dupstr(base?base->path:"");if(base)put_data(r->data,base->data->s,base->data->len);return r;}
void kr_mac_override_read(settings_r *r,const char *key,const char *value){char *encoded=encode(value);put_fmt(r->data,"\n%s\\%s\\\n",key,encoded);sfree(encoded);}
char *read_setting_s(settings_r *r,const char *key) {
    if(!r)return NULL;size_t keylen=strlen(key);char *result=NULL;
    for(size_t start=0;start<r->data->len;) {
        size_t end=start;while(end<r->data->len&&r->data->s[end]!='\n')end++;
        size_t trimmed=end;while(trimmed>start&&(r->data->s[trimmed-1]=='\r'||r->data->s[trimmed-1]=='\n'))trimmed--;
        if(trimmed>start+keylen+1&&!memcmp(r->data->s+start,key,keylen)&&r->data->s[start+keylen]=='\\'&&r->data->s[trimmed-1]=='\\') {
            char *value=decode(r->data->s+start+keylen+1,trimmed-start-keylen-2);if(value){sfree(result);result=value;}
        }
        start=end+1;
    }return result;
}
int read_setting_i(settings_r *r,const char *key,int fallback) {
    char *value=read_setting_s(r,key);if(!value)return fallback;
    char *end;errno=0;long n=strtol(value,&end,10);int result=*value&&!*end&&!errno&&n>=INT_MIN&&n<=INT_MAX?(int)n:fallback;sfree(value);return result;
}
void close_settings_r(settings_r *r){if(r){strbuf_free(r->data);sfree(r->path);sfree(r);}}
char *kr_mac_asset_path(const char *stored) {
    if(!*stored)return dupstr("");if(stored[0]=='/'||(isalpha((unsigned char)stored[0])&&stored[1]==':'))return dupstr(stored);
    char *copy=dupstr(stored);for(char *p=copy;*p;p++)if(*p=='\\')*p='/';
    char *path=kr_mac_root()?join(kr_mac_root(),copy):dupstr(copy);sfree(copy);return path;
}
static bool asset_key(const char *key){return !strcmp(key,"PublicKeyFile")||!strcmp(key,"DetachedCertificate")||!strcmp(key,"Scriptfile")||!strcmp(key,"IconeFile");}
Filename *read_setting_filename(settings_r *r,const char *key){char *value=read_setting_s(r,key);if(!value)return NULL;char *path=asset_key(key)?kr_mac_asset_path(value):dupstr(value);Filename *f=filename_from_str(path);sfree(path);sfree(value);return f;}
FontSpec *read_setting_fontspec(settings_r *r,const char *key){char *name=dupcat("Mac",key,"Name"),*value=read_setting_s(r,name);sfree(name);if(!value)return NULL;FontSpec *f=fontspec_new(value);sfree(value);return f;}
settings_w *open_settings_w(const char *name,char **error){
    profile_write_ok=false;*error=NULL;char *path=session_path(name);if(!path){*error=dupstr("Select a writable portable directory first");return NULL;}
    settings_r *old=read_path(path);if(old&&old->data->len&&!recognised_data(old->data)){close_settings_r(old);sfree(path);*error=dupstr("Unrecognised profiles are not overwritten");return NULL;}
    settings_w *w=snew(settings_w);w->old=old;w->path=path;w->values=NULL;return w;
}
void write_setting_s(settings_w *w,const char *key,const char *value){MacValue **p=&w->values;while(*p&&strcmp((*p)->key,key))p=&(*p)->next;if(!*p){*p=snew(MacValue);(*p)->key=dupstr(key);(*p)->value=NULL;(*p)->next=NULL;(*p)->written=false;}sfree((*p)->value);(*p)->value=dupstr(value);}
void write_setting_i(settings_w *w,const char *key,int value){char *s=dupprintf("%d",value);write_setting_s(w,key,s);sfree(s);}
void write_setting_fontspec(settings_w *w,const char *key,FontSpec *font){char *name=dupcat("Mac",key,"Name");write_setting_s(w,name,font->name);sfree(name);}
void write_setting_filename(settings_w *w,const char *key,Filename *f){
    const char *path=filename_to_str(f),*base=kr_mac_root();char *canonical=asset_key(key)&&*path?realpath(path,NULL):NULL;if(canonical)path=canonical;
    size_t n=base?strlen(base):0;if(asset_key(key)&&n&&!strncmp(base,path,n)&&path[n]=='/'&&!strstr(path,"/../"))path+=n+1;write_setting_s(w,key,path);free(canonical);
}
static bool atomic_write(const char *path,ptrlen data){
    char *parent=dupstr(path),*slash=strrchr(parent,'/');if(slash)*slash=0;
    char *error=make_dir_path(parent,0700);sfree(parent);if(error){sfree(error);return false;}
    char *temporary=dupcat(path,".tmpXXXXXX");int fd=mkstemp(temporary);if(fd<0){sfree(temporary);return false;}
    FILE *fp=fdopen(fd,"wb");bool ok=fp&&fwrite(data.ptr,1,data.len,fp)==data.len&&fflush(fp)==0&&fsync(fd)==0;
    if(fp){if(fclose(fp))ok=false;}else close(fd);
    if(ok)ok=rename(temporary,path)==0;if(!ok)unlink(temporary);sfree(temporary);return ok;
}
void close_settings_w(settings_w *w){
    strbuf *out=strbuf_new();
    if(w->old)for(size_t start=0;start<w->old->data->len;){size_t end=start;while(end<w->old->data->len&&w->old->data->s[end]!='\n')end++;size_t next=end+(end<w->old->data->len);MacValue *match=NULL;
        for(MacValue *p=w->values;p;p=p->next){size_t n=strlen(p->key);if(end>start+n&&!memcmp(w->old->data->s+start,p->key,n)&&w->old->data->s[start+n]=='\\'){match=p;break;}}
        if(match){if(!match->written){char *encoded=encode(match->value);put_fmt(out,"%s\\%s\\\r\n",match->key,encoded);sfree(encoded);match->written=true;}}else put_data(out,w->old->data->s+start,next-start);start=next;}
    for(MacValue *p=w->values;p;p=p->next)if(!p->written){if(out->len&&out->s[out->len-1]!='\n')put_byte(out,'\n');char *encoded=encode(p->value);put_fmt(out,"%s\\%s\\\r\n",p->key,encoded);sfree(encoded);}
    bool unchanged=true;settings_r *current=read_path(w->path);if(w->old)unchanged=current&&ptrlen_eq_ptrlen(ptrlen_from_strbuf(current->data),ptrlen_from_strbuf(w->old->data));else unchanged=!current;
    profile_write_ok=unchanged&&atomic_write(w->path,ptrlen_from_strbuf(out));if(!profile_write_ok)nonfatal("Unable to save profile: changed file or write failure");
    close_settings_r(current);close_settings_r(w->old);strbuf_free(out);while(w->values){MacValue *p=w->values;w->values=p->next;smemclr(p->value,strlen(p->value));sfree(p->value);sfree(p->key);sfree(p);}sfree(w->path);sfree(w);
}
void del_settings(const char *name){char *p=session_path(name);if(p){unlink(p);sfree(p);}}
static void collect(settings_e *e,const char *path,const char *prefix){DIR *d=opendir(path);if(!d)return;struct dirent *entry;
    while((entry=readdir(d))){if(entry->d_name[0]=='.'||strstr(entry->d_name,".tmp"))continue;char *full=join(path,entry->d_name);struct stat st;
        if(!lstat(full,&st)&&(S_ISDIR(st.st_mode)||S_ISREG(st.st_mode))){char *decoded=S_ISDIR(st.st_mode)?kr_mac_decode_name(entry->d_name):kr_mac_session_name_at(kr_mac_root(),entry->d_name);
            if(decoded){char *id=*prefix?dupcat(prefix,"/",decoded):dupstr(decoded);if(S_ISDIR(st.st_mode))collect(e,full,id);else{e->names=sresize(e->names,e->count+1,char*);e->names[e->count++]=dupstr(id);}sfree(id);sfree(decoded);}}
        sfree(full);}closedir(d);}
settings_e *enum_settings_start(void){settings_e *e=snew(settings_e);e->names=NULL;e->count=e->index=0;const char *base=kr_mac_root();if(base){char *path=kr_mac_directory_at(base,"Sessions");if(path)collect(e,path,"");sfree(path);}return e;}
bool enum_settings_next(settings_e *e,strbuf *out){if(!e||e->index>=e->count)return false;put_dataz(out,e->names[e->index++]);return true;}
void enum_settings_finish(settings_e *e){if(e){for(size_t i=0;i<e->count;i++)sfree(e->names[i]);sfree(e->names);sfree(e);}}
void escape_registry_key(const char *in, strbuf *out);
static char *host_path(const char *host,int port,const char *type){strbuf *name=strbuf_new();put_fmt(name,"%s@%d:",type,port);escape_registry_key(host,name);char *encoded=component(name->s),*suffix=suffix_at(kr_mac_root(),true),*leaf=dupcat(encoded,suffix),*dir=kr_mac_directory_at(kr_mac_root(),"SshHostKeys"),*path=dir?join(dir,leaf):NULL;strbuf_free(name);sfree(encoded);sfree(suffix);sfree(dir);sfree(leaf);return path;}
int check_stored_host_key(const char *host,int port,const char *type,const char *key){if(!kr_mac_root())return 1;char *path=host_path(host,port,type);FILE *fp=path?fopen(path,"rb"):NULL;sfree(path);if(!fp)return 1;char *old=fgetline(fp);fclose(fp);if(!old)return 2;size_t n=strlen(old);while(n&&isspace((unsigned char)old[n-1]))old[--n]=0;int result=!strcmp(old,key)?0:2;sfree(old);return result;}
bool have_ssh_host_key(const char *host,int port,const char *type){return check_stored_host_key(host,port,type,"")!=1;}
void store_host_key(Seat *seat,const char *host,int port,const char *type,const char *key){if(!kr_mac_root()){seat_nonfatal(seat,"Portable directory is unavailable");return;}char *path=host_path(host,port,type);if(!path||!atomic_write(path,ptrlen_from_asciz(key)))seat_nonfatal(seat,"Unable to write portable host key");sfree(path);}
static char *ca_path(const char *name){if(!safe_name(name)||name[0]=='.'||strlen(name)>4096)return NULL;char *directory=kr_mac_directory_at(kr_mac_root(),"SshHostCAs");if(!directory)return NULL;char *leaf=encode(name),*path=join(directory,leaf);sfree(directory);sfree(leaf);struct stat st;if(!lstat(path,&st)&&!S_ISREG(st.st_mode)){sfree(path);return NULL;}return path;}
host_ca_enum *enum_host_ca_start(void){host_ca_enum *e=snew(host_ca_enum);e->entries=snew(settings_e);e->entries->names=NULL;e->entries->count=e->entries->index=0;char *directory=kr_mac_directory_at(kr_mac_root(),"SshHostCAs");DIR *d=directory?opendir(directory):NULL;struct dirent *entry;while(d&&(entry=readdir(d))){if(entry->d_name[0]=='.'||strstr(entry->d_name,".tmp"))continue;char *path=join(directory,entry->d_name);struct stat st;if(!lstat(path,&st)&&S_ISREG(st.st_mode)){char *name=kr_mac_decode_name(entry->d_name);if(name){e->entries->names=sresize(e->entries->names,e->entries->count+1,char*);e->entries->names[e->entries->count++]=name;}}sfree(path);}if(d)closedir(d);sfree(directory);return e;}
bool enum_host_ca_next(host_ca_enum *e,strbuf *out){return e&&enum_settings_next(e->entries,out);}
void enum_host_ca_finish(host_ca_enum *e){if(e){enum_settings_finish(e->entries);sfree(e);}}
static char *ca_value(settings_r *reader,const char *key){char *value=read_setting_s(reader,key);if(value||!reader)return value;size_t n=strlen(key);for(size_t start=0;start<reader->data->len;){size_t end=start;while(end<reader->data->len&&reader->data->s[end]!='\n')end++;size_t trim=end;while(trim>start&&reader->data->s[trim-1]=='\r')trim--;if(trim>start+n&&reader->data->s[start+n]=='='&&!memcmp(reader->data->s+start,key,n))return !strcmp(key,"Validity")?decode(reader->data->s+start+n+1,trim-start-n-1):duplicate_part(reader->data->s+start+n+1,trim-start-n-1);start=end+1;}return NULL;}
host_ca *host_ca_load(const char *name){char *path=ca_path(name);settings_r *r=read_path(path);sfree(path);if(!r)return NULL;char *key=ca_value(r,"PublicKey"),*validity=ca_value(r,"Validity");if(!key||!base64_valid(ptrlen_from_asciz(key))||!validity||!*validity){sfree(key);sfree(validity);close_settings_r(r);return NULL;}host_ca *ca=host_ca_new();ca->name=dupstr(name);ca->ca_public_key=base64_decode_sb(ptrlen_from_asciz(key));ca->validity_expression=validity;const char *keys[]={"PermitRSASHA1","PermitRSASHA256","PermitRSASHA512"};bool *flags[]={&ca->opts.permit_rsa_sha1,&ca->opts.permit_rsa_sha256,&ca->opts.permit_rsa_sha512};for(size_t i=0;i<lenof(keys);i++){char *v=ca_value(r,keys[i]);if(v)*flags[i]=!strcmp(v,"1");sfree(v);}sfree(key);close_settings_r(r);return ca;}
char *host_ca_save(host_ca *ca){char *path=ca_path(ca->name);if(!path)return dupstr("CA record must have a valid portable name");char *failure=NULL;ptrlen location;bool valid=ca->validity_expression&&*ca->validity_expression&&cert_expr_valid(ca->validity_expression,&failure,&location);if(!valid){sfree(path);return failure?failure:dupstr("Host matching expression is required");}const ssh_keyalg *alg=pubkey_blob_to_alg(ptrlen_from_strbuf(ca->ca_public_key));ssh_key *key=alg?ssh_key_new_pub(alg,ptrlen_from_strbuf(ca->ca_public_key)):NULL;if(!key){sfree(path);return dupstr("Invalid SSH CA public key");}ssh_key_free(key);settings_w *w=snew(settings_w);w->old=read_path(path);w->path=path;w->values=NULL;strbuf *encoded=base64_encode_sb(ptrlen_from_strbuf(ca->ca_public_key),0);write_setting_s(w,"PublicKey",encoded->s);strbuf_free(encoded);write_setting_s(w,"Validity",ca->validity_expression);write_setting_i(w,"PermitRSASHA1",ca->opts.permit_rsa_sha1);write_setting_i(w,"PermitRSASHA256",ca->opts.permit_rsa_sha256);write_setting_i(w,"PermitRSASHA512",ca->opts.permit_rsa_sha512);close_settings_w(w);return kr_mac_write_succeeded()?NULL:dupstr("Unable to save SSH CA record");}
char *host_ca_delete(const char *name){char *path=ca_path(name);if(!path)return dupstr("Invalid SSH CA name");bool ok=!unlink(path);sfree(path);return ok?NULL:dupstr("Unable to delete SSH CA record");}
void provide_xrm_string(const char *string,const char *progname){}
void read_random_seed(noise_consumer_t consume){if(!kr_mac_root())return;char *path=kr_mac_directory_at(kr_mac_root(),"seedfile");FILE *fp=path?fopen(path,"rb"):NULL;sfree(path);if(!fp)return;char buf[4096];size_t n;while(!feof(fp)&&!ferror(fp)&&(n=fread(buf,1,sizeof(buf),fp)))consume(buf,n);fclose(fp);smemclr(buf,sizeof(buf));}
void write_random_seed(void *data,int len){if(!kr_mac_root()||strstr(kr_mac_root(),"/AppTranslocation/"))return;char *path=kr_mac_directory_at(kr_mac_root(),"seedfile");if(path)atomic_write(path,make_ptrlen(data,len));sfree(path);}
void cleanup_all(void){} /* Never recursively remove the shared Windows/macOS portable directory. */
static void load_proxy(Conf *conf){
    conf_set_str(conf,CONF_kr_proxy_error,"");const char *name=conf_get_str(conf,CONF_proxyselection);
    if(!*name||!strcmp(name,"- Session defined proxy -"))return;
    if(!strcmp(name,"- No proxy -")){conf_set_int(conf,CONF_proxy_type,PROXY_NONE);return;}
    char *directory=kr_mac_directory_at(kr_mac_root(),"Proxies");if(!directory){conf_set_str(conf,CONF_kr_proxy_error,"Selected proxy directory is unavailable; connection is blocked.");return;}char *leaf=encode(name),*path=join(directory,leaf);sfree(directory);sfree(leaf);settings_r *reader=read_path(path);sfree(path);
    int stored=read_setting_i(reader,"ProxyMethod",-1),type=0;bool valid=reader&&stored>=0&&stored<=5&&conf_enum_map_from_storage(conf_key_info[CONF_proxy_type].storage_enum,stored,&type);
    if(valid){conf_set_int(conf,CONF_proxy_type,type);conf_set_bool(conf,CONF_even_proxy_localhost,false);conf_set_int(conf,CONF_proxy_dns,AUTO);conf_set_int(conf,CONF_proxy_log_to_term,AUTO);
        const struct{const char *key;int option;} fields[]={{"ProxyHost",CONF_proxy_host},{"ProxyUsername",CONF_proxy_username},{"ProxyPassword",CONF_proxy_password},{"ProxyExcludeList",CONF_proxy_exclude_list},{"ProxyTelnetCommand",CONF_proxy_telnet_command}};
        for(size_t i=0;i<lenof(fields);i++){char *value=read_setting_s(reader,fields[i].key);conf_set_str(conf,fields[i].option,value?value:"");if(value){smemclr(value,strlen(value));sfree(value);}}
        if(type!=PROXY_NONE&&type!=PROXY_CMD){int port=read_setting_i(reader,"ProxyPort",0);valid=*conf_get_str(conf,CONF_proxy_host)&&port>0&&port<=65535;if(valid)conf_set_int(conf,CONF_proxy_port,port);}
        if((type==PROXY_CMD||type==PROXY_TELNET)&&!*conf_get_str(conf,CONF_proxy_telnet_command))valid=false;
        conf_set_bool(conf,CONF_even_proxy_localhost,read_setting_i(reader,"ProxyLocalhost",0)!=0);
        int dns=read_setting_i(reader,"ProxyDNS",-1),mapped;if(dns>=0){if(conf_enum_map_from_storage(conf_key_info[CONF_proxy_dns].storage_enum,dns,&mapped))conf_set_int(conf,CONF_proxy_dns,mapped);else valid=false;}
    }
    close_settings_r(reader);if(!valid)conf_set_str(conf,CONF_kr_proxy_error,"Selected proxy profile is missing or invalid; connection is blocked.");
}
static int ini_milliseconds(const char *key,int fallback){char *text=kr_mac_ini("KiTTY",key,""),*end;double number=strtod(text,&end);int result=*text&&!*end&&number>=0&&number<=86400?(int)(number*1000):fallback;sfree(text);return result;}
void kr_mac_load_conf(settings_r *reader,Conf *conf){
    int reconnect=read_setting_i(reader,"ReconnectDelay",-1);char *enabled=kr_mac_ini("KiTTY","autoreconnect","yes");bool disabled=!strcasecmp(enabled,"no")||!strcmp(enabled,"0");sfree(enabled);
    if(reconnect<0)reconnect=!disabled&&read_setting_i(reader,"FailureReconnect",0)?5:0;if(disabled)conf_set_bool(conf,CONF_wakeup_reconnect,false);conf_set_int(conf,CONF_kr_reconnect_delay,reconnect<0?0:reconnect>86400?86400:reconnect);
    const struct{const char *stored,*ini;int key,fallback;}delays[]={{"InitDelay","initdelay",CONF_initdelay,2000},{"CommandDelay","commanddelay",CONF_kr_command_delay,5},{"BCDelay","bcdelay",CONF_bcdelay,0}};
    for(size_t i=0;i<lenof(delays);i++){int value=read_setting_i(reader,delays[i].stored,-1);if(value<0)value=ini_milliseconds(delays[i].ini,delays[i].fallback);int limit=delays[i].key==CONF_bcdelay?1000:60000;conf_set_int(conf,delays[i].key,value<0?0:value>limit?limit:value);}

    load_proxy(conf);
    char *mode_text=kr_mac_ini("KiTTY","cryptsalt","0");int mode=atoi(mode_text);sfree(mode_text);
    const char *encoded=conf_get_str(conf,CONF_kr_password_encoded);conf_set_str(conf,CONF_password,"");conf_set_bool(conf,CONF_kr_password_decoded,true);
    if(mode>1)conf_set_str(conf,CONF_password,encoded);else if(*encoded){char *key=mode?dupstr("KiTTY"):dupcat(conf_get_str(conf,CONF_host),conf_get_str(conf,CONF_termtype),"KiTTY");char *plain=kr_legacy_decode(encoded,key);smemclr(key,strlen(key));sfree(key);if(plain){if(!mode)kr_legacy_mask(plain);conf_set_str(conf,CONF_password,plain);smemclr(plain,strlen(plain));free(plain);}else conf_set_bool(conf,CONF_kr_password_decoded,false);}
}
char *kr_mac_prepare_password(Conf *conf){
    const char *plain=conf_get_str(conf,CONF_password);if(!conf_get_bool(conf,CONF_kr_password_decoded)&&!*plain&&*conf_get_str(conf,CONF_kr_password_encoded))return NULL;
    if(!*plain){conf_set_str(conf,CONF_kr_password_encoded,"");return NULL;}
    char *mode_text=kr_mac_ini("KiTTY","cryptsalt","0");int mode=atoi(mode_text);sfree(mode_text);if(!mode&&!*conf_get_str(conf,CONF_host))return dupstr("A host name is required to save this password");if(mode>1){conf_set_str(conf,CONF_kr_password_encoded,plain);return NULL;}
    unsigned char random[5];arc4random_buf(random,sizeof(random));const char *alphabet="AZERTYUIOPQSDFGHJKLMWXCVBNazertyuiopqsdfghjklmwxcvbn0123456789+/";char seed[5];for(size_t i=0;i<5;i++)seed[i]=alphabet[random[i]%64];
    char *masked=dupstr(plain),*key=mode?dupstr("KiTTY"):dupcat(conf_get_str(conf,CONF_host),conf_get_str(conf,CONF_termtype),"KiTTY");if(!mode)kr_legacy_mask(masked);char *encoded=kr_legacy_encode(masked,key,seed);smemclr(masked,strlen(masked));sfree(masked);smemclr(key,strlen(key));sfree(key);if(!encoded)return dupstr("Unable to encode saved password");conf_set_str(conf,CONF_kr_password_encoded,encoded);smemclr(encoded,strlen(encoded));free(encoded);conf_set_bool(conf,CONF_kr_password_decoded,true);return NULL;
}

char *kr_mac_settings_text(settings_r *reader){return dupstr(reader?reader->data->s:"");}
settings_r *kr_mac_snapshot_settings(Conf *conf,void (*serialize)(settings_w*,Conf*)){settings_w writer={0};serialize(&writer,conf);settings_r *result=kr_mac_copy_reader(NULL);while(writer.values){MacValue *v=writer.values;writer.values=v->next;kr_mac_override_read(result,v->key,v->value);smemclr(v->value,strlen(v->value));sfree(v->value);sfree(v->key);sfree(v);}return result;}
int kr_mac_upgrade_profiles(void){
    settings_e *e=enum_settings_start();strbuf *name=strbuf_new();int count=0;const char *keys[]={"PublicKeyFile","DetachedCertificate","Scriptfile","IconeFile","Autocommand"};
    while(enum_settings_next(e,name)){settings_r *r=open_settings_r(name->s);char *failure=NULL;settings_w *w=NULL;
        if(r)for(size_t i=0;i<lenof(keys);i++){char *stored=read_setting_s(r,keys[i]);if(stored&&stored[0]=='/'){char *canonical=realpath(stored,NULL);const char *base=kr_mac_root();size_t n=strlen(base);if(canonical&&!strncmp(canonical,base,n)&&canonical[n]=='/'){if(!w)w=open_settings_w(name->s,&failure);if(w){if(!strcmp(keys[i],"Autocommand")){const char *dot=strrchr(canonical,'.');if(dot&&(!strcasecmp(dot,".sh")||!strcasecmp(dot,".ksh")||!strcasecmp(dot,".sql")||!strcasecmp(dot,".txt")||!strcasecmp(dot,".ps1")))write_setting_s(w,keys[i],canonical+n+1);}else{Filename *file=filename_from_str(canonical);write_setting_filename(w,keys[i],file);filename_free(file);}}}free(canonical);}sfree(stored);}
        if(w){close_settings_w(w);if(kr_mac_write_succeeded())count++;}sfree(failure);close_settings_r(r);strbuf_clear(name);
    }strbuf_free(name);enum_settings_finish(e);return count;
}
