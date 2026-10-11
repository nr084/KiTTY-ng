"""Exercise real registry storage under an isolated, temporary HKCU root."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid
import winreg

binary = Path(sys.argv[1]).resolve()
root = "Software\\kitty-ng-registry-test-" + uuid.uuid4().hex

def remove_tree(path):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path, 0, winreg.KEY_ALL_ACCESS) as key:
        while True:
            try:
                child = winreg.EnumKey(key, 0)
            except OSError as error:
                if error.winerror == 259:
                    break
                raise
            remove_tree(path + "\\" + child)
    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, path)

with winreg.CreateKey(winreg.HKEY_CURRENT_USER, root):
    pass
try:
    environment = os.environ.copy()
    environment.pop("KITTY_NG_PORTABLE_DIR", None)
    # Copy outside a portable installation, so only registry mode is active.
    with tempfile.TemporaryDirectory(prefix="kitty-ng-registry-") as directory:
        import shutil
        executable = Path(directory) / binary.name
        shutil.copy2(binary, executable)
        subprocess.run([str(executable), "--registry-test", root], check=True, env=environment)
finally:
    assert root.startswith("Software\\kitty-ng-registry-test-")
    remove_tree(root)
