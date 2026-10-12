"""Build all release platforms locally on Windows and assemble verified checksums."""
import argparse,hashlib,json,subprocess,sys,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git','-c','safe.directory='+str(ROOT),*args],cwd=ROOT,text=True).strip()
def run(*args):subprocess.run([sys.executable,*map(str,args)],cwd=ROOT,check=True)
def read(path):return json.loads((ROOT/path).read_text(encoding='utf8'))
def main():
 p=argparse.ArgumentParser();p.add_argument('--assemble-only',action='store_true');p.add_argument('--build-only',action='store_true');args=p.parse_args()
 assert not git('status','--porcelain'),'Commit source and documentation before the release build'
 version=(ROOT/'VERSION').read_text().strip();commit=git('rev-parse','HEAD');out=ROOT/'dist'
 if not args.assemble_only:
  run('tools/local-build.py','--toolchain',ROOT/'_tools/llvm-mingw/20261006-local/toolchain','--cmake',ROOT/'_tools/macos/cmake/cmake/data/bin/cmake.exe','--ninja',ROOT/'_tools/macos/ninja/ninja-1.13.2.data/scripts/ninja.exe','--offline')
  run('tools/local-build.py','--platform','macos','--offline');run('tools/macos-dmg.py')
  run('tools/local-build.py','--platform','linux');run('tools/linux-package-windows.py')
 if args.build_only:
  print('All platforms built locally; native Linux acceptance is required before assembly');return
 windows=read('dist/local-build.json');mac=read('dist/macos-build.json');dmg=read('dist/macos-dmg.json');linux=read('build/linux-windows/linux-cross-build.json');package=read('dist/linux/linux-package.json');acceptance=read('dist/linux/native-acceptance.json')
 for report in (windows,mac,linux):
  assert report['status']=='success' and report['version']==version
  assert report['source_commit']==commit and not report['working_tree_dirty'],'Build must match the committed source'
 assert dmg['version']==package['version']==version and dmg['status']=='success'
 assert acceptance['status']=='passed' and acceptance['version']==version
 assert acceptance['binaries']==package['binaries'],'Native acceptance must cover the shipped Linux binaries'
 package['acceptance_tests']='passed';(out/'linux/linux-package.json').write_text(json.dumps(package,indent=2)+'\n')
 archives=[]
 for name in [*(f'kitty-ng-{version}-{arch}.zip' for arch in ('x64','x86','arm64')),f'kitty-ng-{version}-macos-universal.dmg',f'kitty-ng-{version}-x86_64.AppImage',f'kitty-ng-{version}-x86_64.tar.zst']:
  path=out/name
  if name.endswith(('.AppImage','.tar.zst')):shutil.copy2(out/'linux'/name,path)
  assert path.is_file()
  archives.append({'file':name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'size':path.stat().st_size})
 report={'status':'success','version':version,'source_commit':commit,'working_tree_dirty':False,'build_host':'Windows','archives':archives,'windows_tests':windows['tests'],'macos_validation':{'bundle_seals':mac.get('tamper_checks'),'dmg_round_trip':dmg['bundle_hashes'],'native_runtime':'not run for this build'},'linux_validation':acceptance}
 (out/'release-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 (out/'SHA256SUMS.txt').write_text(''.join(a['sha256']+'  '+a['file']+'\n' for a in archives),encoding='ascii')
 print('PASS: six release packages, source provenance and native Linux acceptance; no publication')
if __name__=='__main__':main()
