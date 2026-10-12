"""New portable Mac policies and cell-border geometry; actual production code."""
from pathlib import Path
import subprocess,tempfile,json
r=Path(__file__).resolve().parents[1]
lookup=next(line for line in (r/'macos/cli-language.c').read_text().splitlines() if line.startswith('const char *kr_mac_translate('))
program=r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "macos/box-drawing.h"
#include "macos/transfer-path.h"
#define lenof(x) (sizeof(x)/sizeof(*(x)))
static int language=1;
static int choice(void){return language;}
static const struct {const char *en,*ru;}translations[]={
#include "macos/error-translations.h"
};
"""+lookup+r"""
int main(void){
 assert(!strcmp(kr_mac_upload_path(NULL,""),"."));
 assert(!strcmp(kr_mac_upload_path("",""),"."));
 assert(!strcmp(kr_mac_upload_path(NULL,"~/uploads"),"~/uploads"));
 assert(!strcmp(kr_mac_upload_path("/srv/current","~/uploads"),"/srv/current"));
 assert(!strcmp(kr_mac_upload_path("/Каталог с пробелами",NULL),"/Каталог с пробелами"));
 assert(!strcmp(kr_mac_translate("New SSH key passphrase"),"Новая парольная фраза SSH-ключа"));
 assert(!strcmp(kr_mac_translate("Re-enter passphrase to verify: "),"Введите парольную фразу повторно: "));
 const char *prompt="Enter passphrase to save key: ";assert(strcmp(kr_mac_translate(prompt),prompt));language=0;assert(kr_mac_translate(prompt)==prompt);
 KrMacLine lines[8];double widths[]={7.5,8,13.3};
 for(unsigned ch=0x2500;ch<=0x257f;ch++)for(int i=0;i<3;i++)for(int scale=1;scale<=2;scale++){
  int n=kr_mac_box_lines(ch,widths[i],17,scale,lines);assert(n>0&&n<=8);
  for(int j=0;j<n;j++){KrMacLine l=lines[j];assert(l.x1>=0&&l.x1<=widths[i]&&l.x2>=0&&l.x2<=widths[i]);assert(l.y1>=0&&l.y1<=17&&l.y2>=0&&l.y2<=17);assert(l.width>0);}
 }
 int n=kr_mac_box_lines(0x2502,8,17,2,lines);assert(n==2);assert(lines[0].y2==0&&lines[1].y2==17);assert(lines[0].x1==lines[1].x1);
 n=kr_mac_box_lines(0x2500,8,17,2,lines);assert(n==2);assert(lines[0].x2==8&&lines[1].x2==0);assert(lines[0].y1==lines[1].y1);
 assert(!kr_mac_box_lines('A',8,17,2,lines));
 puts("PASS: upload destination/no-prompt policy; key-passphrase Russian/English; 768 box drawing geometry cases and continuous cell edges");
}
"""
with tempfile.TemporaryDirectory(prefix='macos-followups-',dir=r/'build') as temp:
 p=Path(temp);(p/'test.c').write_text(program,encoding='utf-8')
 subprocess.run([str(r/'_tools/llvm-mingw/20261006-local/toolchain/bin/x86_64-w64-mingw32-clang.exe'),'-I'+str(r),str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
(r/'dist/macos-followups-test.json').write_text(json.dumps({'status':'passed','box_cases':768,'scope':'production geometry, destination policy, translation lookup; not AppKit rendering'})+'\n')
