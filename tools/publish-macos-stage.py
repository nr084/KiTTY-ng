"""Add a verified native Mac DMG to a published five-package client release.

Windows/Linux provenance comes from the existing published release, not stale
local build reports. Their asset IDs and content are never replaced.
"""
import argparse,concurrent.futures,hashlib,json,re,subprocess,tempfile,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO='nr084/KiTTY-ng'
def gh(*args):return subprocess.check_output(['gh',*map(str,args)],cwd=ROOT,text=True)
def api(path):return json.loads(gh('api',f'repos/{REPO}/{path}'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def identity(a):return {k:a.get(k) for k in ('id','name','size','digest')}
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--publish',action='store_true');args=parser.parse_args();out=ROOT/'dist/macos-native';version=(ROOT/'VERSION').read_text().strip();tag='v'+version
 assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT),'Commit source/docs before publication'
 build=json.loads((out/'macos-build.json').read_text());gui=json.loads((out/'gui-acceptance.json').read_text());dmg=json.loads((out/'macos-dmg.json').read_text());path=out/f'kitty-ng-{version}-macos-universal.dmg'
 assert build['status']=='success' and build['version']==version and not build['working_tree_dirty'] and build['runtime_verified'] and build['tamper_checks']=='passed'
 assert set(build['architectures'])=={'arm64','x86_64'} and dmg['native_mount_verified'] and dmg['sha256']==sha(path)
 assert gui['status']=='passed' and gui['source_sha256']==build['source_sha256'] and gui['packaged_main_sha256']==sha(out/'KiTTY-ng.app/Contents/MacOS/kitty-ng')
 subprocess.run(['git','merge-base','--is-ancestor',build['source_commit'],'HEAD'],cwd=ROOT,check=True)
 for arch in build['architectures']:
  native=json.loads((out/f'runtime-{arch}.json').read_text());assert native['status']=='passed'
  for n,h in native['binaries_sha256'].items():assert sha(out/'KiTTY-ng.app/Contents/Helpers'/n)==h
 assert json.loads((out/f'mounted-launch-{version}.json').read_text())['status']=='passed'
 release=api('releases/tags/'+tag);assert not release['draft'] and not release['prerelease'];assert api('releases/latest')['id']==release['id']
 snapshot=out/f'github-before-macos-stage-{version}.json'
 if not snapshot.exists():snapshot.write_text(json.dumps(dict(release=release,tag=api('git/ref/tags/'+tag),releases=api('releases?per_page=100')),ensure_ascii=False,indent=2)+'\n')
 before=json.loads(snapshot.read_text());assert before['release']['id']==release['id'] and before['tag']==api('git/ref/tags/'+tag)
 expected={f'kitty-ng-{version}-{a}.zip' for a in ['x64','x86','arm64']}|{f'kitty-ng-{version}-x86_64.AppImage',f'kitty-ng-{version}-x86_64.tar.zst'}
 original=[a for a in before['release']['assets'] if a['name'] in expected];assert len(original)==5
 for a in original:assert identity(next(x for x in release['assets'] if x['id']==a['id']))==identity(a)
 commits=dict(re.findall(r'- (windows|linux): `([0-9a-f]{40})`',before['release']['body']));assert set(commits)=={'windows','linux'};commits['macos']=build['source_commit']
 def download(a,folder):
  target=folder/a['name'];digest=a['digest'].removeprefix('sha256:')
  if target.exists():assert sha(target)==digest,'Preserve existing package with a different hash: '+str(target)
  if not target.exists():
   temporary=target.with_suffix(target.suffix+'.download')
   try:
    req=urllib.request.Request(a['browser_download_url']+'?macos-stage='+digest[:12],headers={'User-Agent':'KiTTY-ng native stage verification'})
    with urllib.request.urlopen(req,timeout=60) as source,temporary.open('wb') as output:
     while chunk:=source.read(1024*1024):output.write(chunk)
    assert temporary.stat().st_size==a['size'] and sha(temporary)==digest;temporary.replace(target)
   finally:
    if temporary.exists():temporary.unlink()
  assert target.stat().st_size==a['size'] and sha(target)==digest
  return dict(file=target.name,size=a['size'],sha256=digest,published_asset_id=a['id'])
 # Download and independently hash the existing packages without changing them.
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:archives=list(pool.map(lambda a:download(a,ROOT/'dist'),original))
 if not args.publish:print('PASS: native Mac acceptance and five published packages verified; no upload');return
 existing=next((a for a in release['assets'] if a['name']==path.name),None)
 if existing:assert existing.get('digest')=='sha256:'+sha(path),'Use a separately authorized replacement workflow'
 else:gh('release','upload',tag,path,'--repo',REPO)
 release=api('releases/tags/'+tag);assert {a['name'] for a in release['assets']}==expected|{path.name}
 uploaded=next(a for a in release['assets'] if a['name']==path.name);assert uploaded['digest']=='sha256:'+sha(path) and uploaded['size']==path.stat().st_size
 with tempfile.TemporaryDirectory(prefix='kitty-ng-published-mac-',dir='/private/tmp') as temporary:download(uploaded,Path(temporary))
 for a in original:assert identity(next(x for x in release['assets'] if x['id']==a['id']))==identity(a)
 assert api('git/ref/tags/'+tag)==before['tag']
 plugins=[r for r in before['releases'] if r['tag_name'].startswith('kitty-ng-fs-')]
 for r in plugins:
  now=api('releases/'+str(r['id']));assert [identity(a) for a in now['assets']]==[identity(a) for a in r['assets']]
 archives.append(dict(file=path.name,size=path.stat().st_size,sha256=sha(path),published_asset_id=uploaded['id']))
 sums=''.join(a['sha256']+'  '+a['file']+'\n' for a in archives)
 notes=(ROOT/f'docs/releases/{version}.md').read_text();assert 'macOS Universal 2 готова' in notes
 body=notes+'\n\n### SHA-256\n\n```text\n'+sums+'```\n\n### Исходники пакетов / Package sources\n\n'+'\n'.join('- '+k+': `'+v+'`' for k,v in commits.items())+'\n'
 with tempfile.NamedTemporaryFile('w',suffix='.json') as f:
  json.dump({'body':body},f);f.flush();gh('api',f'repos/{REPO}/releases/{release["id"]}','--method','PATCH','--input',f.name)
 assert api('releases/latest')['id']==release['id'] and api('git/ref/tags/'+tag)==before['tag']
 manifest=dict(status='success',version=version,release_scope='all',working_tree_dirty=False,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),platform_commits=commits,archives=archives,windows_linux_validation={'status':'previously-published','execution_reused':True,'evidence':str(snapshot.relative_to(ROOT)),'verification':'Original asset IDs/sizes/digests preserved; downloaded hashes verified; no fresh native Windows/Linux execution claimed'},macos_validation={'status':'passed','build_report':'dist/macos-native/macos-build.json','gui_report':'dist/macos-native/gui-acceptance.json','mounted_dmg_launch':'dist/macos-native/mounted-launch-'+version+'.json'})
 (ROOT/'dist/release-build.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n');(ROOT/'dist/SHA256SUMS.txt').write_text(sums)
 publication=dict(status='passed',release_url=release['html_url'],release_id=release['id'],assets=[identity(a) for a in release['assets']],downloaded_dmg_sha256=sha(path),tag_unchanged=True,windows_linux_assets_unchanged=True,plugin_unchanged=True,latest=True)
 (out/f'publication-{version}-macos.json').write_text(json.dumps(publication,ensure_ascii=False,indent=2)+'\n')
 print('PASS: six published packages; downloaded DMG SHA-256',sha(path),'; original five IDs, tag, plugin and latest preserved')
if __name__=='__main__':main()
