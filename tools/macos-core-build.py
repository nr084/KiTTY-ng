"""Cross-compile real PuTTY Unix backends and macOS portable CLI helpers on Windows."""
import argparse,json,subprocess,tempfile,shutil,os,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--arch',nargs='+',choices=['arm64','x86_64'],default=['arm64','x86_64']);parser.add_argument('--deployment-target',default='13.0');parser.add_argument('--output',type=Path,default=ROOT/'dist/macos-core');args=parser.parse_args()
    store=ROOT/'_tools/macos';candidates=sorted((store/'llvm').glob('*/toolchain-info.json'),key=lambda p:tuple(map(int,p.parent.name.split('.'))));llvm=candidates[-1].parent/'bin';sdk_info=json.loads((store/'sdk/sdk-info.json').read_text());sdk=store/'sdk'/sdk_info['directory']
    cmake=next((store/'cmake').rglob('cmake.exe'));ninja=next((store/'ninja').rglob('ninja.exe'));perl=next((store/'perl').rglob('perl.exe'))
    logs=ROOT/'dist/macos-build-logs';logs.mkdir(exist_ok=True)
    environment=os.environ.copy();environment.update(LC_ALL='C',LC_CTYPE='C',LANG='C');(ROOT/'build').mkdir(exist_ok=True)
    digest=hashlib.sha256()
    for path in sorted(ROOT.rglob('*')):
        relative=path.relative_to(ROOT)
        if relative.parts[0] in ('_tools','build','dist','.git','.github'):continue
        if path.is_file() and (path.suffix in ('.c','.h','.m','.cmake','.pl','.py') or path.name=='CMakeLists.txt'):
            digest.update(relative.as_posix().encode()+b'\0'+path.read_bytes())
    digest.update(json.dumps(sdk_info,sort_keys=True).encode());digest.update(candidates[-1].read_bytes());digest.update(args.deployment_target.encode())
    for arch in args.arch:
        dest=args.output/arch;stamp=dest/'build-cache.json';fingerprint=digest.hexdigest()
        if stamp.exists():
            cached=json.loads(stamp.read_text())
            if cached.get('fingerprint')==fingerprint and all((dest/name).is_file() and hashlib.sha256((dest/name).read_bytes()).hexdigest()==expected for name,expected in cached.get('files',{}).items()) and len(cached.get('files',{}))==6:
                print('REUSE: unchanged macOS compilation',arch,flush=True);continue
        with tempfile.TemporaryDirectory(prefix='macos-core-',dir=ROOT/'build') as temp:
            work=Path(temp)
            command=[cmake,'-S',ROOT,'-B',work,'-G','Ninja','-DCMAKE_SYSTEM_NAME=Darwin','-DCMAKE_SYSTEM_PROCESSOR='+arch,'-DCMAKE_OSX_ARCHITECTURES='+arch,'-DCMAKE_OSX_DEPLOYMENT_TARGET='+args.deployment_target,'-DCMAKE_OSX_SYSROOT='+sdk.as_posix(),'-DCMAKE_C_COMPILER='+(llvm/'clang.exe').as_posix(),'-DCMAKE_C_COMPILER_TARGET='+arch+'-apple-macos'+args.deployment_target,'-DCMAKE_OBJC_COMPILER='+(llvm/'clang.exe').as_posix(),'-DCMAKE_OBJC_COMPILER_TARGET='+arch+'-apple-macos'+args.deployment_target,'-DCMAKE_AR='+(llvm/'llvm-ar.exe').as_posix(),'-DCMAKE_RANLIB='+(llvm/'llvm-ranlib.exe').as_posix(),'-DCMAKE_INSTALL_NAME_TOOL='+(llvm/'llvm-install-name-tool.exe').as_posix(),'-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld','-DCMAKE_MAKE_PROGRAM='+ninja.as_posix(),'-DPERL_EXECUTABLE='+perl.as_posix(),'-DPUTTY_GTK_VERSION=NONE','-DPUTTY_GSSAPI=DYNAMIC','-DCMAKE_BUILD_TYPE=Release']
            for label,command in [('configure',command),('compile',[cmake,'--build',work,'--target','plink','pscp','psftp','puttygen','pageant','kitty-ng-macos','--parallel','8'])]:
                print(arch,label,flush=True);r=subprocess.run([str(a) for a in command],cwd=ROOT,capture_output=True,env=environment);(logs/(arch+'-'+label+'.log')).write_bytes(r.stdout+r.stderr)
                if r.returncode:print((r.stdout+r.stderr).decode('utf8',errors='replace')[-7000:]);raise RuntimeError('macOS '+label+' failed')
            dest=args.output/arch;dest.mkdir(parents=True,exist_ok=True)
            for old,new in [('plink','klink'),('pscp','kscp'),('psftp','ksftp'),('puttygen','kittygen'),('pageant','kageant'),('kitty-ng-macos','kitty-ng')]:
                path=next(p for p in work.rglob(old) if p.is_file());shutil.copyfile(path,dest/new)
            stamp.write_text(json.dumps(dict(fingerprint=fingerprint,files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.iterdir() if p.is_file() and p.name!='build-cache.json'}),indent=2))
            print('PASS: actual macOS core and five helpers',arch,flush=True)
if __name__=='__main__':main()
