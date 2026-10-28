"""Preserve tested provenance when only the portable-browser caption changes.

This deliberately narrow source proof does not claim binary identity. It checks
that all existing product sources, translations and tests are unchanged except
one GUI assignment and one new, GUI-only catalog entry. The changed caption
still needs a fresh native GUI check; no failed or unfinished test is reused.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import platform

KEY = 'Portable sessions'
VALUE = 'Портативные сеансы'
OLD_LINE = '    notice.stringValue = L(@"Portable sessions shared with the Windows version.", @"Портативные сеансы, общие с Windows-версией.");'
NEW_LINE = '    notice.stringValue = kr_mac_localize_string(@"Portable sessions");'


def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root).decode('utf-8').replace('\r\n', '\n')


def literal(text):
    return '"' + ''.join(chr(c) if 32 <= c < 127 and c not in (34, 92) else f'\\{c:03o}' for c in text.encode()) + '"'


def caption_proof(root, old, new):
    """Return a bounded proof, or None for every other source/test change."""
    try:
        changed = git(root, 'diff', '--name-only', old, new).splitlines()
        product = [p for p in changed if Path(p).suffix in ('.c', '.h', '.m', '.cmake', '.pl') or Path(p).name in ('CMakeLists.txt', 'VERSION')]
        if set(product) != {'macos/bootstrap.m', 'locale/builtin.h'}:
            return None
        if any(p.startswith('test/') and p != 'test/macos_caption_reuse.py' for p in changed):
            return None
        read = lambda commit, path: git(root, 'show', commit+':'+path)
        if read(old, 'VERSION') != read(new, 'VERSION'):
            return None
        a, b = read(old, 'macos/bootstrap.m'), read(new, 'macos/bootstrap.m')
        if a.count(OLD_LINE) != 1 or b.count(NEW_LINE) != 1 or a.replace(OLD_LINE, '<caption>') != b.replace(NEW_LINE, '<caption>'):
            return None
        if read(old, 'locale/ru.json') != read(new, 'locale/ru.json'):
            return None
        a, b = json.loads(read(old, 'locale/ru-contexts.json')), json.loads(read(new, 'locale/ru-contexts.json'))
        if KEY in a['macos-ui'] or b['macos-ui'].get(KEY) != VALUE:
            return None
        # Never introduce a reverse-translation ambiguity for an old message.
        common = json.loads(read(old, 'locale/ru.json'))
        if KEY in common or VALUE in common.values() or VALUE in a['macos-ui'].values():
            return None
        del b['macos-ui'][KEY]
        if a != b:
            return None
        a, b = read(old, 'locale/builtin.h'), read(new, 'locale/builtin.h')
        row = '    {'+literal(KEY)+', '+literal(VALUE)+'},\n'
        if b.count(row) != 1 or b.replace(row, '', 1) != a:
            return None
        start = b.index('static const KrMessage macos_ui_messages[] = {')
        if not start < b.index(row) < b.index('};', start):
            return None
        return dict(kind='unchanged-dependent-sources-caption-only', tested_source_commit=old,
                    packaged_source_commit=new, changed_sources=product,
                    new_gui_key=KEY, new_gui_translation=VALUE,
                    caption_requires_fresh_native_gui_check=True,
                    scope='Existing core/helper/quick-command tests; excludes the changed caption and unfinished tests')
    except (subprocess.CalledProcessError, KeyError, ValueError):
        return None


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def environment():
    root = Path(__file__).resolve().parents[1]
    metadata = list((root/'build/macos-test-venv').glob('lib/python*/site-packages/paramiko-*.dist-info/METADATA'))
    return dict(python=sys.version, os=platform.platform(), paramiko_metadata_sha256=sha(metadata[0]) if metadata else None)


def capture(output, evidence):
    build = json.loads((output/'macos-build.json').read_text())
    assert build['status'] == 'success' and not build['working_tree_dirty']
    evidence.mkdir(parents=True, exist_ok=False)
    captured = {}
    for name in ['macos-build.json', 'macos-dmg.json', 'quick-arm64.json', 'quick-gui-before-caption.json',
                 'quick-no-enter-arm64.json', 'runtime-arm64.json', 'runtime-x86_64.json',
                 'proxies-arm64.json', 'proxies-x86_64.json', 'certificates.json', 'keys-arm64.json', 'keys-x86_64.json']:
        path = output/name
        if path.exists():
            shutil.copy2(path, evidence/name)
            captured[name] = sha(path)
    shutil.copytree(output/'KiTTY-ng.app', evidence/'KiTTY-ng.app')
    shutil.copytree(output/'logs', evidence/'logs')
    for arch in ('arm64', 'x86_64'):
        report = json.loads((evidence/f'runtime-{arch}.json').read_text())
        assert report['status'] == 'passed'
        for helper, digest in report['binaries_sha256'].items():
            assert sha(evidence/'KiTTY-ng.app/Contents/Helpers'/helper) == digest
    quick = json.loads((evidence/'quick-arm64.json').read_text())
    assert quick['status'] == 'fixture-finished' and quick['no_secret_files'] and not quick['failures']
    assert quick['main_sha256'] == sha(evidence/'KiTTY-ng.app/Contents/MacOS/kitty-ng')
    (evidence/'capture.json').write_text(json.dumps(dict(files=captured, source_commit=build['source_commit'], environment=environment()), indent=2)+'\n')
    print('PASS: immutable successful test evidence captured from', build['source_commit'])


def reuse(root, output, evidence):
    capture_info = json.loads((evidence/'capture.json').read_text())
    assert capture_info['environment'] == environment(), 'Runtime test environment changed'
    for name, digest in capture_info['files'].items():
        assert sha(evidence/name) == digest, 'Original evidence changed: '+name
    old = json.loads((evidence/'macos-build.json').read_text())
    new = json.loads((output/'macos-build.json').read_text())
    assert new['status'] == 'success' and not new['working_tree_dirty']
    assert all(old[k] == new[k] for k in ('version', 'compiler', 'sdk', 'deployment_target', 'cmake', 'ninja', 'os', 'command_line_tools', 'build_packages'))
    proof = caption_proof(root, old['source_commit'], new['source_commit'])
    assert proof, 'A related source/test changed; run affected tests instead'
    proof['original_capture_sha256'] = sha(evidence/'capture.json')
    proof['original_evidence'] = str(evidence)
    proof_file = output/'caption-reuse-proof.json'
    proof_file.write_text(json.dumps(proof, ensure_ascii=False, indent=2)+'\n')
    retained = []
    for name in ['runtime-arm64.json', 'runtime-x86_64.json', 'proxies-arm64.json',
                 'proxies-x86_64.json', 'certificates.json', 'quick-arm64.json']:
        original = evidence/name
        if not original.exists():
            continue
        report = json.loads(original.read_text())
        assert report['status'] in ('passed', 'fixture-finished') and not report.get('failures')
        if 'binaries_sha256' in report:
            report['tested_binaries_sha256'] = report.get('tested_binaries_sha256', report['binaries_sha256'])
            report['binaries_sha256'] = {n:sha(output/'KiTTY-ng.app/Contents/Helpers'/n) for n in report['binaries_sha256']}
        # main_sha256 remains the actual executed binary, never the new binary.
        report.update(report_kind='reused-coverage-attestation', execution_reused=True, tested_source_commit=old['source_commit'],
                      packaged_source_commit=new['source_commit'],
                      packaged_main_sha256=sha(output/'KiTTY-ng.app/Contents/MacOS/kitty-ng'),
                      original_report=str(original), original_report_sha256=sha(original),
                      reuse_proof=str(proof_file), reuse_proof_sha256=sha(proof_file))
        (output/name).write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
        retained.append(name)
    print('PASS: retained successful unaffected executions:', ', '.join(retained))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['capture', 'reuse'])
    parser.add_argument('--output', type=Path, default=Path('dist/macos-native'))
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    if args.mode == 'capture':
        capture(args.output, args.evidence)
    else:
        reuse(Path(__file__).resolve().parents[1], args.output, args.evidence)
