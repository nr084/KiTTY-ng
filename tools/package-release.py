"""Package a Windows build without intermediate files or personal settings."""
import argparse
import hashlib
import struct
import re
import zipfile
from pathlib import Path
from build_names import release_executables

parser = argparse.ArgumentParser()
parser.add_argument("arch", choices=["x64", "x86", "arm64"])
parser.add_argument("build", type=Path)
parser.add_argument("--output", type=Path, default=Path("dist"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text().strip()
machine = {"x64": 0x8664, "x86": 0x14c, "arm64": 0xaa64}[args.arch]
names = release_executables(args.arch)
base = re.match(r"(\d+)\.(\d+)", version)
assert base
major, minor = map(int, base.groups())
suffix = version[len(base.group()):]
revision = int(suffix[1:]) if re.fullmatch(r"-[1-9][0-9]*", suffix) else 0
fixed_version = ((major << 16) | minor, revision << 16)
for name in names:
    data = (args.build / name).read_bytes()
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    assert data[:2] == b"MZ" and data[offset:offset+4] == b"PE\0\0"
    assert struct.unpack_from("<H", data, offset+4)[0] == machine, name
    assert ("KiTTY-ng " + version).encode("utf-16le") in data, (name, "stale version resource")
    fixed_offset = data.find(struct.pack("<I", 0xFEEF04BD))
    assert fixed_offset >= 0, (name, "missing fixed version info")
    fixed = struct.unpack_from("<13I", data, fixed_offset)
    assert fixed[2:6] == fixed_version * 2, (name, "wrong binary version")
    assert bool(fixed[7] & 2) == ("-test" in version), (name, "wrong prerelease flag")
args.output.mkdir(parents=True, exist_ok=True)
archive = args.output / f"kitty-ng-{version}-{args.arch}.zip"
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    for name in names:
        bundle.write(args.build / name, name)
    # Keep runtime packages minimal; guides, artwork and build reports live online.
    for name in ("LICENCE", "LICENCE-KiTTY.txt"):
        bundle.write(root / name, name)
    bundle.writestr("kitty-ng.portable", "")
with zipfile.ZipFile(archive) as bundle:
    assert set(bundle.namelist()) == set(names) | {"LICENCE", "LICENCE-KiTTY.txt", "kitty-ng.portable"}

digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(args.output / f"SHA256SUMS-{args.arch}.txt").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
print(f"PASS: {archive.name}; PE architecture and version checked for all six executables")
