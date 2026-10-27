"""Compile Linux ELF binaries locally on Windows using LLVM and a target sysroot.

The Ubuntu machine supplies headers/libraries and runs acceptance tests only.
No compiler or build command is executed over SSH by this script.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('kitty-ng', 'klink', 'kscp', 'ksftp', 'kittygen', 'kageant')
TARGETS = ('putty', 'plink', 'pscp', 'psftp', 'puttygen', 'pageant')


def unpack_sysroot(archive, destination):
    # Resolve target symlinks from an uncompressed archive. Random seeking in
    # gzip for every symlink would repeatedly decompress the whole sysroot.
    import gzip
    import tempfile
    with tempfile.TemporaryDirectory(prefix='kitty-ng-sysroot-') as folder:
        raw = Path(folder) / 'sysroot.tar'
        with gzip.open(archive, 'rb') as source, raw.open('wb') as output:
            shutil.copyfileobj(source, output)
        _unpack_sysroot(raw, destination)


def _unpack_sysroot(archive, destination):
    """Materialize Unix links without requiring Windows symlink privileges."""
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as bundle:
        members = {m.name.rstrip('/'): m for m in bundle.getmembers()}
        def resolve(name, seen):
            if name in seen:
                raise ValueError('Cyclic sysroot link: ' + name)
            seen = seen | {name}
            member = members.get(name)
            if member is None:
                # /lib and /lib64 are directory links on merged-/usr Ubuntu.
                parts = name.split('/')
                for i in range(1, len(parts)):
                    prefix = '/'.join(parts[:i])
                    parent = members.get(prefix)
                    if parent and parent.issym():
                        target = parent.linkname
                        target = target.lstrip('/') if target.startswith('/') else str(Path(prefix).parent / target).replace('\\', '/')
                        return resolve(target + '/' + '/'.join(parts[i:]), seen)
                raise ValueError('Missing sysroot link target: ' + name)
            if member.issym() or member.islnk():
                import posixpath
                target = member.linkname
                if member.issym() and not target.startswith('/'):
                    target = posixpath.join(posixpath.dirname(name), target)
                return resolve(posixpath.normpath(target).lstrip('/'), seen)
            return member
        for name, member in members.items():
            parts = Path(name).parts
            if name.startswith('/') or '..' in parts:
                raise ValueError('Unsafe sysroot archive member: ' + name)
            output = destination / name
            if member.isdir():
                output.mkdir(parents=True, exist_ok=True)
            elif member.isfile() or member.islnk() or member.issym():
                try:
                    source = resolve(name, set())
                except ValueError:
                    if member.issym():
                        continue  # Optional links to uninstalled packages.
                    raise
                if source.isfile():
                    output.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.extractfile(source) as input_file, output.open('wb') as output_file:
                        shutil.copyfileobj(input_file, output_file)
        for directory in ('lib', 'lib64'):
            if not (destination / directory).exists():
                if directory == 'lib':
                    shutil.copytree(destination / 'usr/lib', destination / directory, copy_function=os.link)
                else:
                    (destination / directory).mkdir()
                    shutil.copy2(destination / 'usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2', destination / directory / 'ld-linux-x86-64.so.2')


def run(arguments, **kwargs):
    subprocess.run([str(a) for a in arguments], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sysroot', type=Path, default=ROOT / '_tools/linux/sysroot')
    parser.add_argument('--sysroot-archive', type=Path)
    parser.add_argument('--metadata', type=Path, default=ROOT / '_tools/linux/target.json')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/linux-windows')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This entry point builds locally on Windows')
    if args.sysroot_archive:
        unpack_sysroot(args.sysroot_archive, args.sysroot)
    sysroot = args.sysroot.resolve()
    metadata = json.loads(args.metadata.read_text())
    from local_toolchain import ensure_local_toolchain
    llvm = ensure_local_toolchain(ROOT) / 'bin'
    cmake = ROOT / '_tools/macos/cmake/cmake/data/bin/cmake.exe'
    ninja = ROOT / '_tools/macos/ninja/ninja-1.13.2.data/scripts/ninja.exe'
    perl = ROOT / '_tools/macos/perl/SP_54231_64bit/perl/bin/perl.exe'
    # lld.exe dispatches by argv[0]. This is its ELF driver name.
    linker = llvm / 'ld.lld.exe'
    if not linker.exists():
        shutil.copy2(llvm / 'lld.exe', linker)
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    quote = lambda p: '"' + str(p).replace('\\', '/') + '"'
    gtk = build / 'target-gtk.cmake'
    cflags = shlex.split(metadata['gtk_cflags'])
    includes = [str(sysroot / flag[2:].lstrip('/')).replace('\\', '/') for flag in cflags if flag.startswith('-I')]
    libs = [flag[2:] for flag in shlex.split(metadata['gtk_libs']) if flag.startswith('-l')]
    gtk.write_text('set(GTK_FOUND TRUE)\nset(GTK_VERSION 3)\nset(GTK_INCLUDE_DIRS ' + ' '.join(map(quote, includes)) + ')\nset(GTK_LIBRARIES ' + ' '.join(libs) + ')\nset(GTK_LIBRARY_DIRS ' + quote(sysroot / 'usr/lib/x86_64-linux-gnu') + ')\n')
    toolchain = build / 'linux-toolchain.cmake'
    flags = '--gcc-toolchain=' + quote(sysroot / 'usr')
    toolchain.write_text('\n'.join([
        'set(CMAKE_SYSTEM_NAME Linux)',
        'set(CMAKE_SYSTEM_PROCESSOR x86_64)',
        'set(CMAKE_C_COMPILER ' + quote(llvm / 'clang.exe') + ')',
        'set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu)',
        'set(CMAKE_SYSROOT ' + quote(sysroot) + ')',
        'set(CMAKE_AR ' + quote(llvm / 'llvm-ar.exe') + ')',
        'set(CMAKE_RANLIB ' + quote(llvm / 'llvm-ranlib.exe') + ')',
        'set(CMAKE_C_FLAGS_INIT [=[' + flags + ']=])',
        'set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES ' + quote(sysroot / 'usr/include/x86_64-linux-gnu') + ')',
        'set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")',
        'set(CMAKE_FIND_ROOT_PATH ' + quote(sysroot) + ')',
        'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)',
        'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)',
        'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)',
        'set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)',
    ]) + '\n')
    environment = dict(os.environ)
    environment['PATH'] = str(llvm) + os.pathsep + str(perl.parent) + os.pathsep + environment['PATH']
    run([cmake, '-S', ROOT, '-B', build, '-G', 'Ninja',
         '-DCMAKE_TOOLCHAIN_FILE=' + str(toolchain), '-DCMAKE_MAKE_PROGRAM=' + str(ninja),
         '-DPERL_EXECUTABLE=' + str(perl), '-DKITTY_NG_TARGET_GTK=' + str(gtk),
         '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_SKIP_RPATH=ON', '-DPUTTY_GTK_VERSION=3', '-DPUTTY_GSSAPI=DYNAMIC'], env=environment)
    run([cmake, '--build', build, '--target', *TARGETS, '--parallel', str(os.cpu_count() or 4)], env=environment)
    import hashlib
    production = subprocess.check_output(['git', '-c', 'safe.directory=' + str(ROOT), 'ls-files'], cwd=ROOT, text=True).splitlines()
    production += [path.relative_to(ROOT).as_posix() for path in (ROOT / 'linux').rglob('*') if path.is_file()]
    source_digest = hashlib.sha256()
    for name in sorted(set(production)):
        path = ROOT / name
        if path.is_file() and (path.suffix in ('.c', '.h', '.cmake') or path.name in ('CMakeLists.txt', 'AppRun')):
            source_digest.update(name.encode('utf-8') + bytes([0]) + path.read_bytes())
    report = {'version': (ROOT/'VERSION').read_text().strip(), 'status':'success', 'source_commit':subprocess.check_output(['git','-c','safe.directory='+str(ROOT),'rev-parse','HEAD'],cwd=ROOT,text=True).strip(), 'working_tree_dirty':bool(subprocess.check_output(['git','-c','safe.directory='+str(ROOT),'status','--porcelain'],cwd=ROOT)), 'source_sha256': source_digest.hexdigest(), 'build_host': 'Windows', 'target': 'x86_64-linux-gnu', 'sysroot': metadata,
              'compiler': subprocess.check_output([llvm / 'clang.exe', '--version'], text=True).splitlines()[0],
              'binaries': {name: hashlib.sha256((build / name).read_bytes()).hexdigest() for name in NAMES}}
    (build / 'linux-cross-build.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Linux binaries compiled locally on Windows. Native acceptance tests required.')


if __name__ == '__main__':
    main()
