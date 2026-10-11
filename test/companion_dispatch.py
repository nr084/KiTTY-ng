"""Real dispatch: same directory/architecture, PATH decoys and exact Unicode argv."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build_names import main_executable, utility_executable, UTILITY_NAMES

directory = Path(sys.argv[1]).resolve()
arch = sys.argv[2]
with tempfile.TemporaryDirectory(prefix="companion-test-", dir=directory.parent) as temporary:
    root = Path(temporary)
    installation = root / "Клиент с пробелами"
    decoy = root / "PATH decoy"
    installation.mkdir(); decoy.mkdir()
    client = installation / main_executable(arch)
    shutil.copy2(directory / client.name, client)
    probe = directory / "test_companion_probe.exe"
    for program in UTILITY_NAMES:
        name = utility_executable(program, arch)
        shutil.copy2(probe, installation / name)
        shutil.copy2(probe, decoy / name)
    environment = os.environ.copy()
    environment["PATH"] = str(decoy) + os.pathsep + environment.get("PATH", "")
    def launch(tool, report, *arguments):
        return subprocess.run([str(client), "--portable-dir", str(root / "settings"),
            "-launch-utility", tool, str(report), *arguments], cwd=decoy,
            env=environment, timeout=15, capture_output=True)
    arguments = ["", "путь с пробелами", 'a"b', "trailing\\", "two\\\\", "x\\\"y", "& literal"]
    for program, tool in UTILITY_NAMES.items():
        report = root / (tool + ".argv")
        result = launch(tool, report, *arguments)
        assert result.returncode == 0, (tool, result.returncode, result.stderr)
        recorded = [part.decode("utf8") for part in report.read_bytes().split(b"\0")[:-1]]
        assert recorded == [str(installation / utility_executable(program, arch)), str(report), *arguments], recorded
    target = installation / utility_executable("pscp", arch)
    target.unlink()
    report = root / "must-not-run.argv"
    assert launch("kscp", report).returncode == 2  # No PATH fallback.
    assert not report.exists()
    data = bytearray(probe.read_bytes())
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    struct.pack_into("<H", data, offset + 4, 0x14c if arch == "x64" else 0x8664)
    target.write_bytes(data)
    assert launch("kscp", report).returncode == 193  # Wrong PE architecture.
    assert not report.exists()
    target.write_bytes(b"invalid executable")
    assert launch("kscp", report).returncode == 193
    assert not report.exists()
print("PASS:", arch, "five same-architecture companions; no PATH fallback; wrong/corrupt PE rejected; exact Unicode argv")
