"""Verify selection/apply events and durable language across fresh processes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
import winreg

binary = Path(sys.argv[1]).resolve()
environment = os.environ.copy()
environment.pop("KITTY_NG_PORTABLE_DIR", None)

def verify(command):
    for step in ("read0", "pending1", "read0", "set1", "read1", "set0", "read0"):
        subprocess.run([*command, step], env=environment, check=True)
    subprocess.run([*command, "set1"], env=environment, check=True)
    # An explicit launch override must still allow Apply to update preferences.
    subprocess.run([*command, "--language", "en", "set0"], env=environment, check=True)
    subprocess.run([*command, "read0"], env=environment, check=True)

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

with tempfile.TemporaryDirectory(prefix="kitty-ng-language-") as directory:
    root = Path(directory)
    executable = root / binary.name
    shutil.copy2(binary, executable)
    portable = root / "portable"
    portable.mkdir()
    verify([str(executable), "--portable-dir", str(portable)])
    registry = "Software\\kitty-ng-language-test-" + uuid.uuid4().hex
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, registry):
        pass
    try:
        verify([str(executable), "--registry", registry])
    finally:
        remove_tree(registry)
print("PASS: Apply persists English/Russian across restarts; selection alone does not save")
