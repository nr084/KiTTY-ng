"""Exercise the portable INI Unicode decoder; existing name/shortcut checks are reused."""
import argparse,subprocess,tempfile,json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r"""
#include <assert.h>
#include "macos/names.h"
int main(void){char *s=kr_text_from_bytes((unsigned char*)"\xef\xbb\xbf[KiTTY]\n",11);assert(s&&!strcmp(s,"[KiTTY]\n"));free(s);
 const unsigned char le[]={255,254,'[',0,'A',0,']',0,10,0,0x1f,4,0x40,4};s=kr_text_from_bytes(le,sizeof(le));assert(s&&!strcmp(s,"[A]\n\xd0\x9f\xd1\x80"));free(s);
 const unsigned char be[]={254,255,0,'A',0xd8,0x3d,0xde,0};s=kr_text_from_bytes(be,sizeof(be));assert(s&&!strcmp(s,"A\xf0\x9f\x98\x80"));free(s);
 const unsigned char bad[]={255,254,0x00,0xd8};assert(!kr_text_from_bytes(bad,sizeof(bad)));assert(!kr_text_from_bytes(le,sizeof(le)-1));assert(!kr_text_from_bytes((unsigned char*)"a\0b",3));return 0;}
"""
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',type=Path,required=True);a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='macos-ini-',dir=ROOT/'build') as temp:
  work=Path(temp);c=work/'test.c';c.write_text(SOURCE);exe=work/'test.exe';subprocess.run([str(a.compiler),'-std=gnu99','-I',str(ROOT),str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,timeout=10)
 (ROOT/'dist/macos-ini-text-test.json').write_text(json.dumps(dict(status='passed',scope='Actual UTF-8 BOM/UTF-16 LE/BE/surrogate/NUL decoding',source_sha256=hashlib.sha256((ROOT/'macos/names.h').read_bytes()).hexdigest()),indent=2)+'\n');print('PASS: UTF-8 BOM, UTF-16 LE/BE, surrogate pair and malformed INI rejection')
if __name__=='__main__':main()
