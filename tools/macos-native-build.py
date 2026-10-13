"""Build and test Universal 2 locally on macOS; never publish a release."""
import argparse
import hashlib
import importlib.util
import json
import os
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STORE = ROOT/'_tools/macos/native'
TARGETS = dict(plink='klink', pscp='kscp', psftp='ksftp', puttygen='kittygen', pageant='kageant', **{'kitty-ng-macos': 'kitty-ng'})
UNIT_TESTS = ('test_bpp', 'test_host_strfoo', 'test_decode_utf8', 'test_unicode_norm', 'test_tree234', 'test_wildcard', 'test_cert_expr')


def run(command, log=None, env=None):
    command = [str(x) for x in command]
    if log:
        with log.open('wb') as stream:
            result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, env=env)
        if result.returncode:
            print(log.read_text(errors='replace')[-8000:])
            raise RuntimeError(f'Command failed ({result.returncode}): {command}; log: {log}')
    else:
        subprocess.run(command, cwd=ROOT, check=True, env=env)


def output(command):
    return subprocess.check_output([str(x) for x in command], cwd=ROOT, text=True).strip()


def source_hash():
    digest = hashlib.sha256()
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = sorted(d for d in dirs if d not in ('_tools', 'build', 'dist', '.git', '.github', '__pycache__'))
        for name in sorted(files):
            path = Path(base)/name
            if path.suffix in ('.c', '.h', '.m', '.cmake', '.pl') or name in ('CMakeLists.txt', 'VERSION'):
                digest.update(path.relative_to(ROOT).as_posix().encode()+b'\0'+path.read_bytes())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--offline', action='store_true')
    parser.add_argument('--compiler', type=Path, help='Explicit compiler for diagnostic builds')
    parser.add_argument('--deployment-target', default='13.0')
    parser.add_argument('--output', type=Path, default=ROOT/'dist/macos-native')
    parser.add_argument('--force-tests', action='store_true')
    parser.add_argument('--skip-dmg', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Native build requires macOS')
    if not args.offline:
        run([sys.executable, ROOT/'tools/fetch-macos-native-tools.py'])
    candidates = sorted((STORE/'llvm').glob('*/toolchain-info.json'), key=lambda p:tuple(map(int,p.parent.name.split('.'))))
    compiler = args.compiler or (candidates[-1].parent/'bin/clang' if candidates else None)
    if not compiler or not compiler.is_file():
        parser.error('Install native tools first, or supply --compiler for a diagnostic build')
    cmake = STORE/'cmake/cmake/data/bin/cmake'
    ninja = next(p for p in (STORE/'ninja').rglob('ninja') if p.is_file())
    sdk = output(['xcrun', '--show-sdk-path'])
    version = (ROOT/'VERSION').read_text().strip()
    dest = args.output.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    logs = dest/'logs'
    logs.mkdir(exist_ok=True)
    report = dict(version=version, status='building', platform='macos', host_architecture=platform.machine(),
        architectures=['arm64', 'x86_64'], deployment_target=args.deployment_target,
        compiler=output([compiler, '--version']), cmake=output([cmake, '--version']),
        ninja=output([ninja, '--version']), sdk=dict(path=sdk, version=output(['xcrun','--show-sdk-version'])),
        os=output(['sw_vers']), source_sha256=source_hash(), source_commit=None,
        runtime_verified=False, native_tests={})
    report['llvm']=json.loads(candidates[-1].read_text()) if candidates and compiler.resolve().is_relative_to(candidates[-1].parent) else None
    report['build_packages']=json.loads((STORE/'packages.json').read_text())
    report['command_line_tools']=output(['pkgutil','--pkg-info','com.apple.pkg.CLTools_Executables'])
    git = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture_output=True, text=True)
    if git.returncode == 0:
        report['source_commit'] = git.stdout.strip()
        report['working_tree_dirty'] = bool(output(['git','status','--porcelain']))
    metadata = STORE/'host-info.json'
    metadata.parent.mkdir(parents=True, exist_ok=True)
    metadata.write_text(json.dumps({key:report[key] for key in ('compiler','cmake','ninja','sdk','os')}, indent=2)+'\n')
    fingerprint = hashlib.sha256(json.dumps({k:report[k] for k in ('source_sha256','compiler','sdk','deployment_target')}, sort_keys=True).encode()).hexdigest()
    previous = json.loads((dest/'macos-build.json').read_text()) if (dest/'macos-build.json').exists() else {}
    report['fingerprint'] = fingerprint
    environment = os.environ.copy()
    environment.update(LC_ALL='C', LANG='C')
    (ROOT/'build').mkdir(exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix='macos-native-', dir=ROOT/'build') as temporary:
            work = Path(temporary)
            app = work/'KiTTY-ng.app'
            for directory in ('MacOS','Helpers','Resources'):
                (app/'Contents'/directory).mkdir(parents=True)
            binaries = {}
            for arch in report['architectures']:
                # The compiled commit string is part of every executable.
                compile_fingerprint=hashlib.sha256((fingerprint+str(report['source_commit'])).encode()).hexdigest()
                cache = STORE/'cache'/compile_fingerprint/arch
                cache.mkdir(parents=True, exist_ok=True)
                manifest = cache/'files.json'
                cached = json.loads(manifest.read_text()) if manifest.exists() else {}
                expected = set(TARGETS.values()) | set(UNIT_TESTS)
                reused = set(cached) == expected and all((cache/n).exists() and hashlib.sha256((cache/n).read_bytes()).hexdigest()==v for n,v in cached.items())
                if not reused:
                    directory = work/arch
                    print(arch, 'configure and compile', flush=True)
                    run([cmake,'-S',ROOT,'-B',directory,'-G','Ninja',
                        '-DCMAKE_OSX_ARCHITECTURES='+arch,'-DCMAKE_OSX_DEPLOYMENT_TARGET='+args.deployment_target,
                        '-DCMAKE_OSX_SYSROOT='+sdk,'-DCMAKE_C_COMPILER='+str(compiler),
                        '-DCMAKE_OBJC_COMPILER='+str(compiler),'-DCMAKE_MAKE_PROGRAM='+str(ninja),
                        '-DPERL_EXECUTABLE=/usr/bin/perl','-DPUTTY_GTK_VERSION=NONE','-DPUTTY_GSSAPI=DYNAMIC',
                        '-DCMAKE_BUILD_TYPE=Release'], logs/(arch+'-configure.log'), environment)
                    run([cmake,'--build',directory,'--target',*TARGETS,*UNIT_TESTS,'--parallel','8'],logs/(arch+'-build.log'),environment)
                    for target,name in TARGETS.items():
                        binary = directory/'macos'/target if target=='kitty-ng-macos' else directory/target
                        shutil.copy2(binary,cache/name)
                    for name in UNIT_TESTS:
                        shutil.copy2(directory/name,cache/name)
                    manifest.write_text(json.dumps({n:hashlib.sha256((cache/n).read_bytes()).hexdigest() for n in sorted(expected)},indent=2)+'\n')
                else:
                    print('REUSE: unchanged compilation', arch, flush=True)
                binaries[arch] = cache
                checks = dict(previous.get('native_tests',{}).get(arch,{})) if previous.get('fingerprint')==fingerprint else {}
                report['native_tests'][arch] = checks
                for name in UNIT_TESTS:
                    if checks.get(name)=='passed' and not args.force_tests:
                        continue
                    command = ['/usr/bin/arch','-'+arch,cache/name]
                    if name=='test_unicode_norm':
                        vectors=work/'normalization.txt'
                        vectors.write_text('00C5;00C5;0041 030A;00C5;0041 030A;\n212B;00C5;0041 030A;00C5;0041 030A;\nFB01;FB01;FB01;0066 0069;0066 0069;\nAC00;AC00;1100 1161;AC00;1100 1161;\n')
                        command.append(vectors)
                    run(command,logs/(arch+'-'+name+'.log'),environment)
                    checks[name] = 'passed'
            for name in TARGETS.values():
                executable = app/'Contents'/('MacOS' if name=='kitty-ng' else 'Helpers')/name
                run(['lipo','-create',*[binaries[a]/name for a in report['architectures']],'-output',executable])
                executable.chmod(0o755)
                if set(output(['lipo','-archs',executable]).split()) != {'arm64','x86_64'}:
                    raise RuntimeError('Expected exactly Universal 2: '+str(executable))
                headers=output(['otool','-arch','all','-l',executable])
                minimums=re.findall(r'^\s*minos\s+(\S+)',headers,re.MULTILINE)
                def normalized(value):
                    parts=value.split('.')
                    return tuple(map(int,parts+['0']*(3-len(parts))))
                if len(minimums)!=2 or any(normalized(v)!=normalized(args.deployment_target) for v in minimums):
                    raise RuntimeError('Unexpected Mach-O deployment targets: '+str(minimums))
                report.setdefault('binary_deployment_targets',{})[name]=minimums
                libs = output(['otool','-arch','all','-L',executable])
                for line in libs.splitlines():
                    if not line.startswith('\t'):
                        continue
                    dependency = line.strip().split(' (')[0]
                    if dependency.startswith(('/', '@')) and not dependency.startswith(('/usr/lib/','/System/Library/')):
                        raise RuntimeError('Non-system runtime dependency: '+dependency)
                run(['codesign','--force','--sign','-','--timestamp=none',executable])
            info = dict(CFBundleName='KiTTY-ng',CFBundleDisplayName='KiTTY-ng',CFBundleExecutable='kitty-ng',
                CFBundleIdentifier='com.nr084.kitty-ng',CFBundlePackageType='APPL',
                CFBundleShortVersionString=version.split('-')[0],CFBundleVersion=version.replace('-','.'),
                LSMinimumSystemVersion=args.deployment_target,NSHighResolutionCapable=True,CFBundleIconFile='kitty-ng.icns')
            (app/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
            sys.path.insert(0,str(STORE/'pillow'))
            from PIL import Image
            Image.open(ROOT/'icons/kitty-ng.png').save(app/'Contents/Resources/kitty-ng.icns',format='ICNS')
            for name in ('LICENCE','LICENCE-KiTTY.txt'):
                shutil.copy2(ROOT/name,app/'Contents/Resources'/name)
            provenance={key:report[key] for key in ('version','architectures','deployment_target','compiler','llvm','cmake','ninja','build_packages','command_line_tools','sdk','os','source_sha256','source_commit')}
            (app/'Contents/Resources/BUILD-INFO.json').write_text(json.dumps(provenance,indent=2)+'\n')
            run(['codesign','--force','--sign','-','--timestamp=none',app])
            run(['codesign','--verify','--deep','--strict','--verbose=2',app])
            run([sys.executable,ROOT/'tools/verify-macos.py',app])
            # Verify that the native verifier rejects modified code and resources.
            spec=importlib.util.spec_from_file_location('verify_macos',ROOT/'tools/verify-macos.py')
            validator=importlib.util.module_from_spec(spec)
            spec.loader.exec_module(validator)
            executable=app/'Contents/MacOS/kitty-ng'
            original=executable.read_bytes()
            changed=bytearray(original)
            for cpu,thin in validator.slices(original):
                changed[original.find(thin)+4096]^=1
            executable.write_bytes(changed)
            try:
                result=subprocess.run(['codesign','--verify','--all-architectures','--strict',str(app)],capture_output=True)
                if result.returncode==0:raise RuntimeError('Native verifier accepted modified code')
            finally:executable.write_bytes(original)
            resource=app/'Contents/Resources/BUILD-INFO.json'
            original=resource.read_bytes()
            resource.write_bytes(original+b' ')
            try:
                result=subprocess.run(['codesign','--verify','--deep','--strict',str(app)],capture_output=True)
                if result.returncode==0:raise RuntimeError('Native verifier accepted modified resource')
            finally:resource.write_bytes(original)
            report['tamper_checks']='passed'
            for name in ('Smoke-test KiTTY-ng.command','Diagnose KiTTY-ng.command'):
                script = work/name
                script.write_bytes((ROOT/'macos'/name).read_bytes().replace(b'\r\n',b'\n'))
                script.chmod(0o755)
            for arch in report['architectures']:
                if report['native_tests'][arch].get('smoke')=='passed' and not args.force_tests:
                    print('REUSE: passed native smoke checks',arch,flush=True)
                    continue
                test_env=environment.copy()
                test_env['KITTY_NG_TEST_ARCH']=arch
                run(['/bin/zsh',work/'Smoke-test KiTTY-ng.command'],logs/('smoke-'+arch+'.log'),test_env)
                report['native_tests'][arch]['smoke']='passed'
            for arch in report['architectures']:
                prior_keys=dest/('keys-'+arch+'.json')
                if previous.get('fingerprint')==fingerprint and report['native_tests'][arch].get('keys')=='passed' and prior_keys.exists() and not args.force_tests:
                    # A new commit changes its embedded string, not key logic.
                    evidence=json.loads(prior_keys.read_text())
                    assert evidence['status']=='passed'
                    report.setdefault('reused_key_evidence',{})[arch]=dict(tested_binary_sha256=evidence['binary_sha256'],
                        packaged_binary_sha256=hashlib.sha256((app/'Contents/Helpers/kittygen').read_bytes()).hexdigest(),
                        reason='Same source/compiler/SDK/deployment fingerprint; commit provenance only')
                    print('REUSE: passed key interoperability',arch,flush=True)
                else:
                    run([sys.executable,ROOT/'test/macos_native_keys.py',app,'--arch',arch,'--report',prior_keys])
                report['native_tests'][arch]['keys']='passed'
            report['runtime_verified'] = True
            report['fingerprint'] = fingerprint
            report['status'] = 'success'
            final_app = dest/'KiTTY-ng.app'
            if final_app.exists():
                shutil.rmtree(final_app)
            shutil.copytree(app,final_app)
            for name in ('Smoke-test KiTTY-ng.command','Diagnose KiTTY-ng.command'):
                shutil.copy2(work/name,dest/name)
            archive = dest/f'kitty-ng-{version}-macos-universal.zip'
            run(['ditto','-c','-k','--keepParent',app,archive])
            report['archive'] = archive.name
            report['sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
            if not args.skip_dmg:
                payload = work/'payload'
                payload.mkdir()
                shutil.copytree(app,payload/'KiTTY-ng.app')
                shutil.copytree(ROOT/'docs',payload/'Instructions/docs')
                for name in ('README.md','README.en.md','LICENCE','LICENCE-KiTTY.txt'):
                    shutil.copy2(ROOT/name,payload/'Instructions'/name)
                icons=payload/'Instructions/icons'
                icons.mkdir()
                for name in ('kitty-ng.png','kitty-ng-forum.png','kitty-ng-prompt.txt'):
                    shutil.copy2(ROOT/'icons'/name,icons/name)
                for name in ('Smoke-test KiTTY-ng.command','Diagnose KiTTY-ng.command'):
                    shutil.copy2(work/name,payload/'Instructions'/name)
                dmg = dest/f'kitty-ng-{version}-macos-universal.dmg'
                run(['hdiutil','create','-ov','-format','UDZO','-fs','HFS+','-volname','KiTTY-ng '+version+' Universal','-srcfolder',payload,dmg],logs/'dmg.log')
                run(['hdiutil','verify',dmg],logs/'dmg-verify.log')
                mount=work/'mounted'
                mount.mkdir()
                run(['hdiutil','attach','-readonly','-nobrowse','-mountpoint',mount,dmg],logs/'dmg-mount.log')
                try:
                    run(['codesign','--verify','--deep','--strict',mount/'KiTTY-ng.app'],logs/'dmg-signature.log')
                    for source in app.rglob('*'):
                        if source.is_file():
                            restored=mount/'KiTTY-ng.app'/source.relative_to(app)
                            if restored.read_bytes()!=source.read_bytes():raise RuntimeError('DMG round-trip changed '+str(source))
                            if source.stat().st_mode&0o111 and not restored.stat().st_mode&0o111:raise RuntimeError('DMG lost executable mode')
                finally:run(['hdiutil','detach',mount],logs/'dmg-unmount.log')
                report['dmg'] = dict(version=version,status='success',file=dmg.name,sha256=hashlib.sha256(dmg.read_bytes()).hexdigest(),
                    size=dmg.stat().st_size,architectures=report['architectures'],filesystem='HFS+',compression='zlib',
                    bundle_hashes='verified after native mount',darwin_execute_modes='verified',native_mount_verified=True)
                (dest/'macos-dmg.json').write_text(json.dumps(report['dmg'],indent=2)+'\n')
            checksum=f"{report['sha256']}  {report['archive']}\n"
            if 'dmg' in report:checksum+=f"{report['dmg']['sha256']}  {report['dmg']['file']}\n"
            (dest/'SHA256SUMS.txt').write_text(checksum)
            print('PASS: native Universal 2 build and runtime checks:',final_app,flush=True)
    except BaseException as error:
        report.update(status='failed',error=str(error))
        raise
    finally:
        (dest/'macos-build.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__':
    main()
