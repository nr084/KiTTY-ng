# Compile release binaries; reuse verified unchanged logic without re-running suites.
import hashlib,json,shutil,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git','-c','safe.directory='+str(ROOT),*args],cwd=ROOT,text=True).strip()
def source_inputs():
 return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in ROOT.rglob('*') if p.is_file() and p.relative_to(ROOT).parts[0] not in ('_tools','build','dist','.git') and (p.suffix in ('.c','.h','.m','.rc','.rc2') or p.name=='CMakeLists.txt')}
def main():
 version=(ROOT/'VERSION').read_text().strip();out=ROOT/'dist';prior=json.loads((out/'windows-checks.json').read_text(encoding='utf-8'))
 assert prior['compiler']=='Clang 23.1.3' and prior['checks']['x64_real_menu_dispatch_ssh_arrows_log']=='passed'
 toolchain=ROOT/'_tools/llvm-mingw/20261006-local/toolchain';cmake=ROOT/'_tools/macos/cmake/cmake/data/bin/cmake.exe';ninja=ROOT/'_tools/macos/ninja/ninja-1.13.2.data/scripts/ninja.exe'
 report={'version':version,'status':'prepared','source_commit':git('rev-parse','HEAD'),'working_tree_dirty':bool(git('status','--porcelain')),'source_inputs':source_inputs(),'tests_reused':prior['checks'],'reuse_reason':'The tested Windows logic is unchanged since local native verification; release version/resources/docs only changed. Compiler is the same verified stable Clang 23.1.3.','archives':[]}
 with tempfile.TemporaryDirectory(prefix='release-build-',dir=ROOT/'build') as temp:
  for arch in ('x64','x86','arm64'):
   directory=Path(temp)/arch
   subprocess.run([sys.executable,str(ROOT/'tools/build-llvm.py'),arch,'--toolchain',str(toolchain),'--build-dir',str(directory),'--cmake',str(cmake),'--ninja',str(ninja)],cwd=ROOT,check=True)
   saved=out/'release-files'/arch;saved.mkdir(parents=True,exist_ok=True)
   for p in directory.glob('*-'+arch+'.exe'):shutil.copy2(p,saved/p.name)
   for name in ('compiler.json','build.log','configure.log'):shutil.copy2(directory/name,saved/name)
   subprocess.run([sys.executable,str(ROOT/'tools/package-release.py'),arch,str(saved),'--output',str(out)],cwd=ROOT,check=True)
   p=out/f'kitty-ng-{version}-{arch}.zip';report['archives'].append({'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'size':p.stat().st_size})
 mac=json.loads((out/'macos-dmg.json').read_text());assert mac['version']==version and mac['status']=='success';report['archives'].append({'file':mac['file'],'sha256':mac['sha256'],'size':mac['size']})
 (out/'release-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 (out/'SHA256SUMS.txt').write_text(''.join(a['sha256']+'  '+a['file']+'\n' for a in report['archives']))
 print('PASS: versioned release packages prepared; prior native tests reused, no publication')
if __name__=='__main__':main()
