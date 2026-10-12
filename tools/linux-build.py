"""Native Linux build and portable AppImage/AppDir packaging (no publication)."""
import argparse,hashlib,json,os,platform,shutil,subprocess,tempfile,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def run(args,**kw):subprocess.run([str(a) for a in args],check=True,**kw)
def fetch(repo,asset,store):
 headers={'User-Agent':'KiTTY-ng Linux portable build'}
 with urllib.request.urlopen(urllib.request.Request('https://api.github.com/repos/'+repo+'/releases/latest',headers=headers),timeout=30) as response:release=json.load(response)
 item=next(a for a in release['assets'] if a['name']==asset);digest=item.get('digest','');assert digest.startswith('sha256:')
 path=store/asset
 if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest[7:]:
  with urllib.request.urlopen(item['browser_download_url'],timeout=60) as source,path.open('wb') as output:shutil.copyfileobj(source,output)
 assert hashlib.sha256(path.read_bytes()).hexdigest()==digest[7:];path.chmod(0o755)
 return path,{'repo':repo,'release':release['tag_name'],'url':item['browser_download_url'],'sha256':digest[7:]}
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--compiler',default='clang');parser.add_argument('--output',type=Path,default=ROOT/'dist/linux');parser.add_argument('--build-dir',type=Path,default=ROOT/'build/linux');parser.add_argument('--package-only',action='store_true');args=parser.parse_args();assert platform.system()=='Linux';arch=platform.machine();assert arch in ('x86_64','aarch64')
 out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);store=ROOT/'_tools/linux/packaging';store.mkdir(parents=True,exist_ok=True);version=(ROOT/'VERSION').read_text().strip();names=['kitty-ng','klink','kscp','ksftp','kittygen','kageant']
 compiler=subprocess.check_output([args.compiler,'--version'],text=True).splitlines()[0]
 if not args.package_only:
  run(['cmake','-S',ROOT,'-B',args.build_dir,'-G','Ninja','-DCMAKE_C_COMPILER='+args.compiler,'-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DPUTTY_GTK_VERSION=3','-DPUTTY_GSSAPI=DYNAMIC'])
  run(['cmake','--build',args.build_dir,'--target','putty','plink','pscp','psftp','puttygen','pageant','--parallel',str(min(os.cpu_count() or 2,4))])
 deploy,deployment=fetch('linuxdeploy/linuxdeploy','linuxdeploy-'+arch+'.AppImage',store)
 pack,packaging=fetch('AppImage/appimagetool','appimagetool-'+arch+'.AppImage',store)
 with tempfile.TemporaryDirectory(prefix='appimage-',dir=out) as temporary:
  work=Path(temporary);app=work/'KiTTY-ng.AppDir';binary=app/'usr/bin';binary.mkdir(parents=True)
  for name in names:shutil.copy2(args.build_dir/name,binary/name)
  share=app/'usr/share';(share/'applications').mkdir(parents=True);(share/'icons/hicolor/256x256/apps').mkdir(parents=True)
  from PIL import Image,ImageOps
  icon=ImageOps.contain(Image.open(ROOT/'icons/kitty-ng.png').convert('RGBA'),(256,256),Image.Resampling.LANCZOS)
  canvas=Image.new('RGBA',(256,256));canvas.alpha_composite(icon,((256-icon.width)//2,(256-icon.height)//2));canvas.save(share/'icons/hicolor/256x256/apps/kitty-ng.png')
  desktop=share/'applications/kitty-ng.desktop';desktop.write_text('[Desktop Entry]\nType=Application\nName=KiTTY-ng\nComment=Portable SSH client\nExec=kitty-ng\nIcon=kitty-ng\nTerminal=false\nCategories=Network;RemoteAccess;\n')
  command=[deploy,'--appimage-extract-and-run','--appdir',app,'--desktop-file',desktop,'--icon-file',share/'icons/hicolor/256x256/apps/kitty-ng.png']
  for name in names:command+=['--executable',binary/name]
  run(command,cwd=work)
  shutil.copytree(ROOT/'docs',share/'doc/kitty-ng/docs');shutil.copytree(ROOT/'icons',share/'doc/kitty-ng/icons',ignore=shutil.ignore_patterns('*.ico','*.icns','*.pam','*.c'))
  for name in ['README.md','README.en.md','LICENCE','LICENCE-KiTTY.txt']:shutil.copy2(ROOT/name,share/'doc/kitty-ng'/name)
  schemas=Path('/usr/share/glib-2.0/schemas/gschemas.compiled')
  if schemas.exists():(share/'glib-2.0/schemas').mkdir(parents=True);shutil.copy2(schemas,share/'glib-2.0/schemas/gschemas.compiled')
  launcher=app/'AppRun';launcher.write_text("""#!/bin/sh
set -eu
APPDIR=${APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}
export APPDIR
if [ -z "${KITTY_NG_PORTABLE_DIR:-}" ]; then
    if [ -n "${APPIMAGE:-}" ]; then
        KITTY_NG_PORTABLE_DIR=$(dirname -- "$APPIMAGE")
    else
        KITTY_NG_PORTABLE_DIR=$(dirname -- "$APPDIR")
    fi
    export KITTY_NG_PORTABLE_DIR
fi
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="$APPDIR/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GSETTINGS_SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas"
program=kitty-ng
if [ "${1:-}" = --tool ]; then
    shift
    case "${1:-}" in klink|kscp|ksftp|kittygen|kageant) program=$1;; *) echo 'Unknown helper' >&2; exit 2;; esac
    shift
fi
exec "$APPDIR/usr/bin/$program" "$@"
""");launcher.chmod(0o755)
  target=out/f'kitty-ng-{version}-{arch}.AppImage'
  run([pack,'--appimage-extract-and-run',app,target],cwd=work,env={**os.environ,'ARCH':arch,'VERSION':version})
  run(['tar','--zstd','-cf',out/f'kitty-ng-{version}-{arch}.tar.zst','-C',work,app.name])
  binaries={name:hashlib.sha256((binary/name).read_bytes()).hexdigest() for name in names}
  report={'version':version,'architecture':arch,'compiler':compiler,'deployment':deployment,'packaging':packaging,'binaries':binaries,'appimage':target.name,'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'runtime_tests':'pending'}
  (out/'linux-build.json').write_text(json.dumps(report,indent=2)+'\n')
 print('Built AppImage and portable AppDir archive; native acceptance tests still required')
if __name__=='__main__':main()
