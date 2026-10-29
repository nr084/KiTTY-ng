"""Native check of documentation-only Linux repackaging; preserve prior GUI/SSH results."""
from pathlib import Path
import hashlib,json,subprocess
root=Path('/opt/kitty-ng/acceptance-0.85-11');old=root/'KiTTY-ng.AppDir';new=root/'minimal-check'
new.mkdir(exist_ok=True)
subprocess.run(['tar','--zstd','-xf',str(root/'minimal.tar.zst'),'-C',str(new)],check=True)
app=new/'KiTTY-ng.AppDir'
def files(folder):
 return {str(p.relative_to(folder)):hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*') if p.is_file()}
a=files(old);b=files(app)
# Generated entropy seed is runtime state, not a distributed file.
a.pop('usr/bin/kitty-ng-linux.rnd',None)
assert all(a.get(k)==v for k,v in b.items()),[k for k,v in b.items() if a.get(k)!=v]
removed=sorted(set(a)-set(b));assert removed and all(k.startswith('usr/share/doc/kitty-ng/') for k in removed),removed
assert not any('/screenshots/' in k or k.endswith(('README.md','README.en.md')) for k in b)
image=root/'minimal.AppImage';image.chmod(0o755)
run=subprocess.run(['xvfb-run','-a',str(image),'--appimage-extract-and-run','--version'],text=True,capture_output=True,timeout=60)
assert run.returncode==0,run.stderr
assert '0.85-11' in run.stdout,run.stdout
report={'status':'passed','kind':'documentation-only-repackaging','unchanged_files':len(b),'removed_files':removed,'appimage_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),'appdir_archive_sha256':hashlib.sha256((root/'minimal.tar.zst').read_bytes()).hexdigest(),'version_output':run.stdout,'native_checks':'unchanged runtime files plus packaged AppImage launch; prior GUI/SSH acceptance retained'}
(root/'minimal-package-check.json').write_text(json.dumps(report,indent=2))
print('PASS: all runtime files unchanged; documentation removed; new AppImage launches natively')
