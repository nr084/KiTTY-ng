"""Reproduce MC Unicode cell widths under the C locale using the Mac grid code."""
import argparse,json,hashlib,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--compiler",type=Path,required=True);args=parser.parse_args()
    source=r"""
#include "putty.h"
#include "macos/grid.h"
#include "utils/wcwidth.c"
#include <locale.h>
int main(void){
    assert(setlocale(LC_ALL,"C"));
    const unsigned mc[]={0x250c,0x2500,0x041b,0x0435,0x0432,0x0430,0x044f,0x20,0x043f,0x0430,0x043d,0x0435,0x043b,0x044c,0x2510};
    int columns=0;for(unsigned i=0;i<sizeof(mc)/sizeof(*mc);i++){assert(kr_mac_char_width(mc[i])==1);columns+=kr_mac_char_width(mc[i]);}
    assert(columns==15);assert(kr_mac_run_cells(columns,0)==15);
    assert(kr_mac_run_cells(120,0)==120);
    assert(kr_mac_char_width(0x4e2d)==2);assert(kr_mac_run_cells(3,ATTR_WIDE)==6);
    assert(kr_mac_char_width(0x0301)==0);assert(kr_mac_run_cells(2,TATTR_COMBINING)==1);
    assert(kr_mac_run_cells(3,TATTR_COMBINING|ATTR_WIDE)==2);return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="macos-grid-",dir=ROOT/"build") as temporary:
        work=Path(temporary);c=work/"grid.c";c.write_text(source);exe=work/"grid.exe"
        subprocess.run([str(args.compiler),"-std=c99","-DHAVE_STRTOUMAX=1","-I",str(ROOT),"-I",str(ROOT/"windows"),str(c),"-o",str(exe)],check=True,cwd=ROOT)
        subprocess.run([str(exe)],check=True,timeout=10)
    files=("macos/grid.h","utils/wcwidth.c","test/macos_grid.py")
    (ROOT/"dist/macos-grid-test.json").write_text(json.dumps(dict(status="passed",scope="C locale/MC Unicode grid regression; not native drawing",source_sha256={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files}),indent=2)+"\n")
    print("PASS: MC Cyrillic and box drawing retain all cells under C locale; wide and combining runs preserve PuTTY allocations")
if __name__=="__main__":main()
