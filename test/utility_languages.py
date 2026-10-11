"""Native utility UI and console output, isolated from user preferences."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

directory = Path(sys.argv[1]).resolve()
arch = sys.argv[2]
with tempfile.TemporaryDirectory(prefix="kitty-ng-utility-language-") as temporary:
    root = Path(temporary)
    env = os.environ.copy()
    env["KITTY_NG_PORTABLE_DIR"] = str(root / "preferences")
    probe = directory / "test_utility_ui.exe"
    def run(command):
        return subprocess.run([str(x) for x in command], env=env, capture_output=True, timeout=25)
    for mode in ("default", "switch", "read0"):
        result = run([probe, mode]); assert result.returncode == 0, result.stderr
    for tool in ("klink", "kscp", "ksftp"):
        exe = directory / f"{tool}-{arch}.exe"
        english = run([exe, "--language", "en", "--help"])
        russian = run([exe, "--language", "ru", "--help"])
        assert english.returncode == russian.returncode == 0
        assert b"Usage:" in english.stdout and "Использование:" in russian.stdout.decode("utf-8")
        assert b"Options:" in english.stdout and "Параметры:" in russian.stdout.decode("utf-8")
        assert b"--language" in english.stdout and b"--language" in russian.stdout
        assert not russian.stderr, russian.stderr
        bad = run([exe, "--language", "ru", "-nonexistent-option"])
        assert bad.returncode != 0 and "неизвестный" in bad.stderr.decode("utf-8"), bad.stderr
    # No connection is opened; test interactive command help via a pipe.
    sftp = subprocess.run([str(directory / f"ksftp-{arch}.exe"), "--language", "ru"],
                          input=b"help\nhelp chmod\nquit\n", env=env, capture_output=True, timeout=25)
    text = sftp.stdout.decode("utf-8")
    assert "показать справку" in text and "Меняет права" in text, (sftp.returncode, text, sftp.stderr)
    screenshot = root / "generator.bmp"
    result = run([directory / f"kittygen-{arch}.exe", "--language", "en",
                  "-test-language-menu", "-demo-screenshot", screenshot])
    assert result.returncode == 0 and screenshot.exists(), (result.returncode, result.stderr)
    result = run([probe, "read0"]); assert result.returncode == 0
print("PASS: Windows default, persisted utility language, RU/EN console help and errors, SFTP help, real KiTTYgen key/menu preservation")
