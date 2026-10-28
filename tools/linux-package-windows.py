"""Package Windows-built Linux ELF binaries as AppImage without executing them."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '_tools/linux/python'))
from elftools.elf.elffile import ELFFile
import zstandard
import urllib.request

def fetch(repo, asset, store):
    request = urllib.request.Request('https://api.github.com/repos/' + repo + '/releases/latest', headers={'User-Agent': 'KiTTY-ng portable build'})
    with urllib.request.urlopen(request, timeout=30) as response:
        release = json.load(response)
    item = next(item for item in release['assets'] if item['name'] == asset)
    digest = item.get('digest', '')
    if not digest.startswith('sha256:'):
        raise ValueError('Missing upstream runtime SHA-256')
    target = store / asset
    if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != digest[7:]:
        with urllib.request.urlopen(item['browser_download_url'], timeout=60) as source, target.open('wb') as output:
            shutil.copyfileobj(source, output)
    if hashlib.sha256(target.read_bytes()).hexdigest() != digest[7:]:
        raise ValueError('Runtime checksum mismatch')
    return target, {'release': release['tag_name'], 'url': item['browser_download_url'], 'sha256': digest[7:]}


# These belong to the host ABI or hardware drivers, rather than our GUI bundle.
HOST_LIBRARIES = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0',
                  'librt.so.1', 'libresolv.so.2', 'libutil.so.1',
                  'ld-linux-x86-64.so.2', 'libGL.so.1', 'libGLX.so.0',
                  'libGLdispatch.so.0', 'libEGL.so.1'}
NAMES = ('kitty-ng', 'klink', 'kscp', 'ksftp', 'kittygen', 'kageant')


def needed(path):
    with path.open('rb') as source:
        elf = ELFFile(source)
        if elf.header.e_machine != 'EM_X86_64':
            raise ValueError('Unexpected ELF architecture: ' + str(path))
        dynamic = elf.get_section_by_name('.dynamic')
        return [tag.needed for tag in dynamic.iter_tags() if tag.entry.d_tag == 'DT_NEEDED'] if dynamic else []


def bundle_dependencies(app, sysroot):
    search = [sysroot / 'usr/lib/x86_64-linux-gnu', sysroot / 'lib/x86_64-linux-gnu']
    output = app / 'usr/lib'
    output.mkdir(parents=True)
    modules = output / 'gio/modules'
    shutil.copytree(sysroot / 'usr/lib/x86_64-linux-gnu/gio/modules', modules)
    queue = list((app / 'usr/bin').iterdir()) + list(modules.glob('*.so'))
    copied = set()
    while queue:
        for name in needed(queue.pop()):
            if name in HOST_LIBRARIES or name in copied:
                continue
            source = next((directory / name for directory in search if (directory / name).is_file()), None)
            if source is None:
                raise FileNotFoundError('Missing target dependency: ' + name)
            shutil.copy2(source, output / name)
            copied.add(name)
            queue.append(output / name)
    return sorted(copied)


def tar_tree(app, target, prefix=''):
    with tarfile.open(target, 'w') as archive:
        for source in [app, *sorted(app.rglob('*'))]:
            name = prefix + source.relative_to(app).as_posix()
            info = archive.gettarinfo(str(source), arcname=name)
            info.uid = info.gid = 0
            info.uname = info.gname = 'root'
            info.mode = 0o755 if source.is_dir() or source.name == 'AppRun' or source.parent.name == 'bin' else 0o644
            if source.is_file():
                with source.open('rb') as stream:
                    archive.addfile(info, stream)
            else:
                archive.addfile(info)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/linux-windows')
    parser.add_argument('--sysroot', type=Path, default=ROOT / '_tools/linux/sysroot')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/linux')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Use this entry point locally on Windows')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    store = ROOT / '_tools/linux/packaging'
    store.mkdir(parents=True, exist_ok=True)
    runtime, provenance = fetch('AppImage/type2-runtime', 'runtime-x86_64', store)
    version = (ROOT / 'VERSION').read_text().strip()
    with tempfile.TemporaryDirectory(prefix='linux-package-', dir=out) as directory:
        work = Path(directory)
        app = work / 'KiTTY-ng.AppDir'
        binaries = app / 'usr/bin'
        binaries.mkdir(parents=True)
        for name in NAMES:
            shutil.copy2(args.build_dir / name, binaries / name)
        from local_toolchain import ensure_local_toolchain
        toolchain = ensure_local_toolchain(ROOT)
        compiler = subprocess.check_output([toolchain / 'bin/clang.exe', '--version'], text=True).splitlines()[0]
        build_report = json.loads((args.build_dir / 'linux-cross-build.json').read_text())
        if build_report['compiler'] != compiler:
            raise RuntimeError('Stable compiler changed: rebuild Linux binaries before packaging and testing')
        source_binaries = {name: hashlib.sha256((binaries / name).read_bytes()).hexdigest() for name in NAMES}
        if source_binaries != build_report['binaries']:
            raise RuntimeError('Linux input binaries do not match the build report')
        strip = toolchain / 'bin/llvm-strip.exe'
        for name in NAMES:
            subprocess.run([str(strip), '--strip-all', str(binaries / name)], check=True)
        libraries = bundle_dependencies(app, args.sysroot)
        from PIL import Image, ImageOps
        icon = ImageOps.contain(Image.open(ROOT / 'icons/kitty-ng.png').convert('RGBA'), (256, 256), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (256, 256))
        canvas.alpha_composite(icon, ((256-icon.width)//2, (256-icon.height)//2))
        icon_path = app / 'usr/share/icons/hicolor/256x256/apps/kitty-ng.png'
        icon_path.parent.mkdir(parents=True)
        canvas.save(icon_path)
        shutil.copytree(args.sysroot / 'usr/share/icons/Adwaita', app / 'usr/share/icons/Adwaita')
        shutil.copy2(args.sysroot / 'usr/share/icons/hicolor/index.theme', app / 'usr/share/icons/hicolor/index.theme')
        shutil.copy2(icon_path, app / 'kitty-ng.png')
        shutil.copy2(icon_path, app / '.DirIcon')
        desktop = '[Desktop Entry]\nType=Application\nName=KiTTY-ng\nComment=Portable SSH client\nExec=kitty-ng\nIcon=kitty-ng\nTerminal=false\nCategories=Network;RemoteAccess;\n'
        (app / 'kitty-ng.desktop').write_text(desktop, encoding='utf-8', newline='\n')
        # Import only launcher text; native build/package commands are never run.
        (app / 'AppRun').write_bytes((ROOT / 'linux/AppRun').read_bytes().replace(b'\r\n', b'\n'))
        docs = app / 'usr/share/doc/kitty-ng'
        docs.mkdir(parents=True)
        for name in ('README.md', 'README.en.md', 'LICENCE', 'LICENCE-KiTTY.txt'):
            shutil.copy2(ROOT / name, docs / name)
        shutil.copytree(ROOT / 'docs', docs / 'docs')
        # Library licensing from the exact Ubuntu dependency root is included.
        for folder in (args.sysroot / 'usr/share/doc').glob('*') if (args.sysroot / 'usr/share/doc').exists() else []:
            if (folder / 'copyright').is_file():
                destination = docs / 'dependencies' / folder.name
                destination.mkdir(parents=True, exist_ok=True)
                shutil.copy2(folder / 'copyright', destination / 'copyright')
        schemas = args.sysroot / 'usr/share/glib-2.0/schemas/gschemas.compiled'
        if not schemas.is_file():
            raise FileNotFoundError('Missing GTK schemas in target sysroot')
        destination = app / 'usr/share/glib-2.0/schemas'
        destination.mkdir(parents=True)
        shutil.copy2(schemas, destination / 'gschemas.compiled')
        raw_tar = work / 'appdir.tar'
        tar_tree(app, raw_tar)
        filesystem = work / 'appdir.squashfs'
        packer = ROOT / '_tools/linux/squashfs/squashfs-tools-ng-1.3.2-mingw64/bin/tar2sqfs.exe'
        with raw_tar.open('rb') as stream:
            subprocess.run([str(packer), '-c', 'zstd', '-j', str(os.cpu_count() or 4), str(filesystem)], stdin=stream, check=True)
        image = out / f'kitty-ng-{version}-x86_64.AppImage'
        with image.open('wb') as output:
            for source in (runtime, filesystem):
                with source.open('rb') as stream:
                    shutil.copyfileobj(stream, output)
        tar_tree(app, raw_tar, 'KiTTY-ng.AppDir/')
        with raw_tar.open('rb') as source, (out / f'kitty-ng-{version}-x86_64.tar.zst').open('wb') as output:
            zstandard.ZstdCompressor(level=12).copy_stream(source, output)
        report = {'build_host': 'Windows', 'version': version, 'runtime': provenance,
                  'source_commit': build_report['source_commit'],
                  'working_tree_dirty': build_report['working_tree_dirty'],
                  'source_binaries': source_binaries,
                  'libraries': libraries, 'sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
                  'binaries': {name: hashlib.sha256((binaries / name).read_bytes()).hexdigest() for name in NAMES}, 'acceptance_tests': 'pending'}
        (out / 'linux-package.json').write_text(json.dumps(report, indent=2) + '\n')
    print('AppImage assembled locally on Windows; native acceptance tests required.')


if __name__ == '__main__':
    main()
