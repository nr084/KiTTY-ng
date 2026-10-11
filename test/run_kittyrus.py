"""Verify legacy file compatibility using synthetic KiTTY portable sessions."""
import subprocess
import sys
import tempfile
from pathlib import Path

binary = Path(sys.argv[1]).resolve()
for suffix, custom in [("", False), (".ktx", False), (".session", True)]:
  with tempfile.TemporaryDirectory(prefix="kitty-rus-test-") as directory:
    root = Path(directory)
    session_base = "my sessions" if custom else "Sessions"
    host_base = "known keys" if custom else "SshHostKeys"
    (root / "kitty.ini").write_text(f"[KiTTY]\nfileextension={suffix}\nhostkeyextension=.khk\n")
    if custom:
        (root / "putty.conf").write_text(
            f"Sessions={session_base}\nSshHostKeys={host_base}\nsessionsuffix={suffix}\nkeysuffix=.khk\n")
    sessions = root / session_base / "Production East"
    sessions.mkdir(parents=True)
    (sessions / ("root-session" + suffix)).write_bytes(
        b"HostName\\legacy.example.com\\\n"
        b"PortNumber\\2222\\\n"
        b"UserName\\root\\\n"
        b"LegacyUnknownOption\\preserve%20this\\\n")
    hostkeys = root / host_base
    hostkeys.mkdir()
    (hostkeys / "rsa@2222%3Alegacy.example.com.khk").write_bytes(b"0x10001,0xabcdef")
    subprocess.run([str(binary), "--portable-dir", str(root)], check=True)
    saved = (sessions / ("root-session" + suffix)).read_bytes()
    assert b"LegacyUnknownOption\\preserve%20this\\" in saved
    assert b"TestMultiline\\first%0Asecond%5Cthird%25fourth\\" in saved
    assert b"synthetic-root-password" not in saved
print("PASS: original KiTTY file syntax retained; no plaintext test password stored")
