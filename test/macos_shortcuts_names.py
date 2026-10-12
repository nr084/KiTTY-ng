"""Run the production portable shortcut and legacy-name code on Windows."""
import argparse,subprocess,tempfile,json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r"""
#include <stdio.h>
#include <assert.h>
#define lenof(a) (sizeof(a)/sizeof(*(a)))
#include "macos/names.h"
#include "macos/shortcuts.h"
int main(void){
 assert(kr_mac_parse_shortcut("{CONTROL}{F3}")==((2<<16)|114));assert(kr_mac_parse_shortcut("Ctrl+Shift+F24")==((6<<16)|135));
 assert(!kr_mac_parse_shortcut("{F25}"));assert(!kr_mac_parse_shortcut("{CONTROL"));assert(!kr_mac_parse_shortcut("A+B"));assert(!kr_mac_parse_shortcut("EXEC"));
 for(unsigned mods=0;mods<32;mods++)for(unsigned key=10;key<=255;key++){unsigned number=key+(mods&8?8000:0)+(mods&16?4000:0)+(mods&1?2000:0)+(mods&2?1000:0)+(mods&4?500:0);char text[32];snprintf(text,sizeof(text),"%u",number);assert(kr_mac_parse_shortcut(text)==(key|(mods<<16)));}
 char *name=kr_name_from_bytes("\xcf\xf0\xee\xf4\xe8\xeb\xe8");assert(name&&!strcmp(name,"\xd0\x9f\xd1\x80\xd0\xbe\xd1\x84\xd0\xb8\xd0\xbb\xd0\xb8"));free(name);
 name=kr_name_from_bytes("\xd0\x9f\xd1\x80\xd0\xbe\xd1\x84\xd0\xb8\xd0\xbb\xd0\xb8");assert(name&&kr_name_valid_utf8((unsigned char*)name,strlen(name)));free(name);
 assert(!kr_name_from_bytes("\x98"));assert(!kr_name_valid_utf8((unsigned char*)"\xc0\x80",2));assert(!kr_name_valid_utf8((unsigned char*)"\xed\xa0\x80",3));return 0;
}
"""
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',type=Path,required=True);args=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='macos-shortcut-name-',dir=ROOT/'build') as temp:
  work=Path(temp);c=work/'test.c';c.write_text(SOURCE);exe=work/'test.exe';subprocess.run([str(args.compiler),'-std=gnu99','-I',str(ROOT),str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,timeout=10)
 (ROOT/'dist/macos-shortcuts-names-test.json').write_text(json.dumps(dict(status='passed',source_sha256={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in ['macos/names.h','macos/shortcuts.h']}),indent=2)+'\n');print('PASS: 7872 numeric shortcut cases, brace syntax, invalid keys and UTF-8/CP1251 names')
if __name__=='__main__':main()
