"""Bulk migration edits only references in isolated synthetic profiles."""
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()

def encode(value):
    return ''.join(f'%{b:02X}' if b <= 32 or b > 126 or chr(b) in '\\*?:/"<>|%' else chr(b)
                   for b in value.encode('ascii')).encode('ascii')

def line(name, value, ending=b'\r\n'):
    return name.encode() + b'\\' + encode(value) + b'\\' + ending

with tempfile.TemporaryDirectory(prefix='kitty-ng-profile-update-') as temporary:
    base = Path(temporary)
    unrelated = base / 'unrelated'; unrelated.mkdir()
    for custom in (False, True):
        root = base / ('package-' + str(int(custom))); root.mkdir()
        sessions = root / ('profiles' if custom else 'Sessions')
        (sessions / 'nested/deeper').mkdir(parents=True)
        suffix = '.profile' if custom else ''
        if custom:
            (root / 'putty.conf').write_text('Sessions=profiles\nsessionsuffix=.profile\n')
        env = os.environ.copy(); env['KITTY_NG_PORTABLE_DIR'] = str(root)
        def run():
            result = subprocess.check_output([str(binary), 'update-profiles'],
                                             env=env, cwd=unrelated, text=True)
            return tuple(map(int, result.split()))
        absolute = str(root / 'SSH-Keys/client.ppk')
        certificate = str(root / 'SSH-Keys/certificate.pub')
        retained = b'; synthetic comment\r\nPassword\\encoded%2Fsecret\\\nUnknownKiTTYOption\\raw%25Value\\\r\nBinaryUnknown\\\x00opaque\\\n'
        default = sessions / ('Default%20Settings' + suffix)
        default.write_bytes(retained + line('PublicKeyFile', absolute))
        profile = sessions / ('nested/deeper/example' + suffix)
        external = str(root) + '-sibling\\external.ppk'
        old = (line('LogFileName', absolute) + line('PublicKeyFile', absolute, b'\n') +
               retained + line('DetachedCertificate', certificate, b'') + b'')
        expected = (line('LogFileName', absolute) + line('PublicKeyFile', 'SSH-Keys\\client.ppk', b'\n') +
                    retained + line('DetachedCertificate', 'SSH-Keys\\certificate.pub', b''))
        profile.write_bytes(old)
        unchanged = sessions / ('unchanged' + suffix)
        unchanged_bytes = (line('PublicKeyFile', external) + b'PublicKeyFile\\SSH-Keys%5cother.ppk\\\n' +
                           line('DetachedCertificate', '') + retained)
        unchanged.write_bytes(unchanged_bytes)
        ignored = sessions / 'download.tmp'; ignored.write_bytes(line('PublicKeyFile', absolute))
        if custom:
            wrong_suffix = sessions / 'unrelated.txt'; wrong_suffix.write_bytes(old)
        before = unchanged.stat().st_mtime_ns
        assert run() == (2, 1, 0)
        assert default.read_bytes() == retained + line('PublicKeyFile', 'SSH-Keys\\client.ppk')
        assert profile.read_bytes() == expected
        assert unchanged.read_bytes() == unchanged_bytes and unchanged.stat().st_mtime_ns == before
        assert ignored.read_bytes() == line('PublicKeyFile', absolute)
        if custom: assert wrong_suffix.read_bytes() == old
        times = {p: p.stat().st_mtime_ns for p in (default, profile, unchanged)}
        assert run() == (0, 3, 0)
        assert all(p.stat().st_mtime_ns == t for p, t in times.items())
        profile.write_bytes(old); profile.chmod(stat.S_IREAD)
        try:
            assert run() == (0, 2, 1)
            assert profile.read_bytes() == old
            # Only the deliberate ignored fixture exists; failed writes leave no temporary files.
            assert list(sessions.rglob('*.tmp')) == [ignored]
        finally:
            profile.chmod(stat.S_IREAD | stat.S_IWRITE)
        assert run() == (1, 2, 0)
    empty = base / 'empty'; empty.mkdir()
    env['KITTY_NG_PORTABLE_DIR'] = str(empty)
    assert run() == (0, 0, 0)
    env.pop('KITTY_NG_PORTABLE_DIR')
    subprocess.run([str(binary), 'update-registry'], env=env, cwd=unrelated, check=True)
print('PASS: recursive profiles, custom storage/suffix, exact preservation, idempotence, write failure and registry guard')
