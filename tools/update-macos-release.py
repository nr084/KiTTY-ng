"""Explicitly update only the Mac asset of an existing release, keeping its tag and other assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
REPO='nr084/kitty-ng'


def gh(*arguments):
    return subprocess.check_output(['gh',*map(str,arguments)],cwd=ROOT,text=True)


def api(path, data=None):
    arguments=['api',f'repos/{REPO}/{path}']
    if data is None:return json.loads(gh(*arguments))
    with tempfile.NamedTemporaryFile(mode='w',suffix='.json') as stream:
        json.dump(data,stream);stream.flush()
        return json.loads(gh(*arguments,'--method','PATCH','--input',stream.name))


def identity(asset):
    return (asset['id'],asset['name'],asset['size'],asset.get('digest'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publish',action='store_true',help='Requires a new explicit user instruction for this update')
    parser.add_argument('--output',type=Path,default=ROOT/'dist/macos-native')
    args=parser.parse_args()
    version=(ROOT/'VERSION').read_text().strip();tag='v'+version
    name=f'kitty-ng-{version}-macos-universal.dmg'
    archived=f'kitty-ng-{version}-macos-universal-windows-build.dmg'
    dest=args.output.resolve();snapshot=dest/'github-before-macos-update.json'
    release=api('releases/tags/'+tag)
    assert not release['draft'] and not release['prerelease']
    if not snapshot.exists():
        # Capture the original state before the first mutation; retain it for safe retries.
        releases=api('releases?per_page=100')
        assert len(releases)<100,'Extend pagination before updating a larger release history'
        snapshot.write_text(json.dumps(dict(release=release,releases=releases,tag=api('git/ref/tags/'+tag)),indent=2)+'\n')
    before=json.loads(snapshot.read_text())
    assert before['release']['id']==release['id']
    if not args.publish:
        print('Saved reviewable snapshot:',snapshot)
        return
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT),'Commit changes before publishing'
    report=json.loads((dest/'macos-build.json').read_text())
    assert report['status']=='success' and report['version']==version
    assert report['source_commit']==commit and not report['working_tree_dirty']
    assert report['runtime_verified'] and report['tamper_checks']=='passed'
    assert set(report['architectures'])=={'arm64','x86_64'}
    assert report['dmg']['native_mount_verified']
    for arch in report['architectures']:
        assert all(value=='passed' for value in report['native_tests'][arch].values())
        proxy=json.loads((dest/f'proxies-{arch}.json').read_text())
        assert proxy['status']=='passed' and {'http-authenticated-loopback-proxy','socks5-authenticated-loopback-proxy'}<=set(proxy['checks'])
        for binary,digest in proxy['binaries_sha256'].items():
            assert hashlib.sha256((dest/'KiTTY-ng.app/Contents/Helpers'/binary).read_bytes()).hexdigest()==digest
    cert=json.loads((dest/'certificates.json').read_text());assert cert['status']=='passed'
    for binary,digest in cert['binaries_sha256'].items():
        assert hashlib.sha256((dest/'KiTTY-ng.app/Contents/Helpers'/binary).read_bytes()).hexdigest()==digest
    assert json.loads((dest/'gui-acceptance.json').read_text())['status']=='passed'
    path=dest/name;digest=hashlib.sha256(path.read_bytes()).hexdigest()
    assert digest==report['dmg']['sha256']
    # Publish source first; the release tag remains on its original commit.
    assert api('commits/main')['sha']==commit,'Push the verified source commit before uploading'
    assert api('git/ref/tags/'+tag)==before['tag'],'Existing tag changed'
    original=next(asset for asset in before['release']['assets'] if asset['name']==name)
    asset=next(asset for asset in release['assets'] if asset['id']==original['id'])
    assert asset['size']==original['size'] and asset['digest']==original['digest']
    if asset['name']==name:
        asset=api('releases/assets/'+str(asset['id']),dict(name=archived,label='Предыдущая Mac-сборка на Windows (архив)'))
    assert asset['name']==archived
    release=api('releases/tags/'+tag)
    uploaded=next((asset for asset in release['assets'] if asset['name']==name),None)
    if uploaded is None:
        gh('release','upload',tag,path,'--repo',REPO)
        release=api('releases/tags/'+tag)
        uploaded=next(asset for asset in release['assets'] if asset['name']==name)
    assert uploaded['digest']=='sha256:'+digest and uploaded['size']==path.stat().st_size
    body=(ROOT/f'docs/releases/{version}.md').read_text()
    body+=f'\n\nИсходники обновлённого Mac-пакета: [{commit[:12]}](https://github.com/{REPO}/commit/{commit}). Исходный тег и Windows/Linux-пакеты сохранены.\n\n### SHA-256\n\n```text\n'
    for asset in sorted(release['assets'],key=lambda item:item['name']):
        assert asset['digest'].startswith('sha256:')
        body+=asset['digest'].removeprefix('sha256:')+'  '+asset['name']+'\n'
    body+='```\n'
    release=api('releases/'+str(release['id']),dict(body=body))
    untouched=[a for a in before['release']['assets'] if a['id']!=original['id']]
    for asset in untouched:
        assert identity(next(a for a in release['assets'] if a['id']==asset['id']))==identity(asset),'Non-Mac asset changed'
    assert len(release['assets'])==len(before['release']['assets'])+1
    for old in before['releases']:
        if old['id']==release['id']:continue
        current=api('releases/'+str(old['id']))
        assert current['tag_name']==old['tag_name']
        assert [identity(a) for a in current['assets']]==[identity(a) for a in old['assets']],'Previous release assets changed'
    assert api('git/ref/tags/'+tag)==before['tag']
    result=dict(status='published',version=version,release_url=release['html_url'],source_commit=commit,
                mac_asset=uploaded,archived_mac_asset=asset,other_assets_preserved=True,previous_releases_preserved=True,tag_preserved=True)
    result['archived_mac_asset']=next(a for a in release['assets'] if a['id']==original['id'])
    (dest/'macos-release-update.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Updated Mac package:',release['html_url'])


if __name__=='__main__':main()
