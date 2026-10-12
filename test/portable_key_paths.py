"""Exercise actual Windows storage with relocated, isolated packages."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
import winreg

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="kitty-ng-keypaths-") as temporary:
    base = Path(temporary).resolve()
    package = base / "Portable KiTTY-ng"
    (package / "SSH-Keys").mkdir(parents=True)
    for name in ("client.ppk", "certificate.pub"):
        (package / "SSH-Keys" / name).write_bytes(b"synthetic fixture\n")
    unrelated = base / "unrelated"; unrelated.mkdir()
    env = os.environ.copy(); env["KITTY_NG_PORTABLE_DIR"] = str(package)
    for custom in (False, True):
        if custom: (package / "putty.conf").write_text("Sessions=profiles\n")
        subprocess.run([str(binary), "save"], env=env, cwd=unrelated, check=True)
        session_base = package / ("profiles" if custom else "Sessions")
        stored = (session_base / "Folder/keys").read_bytes()
        assert b"PublicKeyFile\\SSH-Keys%5Cclient.ppk\\" in stored
        assert b"DetachedCertificate\\SSH-Keys%5Ccertificate.pub\\" in stored
        assert str(package).encode() not in stored
        moved = base / ("Перенесённый KiTTY-ng 😀" + str(int(custom)))
        assert package.resolve().is_relative_to(base) and moved.resolve().is_relative_to(base)
        package.rename(moved); package = moved
        env["KITTY_NG_PORTABLE_DIR"] = str(package)
        subprocess.run([str(binary), "check"], env=env, cwd=unrelated, check=True)
    # The registry backend keeps standard PuTTY relative-path semantics.
    env.pop("KITTY_NG_PORTABLE_DIR")
    key = "Software\\kitty-ng-keypaths-test-" + uuid.uuid4().hex
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, key): pass
    try:
        subprocess.run([str(binary), "--registry", key], env=env, cwd=unrelated, check=True)
    finally:
        for suffix in ("\\Software\\KiTTY-NG\\Sessions\\keys", "\\Software\\KiTTY-NG\\Sessions",
                       "\\Software\\KiTTY-NG", "\\Software", ""):
            try: winreg.DeleteKey(winreg.HKEY_CURRENT_USER, key + suffix)
            except FileNotFoundError: pass
print("PASS: legacy portable values, relocated Unicode package, custom session folder and registry isolation")
