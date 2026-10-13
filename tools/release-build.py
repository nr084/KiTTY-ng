"""Build on the platform host and assemble the existing release package set."""
import argparse,hashlib,json,subprocess,sys,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git','-c','safe.directory='+str(ROOT),*args],cwd=ROOT,text=True).strip()
def run(*args):subprocess.run([sys.executable,*map(str,args)],cwd=ROOT,check=True)
def read(path):return json.loads((ROOT/path).read_text(encoding='utf8'))
def main():
 p=argparse.ArgumentParser();p.add_argument('--assemble-only',action='store_true');p.add_argument('--build-only',action='store_true');p.add_argument('--macos-dir',type=Path,default=ROOT/'dist/macos-native');args=p.parse_args()
 assert not git('status','--porcelain'),'Commit source and documentation before the release build'
 version=(ROOT/'VERSION').read_text().strip();commit=git('rev-parse','HEAD');out=ROOT/'dist'
 if not args.assemble_only:
  if sys.platform=='darwin':
   run('tools/local-build.py','--platform','macos','--output',args.macos_dir)
  elif sys.platform=='win32':
   run('tools/local-build.py','--toolchain',ROOT/'_tools/llvm-mingw/20261006-local/toolchain','--cmake',ROOT/'_tools/macos/cmake/cmake/data/bin/cmake.exe','--ninja',ROOT/'_tools/macos/ninja/ninja-1.13.2.data/scripts/ninja.exe','--offline')
   run('tools/local-build.py','--platform','linux');run('tools/linux-package-windows.py')
  else:p.error('Build Windows/Linux on Windows and macOS on Mac, then assemble verified artifacts')
 if args.build_only:
  print('Host platform built locally; prepare other platform artifacts and native acceptance before assembly');return
 windows=read('dist/local-build.json');mac=read(args.macos_dir/'macos-build.json');dmg=read(args.macos_dir/'macos-dmg.json');linux=read('build/linux-windows/linux-cross-build.json');package=read('dist/linux/linux-package.json');acceptance=read('dist/linux/native-acceptance.json')
 for report in (windows,mac,linux):
  assert report['status']=='success' and report['version']==version
  assert report['source_commit']==commit and not report['working_tree_dirty'],'Build must match the committed source'
 assert dmg['version']==package['version']==version and dmg['status']=='success'
 assert mac['runtime_verified'] and dmg['native_mount_verified'],'Native macOS acceptance and mounted DMG verification are required'
 gui=read(args.macos_dir/'gui-acceptance.json')
 assert gui['status']=='passed' and gui['source_sha256']==mac['source_sha256'],'Native GUI acceptance must match the macOS sources'
 for arch in ('arm64','x86_64'):
  native=read(args.macos_dir/('runtime-'+arch+'.json'))
  assert native['status']=='passed' and native['architecture']==arch
  for name,digest in native['binaries_sha256'].items():
   assert hashlib.sha256((args.macos_dir/'KiTTY-ng.app/Contents/Helpers'/name).read_bytes()).hexdigest()==digest,'macOS runtime acceptance must cover shipped helpers'
 assert acceptance['status']=='passed' and acceptance['version']==version
 assert acceptance['binaries']==package['binaries'],'Native acceptance must cover the shipped Linux binaries'
 package['acceptance_tests']='passed';(out/'linux/linux-package.json').write_text(json.dumps(package,indent=2)+'\n')
 archives=[]
 for name in [*(f'kitty-ng-{version}-{arch}.zip' for arch in ('x64','x86','arm64')),f'kitty-ng-{version}-macos-universal.dmg',f'kitty-ng-{version}-x86_64.AppImage',f'kitty-ng-{version}-x86_64.tar.zst']:
  path=out/name
  if name.endswith(('.AppImage','.tar.zst')):shutil.copy2(out/'linux'/name,path)
  if name.endswith('-macos-universal.dmg'):
   source=args.macos_dir/name
   assert hashlib.sha256(source.read_bytes()).hexdigest()==dmg['sha256']
   if source.resolve()!=path.resolve():shutil.copy2(source,path)
  assert path.is_file()
  archives.append({'file':name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'size':path.stat().st_size})
 report={'status':'success','version':version,'source_commit':commit,'working_tree_dirty':False,'build_host':sys.platform,'archives':archives,'windows_tests':windows['tests'],'macos_validation':{'bundle_seals':mac.get('tamper_checks'),'dmg_round_trip':dmg['bundle_hashes'],'native_runtime':'arm64 and x86_64 passed'},'linux_validation':acceptance}
 (out/'release-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 (out/'SHA256SUMS.txt').write_text(''.join(a['sha256']+'  '+a['file']+'\n' for a in archives),encoding='ascii')
 print('PASS: six release packages, source provenance and native Linux acceptance; no publication')
if __name__=='__main__':main()
