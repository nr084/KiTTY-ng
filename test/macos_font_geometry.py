"""Check the geometry used by the native double-width/double-height renderer."""
import argparse,subprocess,tempfile,json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r"""
#include <assert.h>
#include "putty.h"
#include "macos/grid.h"
int main(void){assert(kr_mac_line_width(LATTR_NORM)==1);assert(kr_mac_line_width(LATTR_WIDE)==2);assert(kr_mac_line_width(LATTR_TOP)==2);assert(kr_mac_line_height(LATTR_TOP)==2);assert(kr_mac_line_height(LATTR_BOT)==2);assert(kr_mac_line_height(LATTR_WIDE)==1);assert(kr_mac_line_offset(LATTR_TOP,20)==0);assert(kr_mac_line_offset(LATTR_BOT,20)==-20);return 0;}
"""
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',type=Path,required=True);args=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='macos-font-',dir=ROOT/'build') as temp:
  work=Path(temp);c=work/'test.c';c.write_text(SOURCE);exe=work/'test.exe';subprocess.run([str(args.compiler),'-std=gnu99','-DHAVE_STRTOUMAX=1','-I',str(ROOT),'-I',str(ROOT/'windows'),str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,timeout=10)
 (ROOT/'dist/macos-font-geometry-test.json').write_text(json.dumps(dict(status='passed',scope='Actual renderer geometry, not native glyph drawing',source_sha256=hashlib.sha256((ROOT/'macos/grid.h').read_bytes()).hexdigest()),indent=2)+'\n');print('PASS: normal/double-width and aligned top/bottom double-height geometry')
if __name__=='__main__':main()
