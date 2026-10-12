"""Install verified portable Perl and rcodesign for native Windows macOS builds."""
import hashlib,json,re,shutil,tempfile,urllib.request,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def fetch(url):return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'KiTTY-NG macOS tools'}),timeout=60)
def api(path):
    with fetch('https://api.github.com/repos/'+path) as source:return json.load(source)
def install(kind,release,asset,checksum=None):
    version=release['tag_name'].split('/')[-1];dest=ROOT/'_tools/macos'/kind/version;info=dest/'tool-info.json'
    digest=asset.get('digest')
    if digest:digest=digest.removeprefix('sha256:')
    elif checksum:
        with fetch(checksum['browser_download_url']) as source:digest=source.read().decode().strip().split()[0]
    if not digest or not re.fullmatch('[0-9a-f]{64}',digest):raise RuntimeError('Official SHA-256 unavailable')
    if info.is_file():
        assert json.loads(info.read_text())['sha256']==digest;print('REUSE:',kind,version,flush=True);return
    with tempfile.TemporaryDirectory(prefix='macos-'+kind+'-',dir=ROOT/'build') as temp:
        path=Path(temp)/asset['name'];sha=hashlib.sha256()
        print('Downloading:',asset['name'],flush=True)
        with fetch(asset['browser_download_url']) as source,path.open('wb') as output:
            while chunk:=source.read(1048576):output.write(chunk);sha.update(chunk)
        if sha.hexdigest()!=digest:raise RuntimeError('Official SHA-256 mismatch')
        dest.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(path) as archive:
            for entry in archive.infolist():
                name=entry.filename
                if kind=='perl' and not (name.startswith('perl/') or (name.startswith('c/bin/') and name.lower().endswith('.dll')) or name.lower().startswith(('licenses/','license','readme'))):continue
                target=(dest/name).resolve()
                if not target.is_relative_to(dest.resolve()):raise RuntimeError('Unsafe tool archive path')
                archive.extract(entry,dest)
        info.write_text(json.dumps({'release':release['tag_name'],'url':asset['browser_download_url'],'sha256':digest},indent=2)+'\n')
        print('Installed:',dest,flush=True)
releases=api('StrawberryPerl/Perl-Dist-Strawberry/releases?per_page=50')
choices=[]
for release in releases:
    if release['draft'] or release['prerelease']:continue
    for asset in release['assets']:
        match=re.fullmatch(r'strawberry-perl-([0-9.]+)-64bit-portable.zip',asset['name'])
        if match:choices.append((tuple(map(int,match[1].split('.'))),release,asset))
_,release,asset=max(choices,key=lambda x:x[0]);install('perl',release,asset)
release=api('indygreg/apple-platform-rs/releases/latest');assert not release['draft'] and not release['prerelease']
asset=next(a for a in release['assets'] if a['name'].endswith('x86_64-pc-windows-msvc.zip'))
checksum=next(a for a in release['assets'] if a['name']==asset['name']+'.sha256')
install('rcodesign',release,asset,checksum)
