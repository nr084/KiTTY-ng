"""Compile the production Mac startup parser with host-only buffer/platform shims."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HEADER=r"""
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#define lenof(a) (sizeof(a)/sizeof(*(a)))
#define sfree free
#define CONF_autocommand 0
#define CONF_scriptfilecontent 1
#define CONF_kr_script_file 2
typedef struct {char *s;size_t len;} strbuf;
typedef struct {const char *path;} Filename;
typedef struct {const char *automatic,*inline_text;Filename file;} Conf;
static strbuf *strbuf_new(void){strbuf *b=calloc(1,sizeof(*b));b->s=calloc(1,1);return b;}
static void put_data(strbuf *b,const void *p,size_t n){b->s=realloc(b->s,b->len+n+1);memcpy(b->s+b->len,p,n);b->len+=n;b->s[b->len]=0;}
static void put_byte(strbuf *b,unsigned c){char v=c;put_data(b,&v,1);}
static void strbuf_free(strbuf *b){free(b->s);free(b);}
static char *strbuf_to_str(strbuf *b){char *s=b->s;free(b);return s;}
static char *dupstr(const char *s){return strdup(s);}
static char *dupcat(const char *a,const char *b){char *s=malloc(strlen(a)+strlen(b)+1);strcpy(s,a);strcat(s,b);return s;}
static void smemclr(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
static const char *filename_to_str(const Filename *f){return f->path;}
static bool filename_is_null(const Filename *f){return !*f->path;}
static Filename *filename_from_str(const char *s){Filename *f=malloc(sizeof(*f));f->path=dupstr(s);return f;}
static void filename_free(Filename *f){free((void*)f->path);free(f);}
static const char *conf_get_str(Conf *c,int k){return k==0?c->automatic:c->inline_text;}
static const Filename *conf_get_filename(Conf *c,int k){return &c->file;}
"""
MAIN=r"""
#include "production.c"
static void check(const char *input,bool escapes,const char *expected){char *s=command_text(input,escapes);assert(!strcmp(s,expected));free(s);}
int main(void){
 check("",false,"");check("one\r\ntwo\nthree\r",false,"one\rtwo\rthree\r");
 check("one\\ntwo\\t\\\\",true,"one\rtwo\t\\\r");check("echo \\q",true,"echo \\q\r");
 char *shortcut=kr_mac_command_text("echo\\n",true,false);assert(!strcmp(shortcut,"echo\r"));free(shortcut);shortcut=kr_mac_command_text("echo",true,false);assert(!strcmp(shortcut,"echo"));free(shortcut);
 assert(utf8((unsigned char*)"\xd0\xa0\xe2\x94\x80\xf0\x9f\x98\x80",9));
 assert(!utf8((unsigned char*)"\xc0\x80",2));assert(!utf8((unsigned char*)"\xed\xa0\x80",3));
 assert(!utf8((unsigned char*)"\xf4\x90\x80\x80",4));assert(!utf8((unsigned char*)"\xe2\x82",2));assert(!utf8((unsigned char*)"a\0b",3));
 FILE *fp=fopen("script.txt","wb");assert(fp);fwrite("\xef\xbb\xbf" "echo test\r\n",1,14,fp);fclose(fp);
 char *error=NULL;Filename file={"script.txt"};char *s=kr_mac_script_text(&file,&error);assert(s&&!error&&!strcmp(s,"echo test\r"));free(s);
 Conf c={"echo second\\n", "", {"script.txt"}};s=kr_mac_startup_commands(&c,&error);assert(s&&!error&&!strcmp(s,"echo test\recho second\r"));free(s);
 c.file.path="";c.automatic="script.txt";s=kr_mac_startup_commands(&c,&error);assert(s&&!error&&!strcmp(s,"echo test\r"));free(s);
 c.automatic="C:\\missing\\script.txt";assert(!kr_mac_startup_commands(&c,&error)&&error);free(error);
 c.automatic="echo ok";c.inline_text="\xc0\x80";assert(!kr_mac_startup_commands(&c,&error)&&error);free(error);
 fp=fopen("script.txt","wb");fwrite("a\0b",1,3,fp);fclose(fp);assert(!kr_mac_script_text(&file,&error)&&error);free(error);
 fp=fopen("script.txt","wb");for(int i=0;i<1048577;i++)fputc('a',fp);fclose(fp);assert(!kr_mac_script_text(&file,&error)&&error);free(error);
 return 0;
}
"""
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',type=Path,required=True);args=parser.parse_args()
 with tempfile.TemporaryDirectory(prefix='macos-automation-',dir=ROOT/'build') as temp:
  work=Path(temp);(work/'putty.h').write_text(HEADER);(work/'portable.h').write_text('static char *kr_mac_asset_path(const char *s){return dupstr(s);}\n');(work/'automation.h').write_text('')
  (work/'production.c').write_bytes((ROOT/'macos/automation.c').read_bytes());(work/'test.c').write_text(MAIN)
  exe=work/'test.exe';subprocess.run([str(args.compiler),'-std=gnu99',str(work/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,cwd=work,timeout=10)
 (ROOT/'dist/macos-automation-test.json').write_text(json.dumps(dict(status='passed',scope='Production UTF-8, command normalization, file reading, rejection and startup composition on host; platform/buffer shims, no AppKit runtime',source_sha256={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in ['macos/automation.c','test/macos_automation.py']}),indent=2)+'\n')
 print('PASS: production startup parser, CR/LF/escapes, UTF-8, BOM, embedded NUL, Windows path and size rejection')
if __name__=='__main__':main()
