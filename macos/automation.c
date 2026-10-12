/* Startup text is sent to the remote terminal; never executed locally. */
#include "putty.h"
#include "portable.h"
#include "automation.h"
#include <sys/stat.h>
#include <ctype.h>
static bool utf8(const unsigned char *s,size_t length){
    for(size_t i=0;i<length;){unsigned c=s[i++];if(c<0x80){if(!c)return false;continue;}unsigned n,min;
        if(c>=0xc2&&c<=0xdf){n=1;min=0x80;c&=31;}else if(c>=0xe0&&c<=0xef){n=2;min=0x800;c&=15;}else if(c>=0xf0&&c<=0xf4){n=3;min=0x10000;c&=7;}else return false;
        if(i+n>length)return false;while(n--){unsigned next=s[i++];if((next&0xc0)!=0x80)return false;c=(c<<6)|(next&63);}if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
    }return true;
}
char *kr_mac_command_text(const char *text,bool escapes,bool submit){
    strbuf *out=strbuf_new();bool cr=false;
    for(size_t i=0;text[i];i++){unsigned char c=text[i];if(escapes&&c=='\\'&&text[i+1]){unsigned char next=text[i+1];if(next=='n'||next=='r'){c='\r';i++;}else if(next=='t'){c='\t';i++;}else if(next=='\\'){c='\\';i++;}}
        if(c=='\r'){put_byte(out,c);cr=true;}else if(c=='\n'){if(!cr)put_byte(out,'\r');cr=false;}else{put_byte(out,c);cr=false;}}
    if(submit&&out->len&&out->s[out->len-1]!='\r')put_byte(out,'\r');return strbuf_to_str(out);
}
static char *command_text(const char *text,bool escapes){return kr_mac_command_text(text,escapes,true);}
char *kr_mac_script_text(const Filename *file,char **error){
    *error=NULL;struct stat st;const char *path=filename_to_str(file);if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size>1048576){*error=dupstr("Startup script must be UTF-8 text, maximum 1 MiB");return NULL;}
    FILE *fp=fopen(path,"rb");if(!fp){*error=dupstr("Unable to read startup script");return NULL;}strbuf *data=strbuf_new();char block[4096];size_t n;bool valid=true;
    while(!feof(fp)&&!ferror(fp)&&(n=fread(block,1,sizeof(block),fp))){if(data->len+n>1048576){valid=false;break;}put_data(data,block,n);}valid=valid&&!ferror(fp);fclose(fp);smemclr(block,sizeof(block));
    const unsigned char *bytes=(unsigned char*)data->s;size_t size=data->len;if(size>=3&&!memcmp(bytes,"\xef\xbb\xbf",3)){bytes+=3;size-=3;}
    valid=valid&&utf8(bytes,size);char *result=valid?command_text((const char*)bytes,false):NULL;if(!valid)*error=dupstr("Startup script must be UTF-8 text, maximum 1 MiB");smemclr(data->s,data->len);strbuf_free(data);return result;
}
static bool reference(const char *s){if(strpbrk(s,"\r\n"))return false;const char *extensions[]={".sh",".ksh",".sql",".txt",".ps1"};size_t n=strlen(s);for(size_t i=0;i<lenof(extensions);i++){size_t k=strlen(extensions[i]);if(n>k&&!strcasecmp(s+n-k,extensions[i]))return true;}return false;}
char *kr_mac_startup_commands(Conf *conf,char **error){
    *error=NULL;const char *automatic=conf_get_str(conf,CONF_autocommand),*inline_text=conf_get_str(conf,CONF_scriptfilecontent);
    if(strlen(automatic)>65536||strlen(inline_text)>1048576||!utf8((const unsigned char*)automatic,strlen(automatic))||!utf8((const unsigned char*)inline_text,strlen(inline_text))){*error=dupstr("Startup commands must be UTF-8 text within size limits");return NULL;}
    const Filename *file=conf_get_filename(conf,CONF_kr_script_file);char *script=filename_is_null(file)?command_text(inline_text,false):kr_mac_script_text(file,error);if(!script)return NULL;
    char *command=NULL;if(reference(automatic)){char *path=kr_mac_asset_path(automatic);struct stat st;if(!stat(path,&st)){Filename *f=filename_from_str(path);command=kr_mac_script_text(f,error);filename_free(f);}sfree(path);}
    if(*error){smemclr(script,strlen(script));sfree(script);return NULL;}
    if(!command&&(automatic[0]=='\\'||(isalpha((unsigned char)automatic[0])&&automatic[1]==':'))){smemclr(script,strlen(script));sfree(script);*error=dupstr("Automatic command references an unavailable Windows script path");return NULL;}
    if(!command)command=command_text(automatic,true);char *result=dupcat(script,command);smemclr(script,strlen(script));smemclr(command,strlen(command));sfree(script);sfree(command);return result;
}
