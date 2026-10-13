"""Keep verified stable macOS LLVM and CMake locally, without changing the host."""
import hashlib
import importlib.util
import concurrent.futures
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STORE = ROOT / '_tools/macos/native'


def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'KiTTY-ng native build'}), timeout=60)


def main():
    if sys.platform != 'darwin':
        raise SystemExit('This installer requires macOS')
    with fetch('https://api.github.com/repos/llvm/llvm-project/releases/latest') as response:
        release = json.load(response)
    assert not release['draft'] and not release['prerelease']
    version = release['tag_name'].removeprefix('llvmorg-')
    host = {'arm64': 'ARM64', 'x86_64': 'X64'}[platform.machine()]
    from compression import zstd
    asset = next(a for a in release['assets'] if a['name'] == f'LLVM-{version}-macOS-{host}.tar.zst')
    expected = asset['digest'].removeprefix('sha256:')
    installed = STORE / 'llvm' / version
    metadata = installed / 'toolchain-info.json'
    if metadata.is_file() and json.loads(metadata.read_text())['sha256'] == expected and (installed/'bin/clang').exists():
        print('REUSE: stable native LLVM', version, flush=True)
    else:
        (ROOT/'build').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='native-llvm-', dir=ROOT/'build') as temporary:
            work = Path(temporary)
            archive = work / asset['name']
            print('Downloading official native LLVM', version, flush=True)
            # Independent verified byte ranges keep large official downloads fast.
            with fetch(asset['browser_download_url']) as source:
                download_url = source.url
            size = asset['size']
            block = 16*1024*1024
            with archive.open('w+b') as target:
                target.truncate(size)
                def part(start):
                    end = min(start+block,size)-1
                    request = urllib.request.Request(download_url, headers={'Range':f'bytes={start}-{end}'})
                    with urllib.request.urlopen(request,timeout=120) as source:
                        if source.status != 206 or not source.headers.get('Content-Range','').startswith(f'bytes {start}-{end}/'):
                            raise RuntimeError('LLVM download server did not honor the byte range')
                        offset=start
                        while chunk := source.read(1048576):
                            os.pwrite(target.fileno(),chunk,offset)
                            offset+=len(chunk)
                        if offset != end+1:
                            raise RuntimeError('Truncated LLVM download')
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                    list(pool.map(part,range(0,size,block)))
            with archive.open('rb') as source:
                digest = hashlib.file_digest(source,'sha256').hexdigest()
            if digest != expected:
                raise RuntimeError('Official LLVM SHA-256 mismatch')
            payload = work/'payload'
            payload.mkdir()
            with zstd.open(archive,'rb',options={zstd.DecompressionParameter.window_log_max:31}) as decompressed, tarfile.open(fileobj=decompressed, mode='r|') as bundle:
                for member in bundle:
                    relative = Path(*Path(member.name).parts[1:])
                    if not relative.parts:
                        continue
                    keep = (relative.parts[0] == 'bin' and relative.name in {'clang','clang++','clang-'+version.split('.')[0],'llvm-ar','llvm-ranlib'} or
                            relative.parts[:2] == ('lib', 'clang') or
                            relative.parts[0] == 'lib' and '.dylib' in relative.name or
                            relative.name.startswith('LICENSE'))
                    if keep:
                        member.name = relative.as_posix()
                        if member.islnk():
                            member.linkname = Path(*Path(member.linkname).parts[1:]).as_posix()
                        bundle.extract(member, payload, filter='data')
            installed.parent.mkdir(parents=True, exist_ok=True)
            if installed.exists():
                shutil.rmtree(installed)
            shutil.move(payload, installed)
            metadata.write_text(json.dumps(dict(version=version, host=platform.machine(),
                url=asset['browser_download_url'], sha256=expected, release=release['html_url']), indent=2)+'\n')
    # PyPI wheels are verified against the publisher's SHA-256 by fetch_wheel.
    spec = importlib.util.spec_from_file_location('local_build', ROOT/'tools/local-build.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    packages = {}
    prior=json.loads((STORE/'packages.json').read_text()) if (STORE/'packages.json').exists() else {}
    for name in ('cmake', 'ninja', 'pillow'):
        print('Checking native build package:', name, flush=True)
        destination = STORE/name
        with fetch(f'https://pypi.org/pypi/{name}/json') as response:
            latest=json.load(response)['info']['version']
        if prior.get(name,{}).get('version')==latest and destination.is_dir():
            packages[name]=prior[name]
            print('REUSE:',name,latest,flush=True)
        else:
            with tempfile.TemporaryDirectory(prefix='native-package-',dir=ROOT/'build') as temporary:
                staged=Path(temporary)/name
                staged.mkdir()
                packages[name]=module.fetch_wheel(name,staged)
                if destination.exists():shutil.rmtree(destination)
                shutil.move(staged,destination)
    for path in (STORE/'cmake/cmake/data/bin').iterdir():
        path.chmod(0o755)
    next(p for p in (STORE/'ninja').rglob('ninja') if p.is_file()).chmod(0o755)
    (STORE/'packages.json').write_text(json.dumps(packages, indent=2)+'\n')
    print('PASS: verified local macOS build tools', flush=True)


if __name__ == '__main__':
    main()
