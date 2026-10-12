"""Exercise production Mac portable parsing and path resolution on the Windows host."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
import macos_automation as shim
ROOT=Path(__file__).resolve().parents[1]
MAIN=r"""
static void value(settings_r *r,const char *key,const char *expected){char *s=read_setting_s(r,key);assert(s&&!strcmp(s,expected));free(s);}
int main(void){
 const char raw[]="OpaqueLegacy\xff\r\nHostName\\old\\\r\nHostName\\server%20name\\\nPublicKeyFile\\SSH-Keys%5Ckey.ppk\\\r\nEmpty\\\\\nPortNumber\\65535\\\nBadInt\\99999999999999999999999\\\nIgnored\\bad%00value\\\n";
 settings_r r={strbuf_new(),NULL};put_data(r.data,raw,sizeof(raw)-1);assert(recognised_data(r.data));
 value(&r,"HostName","server name");value(&r,"PublicKeyFile","SSH-Keys\\key.ppk");value(&r,"Empty","");
 assert(!read_setting_s(&r,"Ignored"));assert(!read_setting_s(&r,"Missing"));assert(read_setting_i(&r,"PortNumber",0)==65535);assert(read_setting_i(&r,"BadInt",22)==22);
 char *ca=ca_value(&r,"HostName");assert(ca&&!strcmp(ca,"server name"));free(ca);
 assert(r.data->len==sizeof(raw)-1&&!memcmp(raw,r.data->s,sizeof(raw)-1));
 char *s=kr_mac_asset_path("SSH-Keys\\key.ppk");assert(!strcmp(s,"/portable/SSH-Keys/key.ppk"));free(s);
 s=kr_mac_asset_path("/outside/key.ppk");assert(!strcmp(s,"/outside/key.ppk"));free(s);
 s=kr_mac_asset_path("C:\\Keys\\key.ppk");assert(!strcmp(s,"C:\\Keys\\key.ppk"));free(s);
 const char unicode[]="\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9 %\\/:";char *escaped=encode(unicode);s=decode(escaped,strlen(escaped));assert(s&&!strcmp(s,unicode));free(s);free(escaped);
 s=kr_mac_decode_name("%CF%F0%EE%F4%E8%EB%E8");assert(s&&!strcmp(s,"\xd0\x9f\xd1\x80\xd0\xbe\xd1\x84\xd0\xb8\xd0\xbb\xd0\xb8"));free(s);assert(!decode("x%00y",5));assert(!safe_name("../bad"));assert(!safe_name(".."));assert(!safe_name("a\\b"));assert(safe_name("normal"));
 strbuf_free(r.data);r.data=strbuf_new();const char ca_record[]="PublicKey=QUJD\nValidity=*.example.com%20%26%26%20!blocked.example.com\nPermitRSASHA256=1\n";put_data(r.data,ca_record,strlen(ca_record));ca=ca_value(&r,"Validity");assert(ca&&!strcmp(ca,"*.example.com && !blocked.example.com"));free(ca);ca=ca_value(&r,"PublicKey");assert(ca&&!strcmp(ca,"QUJD"));free(ca);strbuf_free(r.data);strbuf *invalid=strbuf_new();put_data(invalid,"binary\0HostName\\x\\",19);assert(!recognised_data(invalid));strbuf_free(invalid);return 0;
}
"""
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',type=Path,required=True);args=parser.parse_args()
 source=(ROOT/'macos/storage.c').read_text(encoding='utf8');segments=[('static char *decode(', 'static char *component('),('static bool safe_name(', 'static char *configuration('),('static bool recognised_data(', 'static settings_r *read_path('),('char *read_setting_s(', 'void close_settings_r('),('char *kr_mac_asset_path(', 'static bool asset_key('),('static char *duplicate_part(', 'typedef struct MacValue'),('static char *ca_value(', 'host_ca *host_ca_load(')]
 actual='\n'.join(source[source.index(a):source.index(b)] for a,b in segments)
 header=shim.HEADER+r"""
#include "macos/names.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#define snewn(n,t) ((t*)malloc((n)*sizeof(t)))
typedef struct {strbuf *data;char *path;} settings_r;
static const char *kr_mac_root(void){return "/portable";}
static char *join(const char *a,const char *b){char *slash=dupcat(a,"/");char *result=dupcat(slash,b);free(slash);return result;}
static void put_fmt(strbuf *b,const char *fmt,...){char text[1024];va_list ap;va_start(ap,fmt);int n=vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);assert(n>=0&&n<sizeof(text));put_data(b,text,n);}
"""
 with tempfile.TemporaryDirectory(prefix='macos-portable-',dir=ROOT/'build') as temp:
  work=Path(temp);c=work/'portable.c';c.write_text(header+actual+MAIN,encoding='utf8');exe=work/'portable.exe'
  subprocess.run([str(args.compiler),'-std=gnu99','-I',str(ROOT),str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,cwd=work,timeout=10)
 (ROOT/'dist/macos-portable-test.json').write_text(json.dumps(dict(status='passed',scope='Production record parsing and root-based path resolution with host buffer/root shims; not native atomic I/O',source_sha256={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in ['macos/storage.c','test/macos_portable.py','test/macos_automation.py']}),indent=2)+'\n')
 print('PASS: original KiTTY records, duplicate/empty/UTF-8 values, NUL/integer rejection, unchanged unknown bytes, portable and external paths')
if __name__=='__main__':main()
