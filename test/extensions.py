"""Synthetic legacy profiles; real user sessions and clipboard are untouched."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='kitty-ng-extensions-') as temporary:
    root = Path(temporary)
    (root/'kitty.ini').write_text('[KiTTY]\nReconnectDelay=7\ninitdelay=0.25\ncommanddelay=0.05\nbcdelay=0.02\nshortcuts=yes\n')
    (root/'Sessions').mkdir()
    opaque = root/'Sessions/unsupported'
    original = b'opaque-whole-file-encryption-fixture\n'
    opaque.write_bytes(original)
    (root/'Proxies').mkdir()
    (root/'Proxies/corp').write_text('ProxyMethod\\2\\\nProxyHost\\127.0.0.1\\\nProxyPort\\1080\\\nProxyUsername\\proxy-user\\\nProxyPassword\\proxy-pass\\\n')
    env = os.environ.copy(); env['KITTY_NG_PORTABLE_DIR'] = str(root)
    subprocess.run([str(binary)], env=env, cwd=root, check=True, timeout=25)
    assert opaque.read_bytes() == original
