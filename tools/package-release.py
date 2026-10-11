"""Package a Windows build without intermediate files or personal settings."""
import argparse
import hashlib
import struct
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("arch", choices=["x64", "x86", "arm64"])
parser.add_argument("build", type=Path)
parser.add_argument("--output", type=Path, default=Path("dist"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text().strip()
machine = {"x64": 0x8664, "x86": 0x14c, "arm64": 0xaa64}[args.arch]
names = ["kitty-rus", "plink", "pscp", "psftp", "puttygen", "pageant"]
for name in names:
    data = (args.build / (name + ".exe")).read_bytes()
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    assert data[:2] == b"MZ" and data[offset:offset+4] == b"PE\0\0"
    assert struct.unpack_from("<H", data, offset+4)[0] == machine, name
    assert ("KiTTY-RUS " + version).encode("utf-16le") in data, (name, "stale version resource")
args.output.mkdir(parents=True, exist_ok=True)
archive = args.output / f"KiTTY-RUS-{version}-{args.arch}.zip"
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    for name in names:
        bundle.write(args.build / (name + ".exe"), name + ".exe")
    for name in ["README.md", "LICENCE", "LICENCE-KiTTY.txt", "VERSION"]:
        bundle.write(root / name, name)
    bundle.write(root / "docs/migration.md", "docs/migration.md")
    bundle.write(root / "docs/kitty-feature-audit.json", "docs/kitty-feature-audit.json")
    bundle.write(root / "docs/screenshots/session-ru.png", "docs/screenshots/session-ru.png")
    bundle.write(root / "icons/kitty-rus.png", "icons/kitty-rus.png")
    bundle.write(root / "icons/kitty-rus-prompt.txt", "icons/kitty-rus-prompt.txt")
    bundle.writestr("kitty-rus.portable", "")
    # Preserve the user's existing cryptsalt and path settings on extraction.
    bundle.writestr("kitty.ini.example", "[KiTTY]\nsavemode=dir\ncryptsalt=0\n")
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(args.output / f"SHA256SUMS-{args.arch}.txt").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
print(f"PASS: {archive.name}; PE architecture and version checked for all six executables")
