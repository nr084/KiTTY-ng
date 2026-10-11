"""Exercise the actual tray command handler without starting a real agent."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

directory = Path(sys.argv[1]).resolve()
arch = sys.argv[2]
with tempfile.TemporaryDirectory(prefix='kitty-ng-kageant-language-') as temporary:
    env = os.environ.copy()
    env['KITTY_NG_PORTABLE_DIR'] = str(Path(temporary) / 'preferences')
    result = subprocess.run([str(directory / f'kageant-{arch}.exe'), '-test-language-menu'],
                            env=env, capture_output=True, timeout=25)
    assert result.returncode == 0, (result.returncode, result.stderr)
    # A new process must read the last selected language from the same preferences.
    subprocess.run([str(directory / 'test_utility_ui.exe'), 'read0'],
                   env=env, check=True, timeout=25)
print('PASS: Kageant tray language round-trip, persistence, raw session/key data and selection retained')
