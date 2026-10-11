"""New portable script/icon migration cases, separate from unchanged key tests."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
def encode(value):
    return ''.join(f'%{b:02X}' if b <= 32 or b > 126 or chr(b) in '\\*?:/"<>|%' else chr(b)
                   for b in value.encode('utf8')).encode('ascii')
def field(name, value):
    return name.encode() + b'\\' + encode(value) + b'\\\r\n'

with tempfile.TemporaryDirectory(prefix='kitty-ng-extension-assets-') as temporary:
    root = Path(temporary) / 'package'; root.mkdir()
    sessions = root / 'Sessions'; sessions.mkdir()
    other = Path(temporary) / 'other'; other.mkdir()
    profile = sessions / 'assets'
    kept = field('Password', 'opaque-value') + field('UnknownLegacyOption', 'retain exactly')
    old = (kept + field('Scriptfile', str(root/'scripts/startup.sh')) +
           field('IconeFile', str(root/'icons/session.ico')) +
           field('Autocommand', str(root/'scripts/legacy.txt')))
    expected = (kept + field('Scriptfile', 'scripts\\startup.sh') +
                field('IconeFile', 'icons\\session.ico') + field('Autocommand', 'scripts\\legacy.txt'))
    profile.write_bytes(old)
    commands = sessions/'commands'
    unchanged = field('Autocommand', 'echo C:\\outside\\script.txt\\n')
    commands.write_bytes(unchanged)
    env = os.environ.copy(); env['KITTY_NG_PORTABLE_DIR'] = str(root)
    def run():
        return subprocess.check_output([str(binary), 'update-profiles'], env=env, cwd=other, text=True).strip()
    assert run() == '1 1 0'
    assert profile.read_bytes() == expected
    assert commands.read_bytes() == unchanged
    before = profile.stat().st_mtime_ns
    assert run() == '0 2 0'
    assert profile.stat().st_mtime_ns == before
print('PASS: portable script, ICO and legacy script references; literal commands unchanged; idempotence')
