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
    assert ("KiTTY-NG " + version).encode("utf-16le") in data, (name, "stale version resource")
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
    for name in ["README.md", "README.en.md", "LICENCE", "LICENCE-KiTTY.txt", "VERSION"]:
        bundle.write(root / name, name)
    bundle.write(root / "docs/migration.md", "docs/migration.md")
    bundle.write(root / "docs/kitty-feature-audit.json", "docs/kitty-feature-audit.json")
    bundle.write(root / "docs/kitty-migration-decisions.json", "docs/kitty-migration-decisions.json")
    bundle.write(root / "docs/putty-utilities-audit.json", "docs/putty-utilities-audit.json")
    bundle.write(root / "docs/screenshots/session-ru.png", "docs/screenshots/session-ru.png")
    bundle.write(root / "docs/screenshots/language-ru.png", "docs/screenshots/language-ru.png")
    bundle.write(root / "docs/screenshots/transfer-ru.png", "docs/screenshots/transfer-ru.png")
    for name in ("session-en.png", "language-en.png", "transfer-en.png", "about-ru.png", "about-en.png", "kittygen-ru.png", "kittygen-en.png",
                 "automation-ru.png", "automation-en.png", "extensions-ru.png", "extensions-en.png", "proxies-ru.png", "proxies-en.png"):
        bundle.write(root / "docs/screenshots" / name, "docs/screenshots/" + name)
    if (args.build / "compiler.json").exists():
        bundle.write(args.build / "compiler.json", "BUILD-INFO.json")
    for name in ("macos.md", "macos.en.md", "macos-migration.json"):
        bundle.write(root / "docs" / name, "docs/" + name)
    for image in sorted((root / "docs/screenshots").glob("macos-*.png")):
        bundle.write(image, "docs/screenshots/" + image.name)
    # Include the complete guide tree referenced by the packaged README.
    existing = set(bundle.namelist())
    for guide in sorted((root / "docs").rglob("*.md")):
        relative = guide.relative_to(root).as_posix()
        if relative not in existing:
            bundle.write(guide, relative)
    bundle.write(root / "icons/kitty-ng-forum.png", "icons/kitty-ng-forum.png")
    bundle.write(root / "icons/kitty-ng.png", "icons/kitty-ng.png")
    bundle.write(root / "icons/kitty-ng-prompt.txt", "icons/kitty-ng-prompt.txt")
    bundle.writestr("kitty-ng.portable", "")
    # Preserve the user's existing cryptsalt and path settings on extraction.
    bundle.writestr("kitty.ini.example", "[KiTTY]\nsavemode=dir\ncryptsalt=0\n")
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(args.output / f"SHA256SUMS-{args.arch}.txt").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
print(f"PASS: {archive.name}; PE architecture and version checked for all six executables")
