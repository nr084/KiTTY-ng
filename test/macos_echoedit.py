"""Host regression for the Ldisc/Seat notification recursion reported on macOS.

Runs the real portable C Ldisc update with the callback selected by the Mac seat.
This is not native AppKit execution. Only the unused line editor is stubbed.
"""
import argparse,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--compiler",type=Path,required=True);args=parser.parse_args()
    source=(ROOT/"macos/client.m").read_text(encoding="utf8")
    callback=re.search(r"\.echoedit_update\s*=\s*(\w+)",source).group(1)
    ldisc=(ROOT/"ldisc.c").read_text(encoding="utf8")
    prefix=ldisc[:ldisc.index("Ldisc *ldisc_create")]
    update=ldisc[ldisc.index("void ldisc_echoedit_update"):ldisc.index("void ldisc_provide_userpass_le")]
    nullseat=(ROOT/"stubs/null-seat.c").read_text(encoding="utf8")
    callback_body=re.search(r"void "+re.escape(callback)+r"\([^)]*\)\s*\{[^}]*\}",nullseat).group(0)
    harness=prefix+callback_body+update+r"""
static unsigned flushes;
void lineedit_send_line(TermLineEditor *le) { flushes++; }
static bool echo_state, edit_state;
static bool options(Backend *backend,int state) { return state==LD_ECHO?echo_state:edit_state; }
int main(void) {
    const BackendVtable backend_vt={.ldisc_option_state=options};
    Backend backend={&backend_vt};
    const SeatVtable seat_vt={.echoedit_update=CALLBACK};
    Seat seat={&seat_vt};Ldisc ldisc={0};ldisc.seat=&seat;ldisc.backend=&backend;
    const int states[]={FORCE_ON,FORCE_OFF,AUTO};unsigned expected=0;
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)for(unsigned e=0;e<2;e++)for(unsigned d=0;d<2;d++) {
        ldisc.localecho=states[i];ldisc.localedit=states[j];echo_state=e;edit_state=d;
        for(unsigned repeat=0;repeat<10000;repeat++) {ldisc_echoedit_update(&ldisc);if(states[j]==FORCE_OFF||(states[j]==AUTO&&!d))expected++;}
    }
    assert(flushes==expected);return 0;
}
""".replace("CALLBACK",callback)
    with tempfile.TemporaryDirectory(prefix="macos-echoedit-",dir=ROOT/"build") as temporary:
        work=Path(temporary);c=work/"regression.c";c.write_text(harness);exe=work/"regression.exe"
        subprocess.run([str(args.compiler),"-std=c99","-DHAVE_STRTOUMAX=1","-O2","-ffunction-sections","-fdata-sections","-I",str(ROOT),"-I",str(ROOT/"windows"),"-I",str(ROOT/"terminal"),str(c),"-Wl,--gc-sections,--exclude-all-symbols","-o",str(exe)],check=True,cwd=ROOT)
        subprocess.run([str(exe)],check=True,timeout=10)
    print("PASS: 360000 real Ldisc updates across forced/automatic echo and editing states; no recursion; pending-line flush preserved")
if __name__=="__main__":main()
