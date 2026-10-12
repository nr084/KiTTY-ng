"""Compile actual terminal arrow formatter; no terminal/SSH implementation mock."""
import json,subprocess,tempfile,sys
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'terminal/terminal.c').read_text();start=s.index('static int shift_bitmap(');end=s.index('int format_function_key(',start)
source=s[start:end]
program=r"""
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
enum {SHARROW_APPLICATION,SHARROW_BITMAP};
typedef struct Terminal {bool vt52_mode,app_cursor_keys,no_applic_c,app_keypad_keys;int sharrow_type;} Terminal;
"""+source+r"""
int main(void) {
 char buf[80],expected[80];
 for(int app=0;app<2;app++)for(int mods=0;mods<8;mods++)for(const char *key="ABCD";*key;key++) {
  Terminal t={.app_cursor_keys=app,.sharrow_type=SHARROW_BITMAP}; bool consumed=false;
  int n=format_arrow_key(buf,&t,*key,mods&1,mods&4,mods&2,&consumed);buf[n]=0;
  if(mods) sprintf(expected,"\033[1;%d%c",mods+1,*key);
  else sprintf(expected,app?"\033O%c":"\033[%c",*key);
  assert(!strcmp(buf,expected));assert(consumed==((mods&2)!=0));
 }
 Terminal t={.app_cursor_keys=true,.sharrow_type=SHARROW_APPLICATION};bool used=false;
 int n=format_arrow_key(buf,&t,'A',true,false,false,&used);buf[n]=0;assert(!strcmp(buf,"\033[1;2A"));
 n=format_arrow_key(buf,&t,'A',false,true,false,&used);buf[n]=0;assert(!strcmp(buf,"\033[A"));
 t.vt52_mode=true;t.sharrow_type=SHARROW_BITMAP;n=format_arrow_key(buf,&t,'B',true,false,false,&used);buf[n]=0;assert(!strcmp(buf,"\033B"));
 puts("PASS: 64 arrow modifier/mode combinations; shifted legacy profiles fixed; Ctrl-only toggle and VT52 retained");
}
"""
with tempfile.TemporaryDirectory(prefix='windows-arrows-',dir=r/'build') as temp:
 p=Path(temp);(p/'test.c').write_text(program);compiler=r/'_tools/llvm-mingw/20261006-local/toolchain/bin/x86_64-w64-mingw32-clang.exe'
 subprocess.run([str(compiler),str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
(r/'dist/windows-arrow-test.json').write_text(json.dumps({'status':'passed','cases':64,'legacy_and_vt52':'passed'})+'\n')
