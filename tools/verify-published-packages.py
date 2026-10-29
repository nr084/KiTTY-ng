"""Download-hash every published package and verify latest and independent plugin preservation."""
import hashlib,json,subprocess,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
report=json.loads((root/'dist/release-build.json').read_text());version=report['version']
c=subprocess.run(['git','credential','fill'],input='protocol=https\nhost=github.com\npath=nr084/kitty-ng\n\n',text=True,capture_output=True,check=True,cwd=root)
v=dict(line.split('=',1) for line in c.stdout.splitlines() if '=' in line)
headers={'Authorization':'Bearer '+v['password'],'User-Agent':'KiTTY-ng release verification'}
def api(path):
 with urllib.request.urlopen(urllib.request.Request('https://api.github.com/repos/nr084/kitty-ng/'+path,headers=headers),timeout=60) as stream:return json.load(stream)
release=api('releases/tags/v'+version);assert not release['draft'] and not release['prerelease']
assert api('releases/latest')['id']==release['id']
expected={a['file']:a for a in report['archives']}
assert {a['name'] for a in release['assets']}==set(expected)
verified=[]
for asset in release['assets']:
 item=expected[asset['name']];digest=hashlib.sha256();size=0
 req=urllib.request.Request(asset['browser_download_url']+'?verify='+item['sha256'],headers={'User-Agent':'KiTTY-ng release verification'})
 with urllib.request.urlopen(req,timeout=120) as stream:
  while chunk:=stream.read(1024*1024):digest.update(chunk);size+=len(chunk)
 assert digest.hexdigest()==item['sha256'] and size==item['size'],asset['name']
 verified.append(dict(file=asset['name'],sha256=digest.hexdigest(),size=size))
 print('VERIFIED:',asset['name'],flush=True)
before=json.loads((root/f'dist/github-before-{version}.json').read_text());after=api('releases')
def plugin_ids(releases):
 return {r['tag_name']:[(a['id'],a['name'],a['size'],a.get('digest')) for a in r['assets']] for r in releases if r['tag_name'].startswith('kitty-ng-fs-')}
assert plugin_ids(before)==plugin_ids(after),'Independent plugin changed'
assert [r['tag_name'] for r in after if r['tag_name'].startswith('v') and not r['draft']]==['v'+version]
result=dict(status='passed',release=release['html_url'],version=version,latest=True,archives=verified,plugin_unchanged=True,platform_commits=report['platform_commits'])
(root/f'dist/{version}-published-verification.json').write_text(json.dumps(result,indent=2))
print('PASS: downloaded package hashes, latest, retained plugin and release retention policy')
