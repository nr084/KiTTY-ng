"""Repackage verified EXEs without rebuilding or changing their test provenance."""
import hashlib,json,subprocess,sys,tempfile,zipfile
from pathlib import Path
from build_names import release_executables
root=Path(__file__).resolve().parents[1];out=root/'dist'
assert not subprocess.check_output(['git','status','--porcelain'],cwd=root),'Commit packaging changes first'
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
path=out/'local-build.json';original=path.read_bytes();report=json.loads(original)
assert report['status']=='success' and not report['working_tree_dirty']
assert report['version']==(root/'VERSION').read_text().strip()
manifest=[]
with tempfile.TemporaryDirectory(prefix='kitty-ng-repackage-') as directory:
 stage=Path(directory)
 for item in report['archives']:
  archive=out/item['file'];assert hashlib.sha256(archive.read_bytes()).hexdigest()==item['sha256']
  arch=item['file'].removesuffix('.zip').rsplit('-',1)[1];names=release_executables(arch)
  build=stage/arch;build.mkdir()
  with zipfile.ZipFile(archive) as bundle:
   for name in names:(build/name).write_bytes(bundle.read(name))
  binaries={name:hashlib.sha256((build/name).read_bytes()).hexdigest() for name in names}
  subprocess.run([sys.executable,str(root/'tools/package-release.py'),arch,str(build),'--output',str(stage)],cwd=root,check=True)
  replacement=stage/item['file']
  with zipfile.ZipFile(replacement) as bundle:
   assert {name:hashlib.sha256(bundle.read(name)).hexdigest() for name in names}==binaries
  manifest.append({'file':item['file'],'previous_sha256':item['sha256'],'binaries':binaries})
 for item in report['archives']:
  replacement=stage/item['file'];item['sha256']=hashlib.sha256(replacement.read_bytes()).hexdigest();replacement.replace(out/item['file'])
(out/'windows-before-repackage.json').write_bytes(original)
report['packaging']={'source_commit':commit,'previous_report_sha256':hashlib.sha256(original).hexdigest(),'unchanged_binaries':manifest,'reason':'minimal Windows ZIP; compiled and tested executables unchanged'}
path.write_text(json.dumps(report,indent=2)+'\n')
(out/'windows-packaging.json').write_text(json.dumps(report['packaging'],indent=2)+'\n')
print('PASS: minimal ZIPs verified; build and test provenance retained')
