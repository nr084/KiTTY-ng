"""Regression for the production PSCP argument loop; no SSH/network is started."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HEADER=r"""
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
typedef const char CmdlineArg;
typedef struct {CmdlineArg **args;} CmdlineArgList;
static const char *cmdline_arg_to_str(CmdlineArg *a){return a;}
static int conf,verbose,recursive,preserve,statistics,list,scp_unsafe_mode,try_scp,try_sftp;
static bool imported,sanitise_stderr;
static int kr_language_arg(const char *a,const char *b){return 0;}
static int cmdline_process_param(CmdlineArg *a,CmdlineArg *next,int save,int c){if(!strcmp(a,"--kitty-ng-conf")){assert(next&&!strcmp(next,"stdin"));imported=true;return 2;}return 0;}
static int cmdline_verbose(void){return 0;}
#define cmdline_error(...) abort()
#define cleanup_exit(x) abort()
#define usage() abort()
#define version() abort()
#define pgp_fingerprints() abort()
"""
TAIL=r"""
 return arglistpos;
}
int main(void){
 CmdlineArg *upload[]={"--kitty-ng-conf","stdin","-sftp","--","/tmp/file with spaces.txt","server:/target",NULL};CmdlineArgList a={upload};assert(parse(&a)==4);assert(imported);
 CmdlineArg *download[]={"-scp","--","server:/file name","/tmp/local directory",NULL};CmdlineArgList b={download};assert(parse(&b)==2);
 CmdlineArg *multiple[]={"-r","--","-leading-dash","/tmp/another.txt","server:.",NULL};CmdlineArgList c={multiple};assert(parse(&c)==2);
 CmdlineArg *plain[]={"-sftp","/tmp/file","server:.",NULL};CmdlineArgList d={plain};assert(parse(&d)==1);
 CmdlineArg *listing[]={"-ls","--","server:.",NULL};CmdlineArgList e={listing};assert(parse(&e)==2);return 0;
}
"""
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',type=Path,required=True);args=p.parse_args()
 source=(ROOT/'pscp.c').read_text(encoding='utf8');start=source.index('    size_t arglistpos = 0;',source.index('int psftp_main('));end=source.index('    backend = NULL;',start)
 with tempfile.TemporaryDirectory(prefix='pscp-args-',dir=ROOT/'build') as temp:
  work=Path(temp);c=work/'args.c';c.write_text(HEADER+'\nstatic size_t parse(CmdlineArgList *arglist){\n'+source[start:end]+TAIL);exe=work/'args.exe'
  subprocess.run([str(args.compiler),'-std=gnu99',str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,timeout=10)
 (ROOT/'dist/pscp-arguments-test.json').write_text(json.dumps(dict(status='passed',scope='Production PSCP option loop; platform and shared option callbacks stubbed; no network',loop_sha256=hashlib.sha256(source[start:end].encode()).hexdigest()),indent=2)+'\n');print('PASS: upload/download/list paths, -- separator, inherited snapshot and multiple files')
if __name__=='__main__':main()
