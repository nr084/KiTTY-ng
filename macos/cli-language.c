/* Reuse the audited Russian utility strings; SSH payloads are not translated. */
#include "putty.h"
#include "kittyng-i18n.h"
#include "storage.h"
#include "portable.h"
#include "cli-language.h"
#include <CoreFoundation/CoreFoundation.h>
#include <crt_externs.h>
#include <ctype.h>
static int language=-1;
void kr_mac_cli_set_language(const char *name){language=!strcmp(name,"ru")?1:0;setenv("KITTY_NG_LANGUAGE",language?"ru":"en",1);}
static int choice(void){
    if(language>=0)return language;CFArrayRef preferred=CFLocaleCopyPreferredLanguages();language=preferred&&CFArrayGetCount(preferred)&&CFStringHasPrefix(CFArrayGetValueAtIndex(preferred,0),CFSTR("ru"))?1:0;if(preferred)CFRelease(preferred);
    const char *base=kr_mac_root();if(base){char *path=dupcat(base,"/values.ini");FILE *fp=fopen(path,"rb");sfree(path);if(fp){strbuf *bytes=strbuf_new();char block[4096];size_t n;bool valid=true;while(!feof(fp)&&!ferror(fp)&&(n=fread(block,1,sizeof(block),fp))){if(bytes->len+n>1048576){valid=false;break;}put_data(bytes,block,n);}valid=valid&&!ferror(fp);fclose(fp);CFStringRef string=valid?CFStringCreateWithBytes(NULL,bytes->u,bytes->len,bytes->len>=2&&bytes->u[0]==255&&bytes->u[1]==254?kCFStringEncodingUTF16LE:kCFStringEncodingUTF8,true):NULL;if(!string&&valid)string=CFStringCreateWithBytes(NULL,bytes->u,bytes->len,kCFStringEncodingWindowsCyrillic,false);if(string){size_t capacity=CFStringGetMaximumSizeForEncoding(CFStringGetLength(string),kCFStringEncodingUTF8)+1;char *text=snewn(capacity,char);if(CFStringGetCString(string,text,capacity,kCFStringEncodingUTF8)){bool active=false;char *cursor=text,*line;while((line=strsep(&cursor,"\n"))){while(isspace((unsigned char)*line))line++;if(*line=='[')active=!strncasecmp(line,"[values]",8);else if(active&&!strncmp(line,"UILanguage=",11)&&(line[11]=='0'||line[11]=='1'))language=line[11]-'0';}}sfree(text);CFRelease(string);}strbuf_free(bytes);}}
    const char *env=getenv("KITTY_NG_LANGUAGE");if(env&&(!strcmp(env,"ru")||!strcmp(env,"en")))language=!strcmp(env,"ru");
    int argc=*_NSGetArgc();char **argv=*_NSGetArgv();for(int i=1;i+1<argc;i++)if(!strcmp(argv[i],"--language")&&(!strcmp(argv[i+1],"ru")||!strcmp(argv[i+1],"en")))language=!strcmp(argv[i+1],"ru");
return language;
}
const char *kr_mac_translate(const char *text) {
    return kr_i18n_translate(choice(), KR_TEXT_MAC_CLI, text);
}
int kr_mac_vfprintf(FILE *stream,const char *format,va_list ap){return vfprintf(stream,kr_mac_translate(format),ap);}
int kr_mac_fprintf(FILE *stream,const char *format,...){va_list ap;va_start(ap,format);int result=kr_mac_vfprintf(stream,format,ap);va_end(ap);return result;}
int kr_mac_printf(const char *format,...){va_list ap;va_start(ap,format);int result=kr_mac_vfprintf(stdout,format,ap);va_end(ap);return result;}
int kr_mac_fputs(const char *text,FILE *stream){return fputs(kr_mac_translate(text),stream);}
char *kr_mac_dupvprintf(const char *format,va_list ap){return dupvprintf(kr_mac_translate(format),ap);}
char *kr_mac_dupprintf(const char *format,...){va_list ap;va_start(ap,format);char *result=kr_mac_dupvprintf(format,ap);va_end(ap);return result;}
