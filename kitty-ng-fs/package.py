#!/usr/bin/env python3
"""Create separate verified Total Commander installer and source archives."""
import argparse
import configparser
import hashlib
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parent
DOCS = ("README.md", "README.en.md", "README.ru.md", "LICENSE", "THIRD_PARTY_NOTICES.md",
        "CHANGELOG.md", "VALIDATION.md", "PUBLICATION.md", "kittyngfs.example.ini")
SOURCE_FILES = (*DOCS, "pluginst.inf", "CMakeLists.txt", "build-cross.sh", "build-windows.cmd",
                "build-local.py", "package.py", ".gitignore", "AGENTS.md", "PLAN.md", "REVIEW.md")

def machine(data):
    if len(data) < 64 or data[:2] != b"MZ": raise ValueError("Missing MZ header")
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if offset + 24 > len(data) or data[offset:offset+4] != b"PE\0\0": raise ValueError("Missing PE header")
    if not struct.unpack_from("<H", data, offset+22)[0] & 0x2000: raise ValueError("Not a DLL")
    return struct.unpack_from("<H", data, offset+4)[0]

def write_zip(path, members):
    manifest = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name,data in sorted(members.items()))
    members = {**members, "SHA256SUMS.txt": manifest.encode("utf-8")}
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(members.items()):
            info = zipfile.ZipInfo(name, (2026,10,27,12,0,0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if name.endswith(".sh") else 0o100644) << 16
            z.writestr(info,data,compresslevel=9)
    with zipfile.ZipFile(path) as z:
        if z.testzip() or set(z.namelist()) != set(members): raise ValueError("ZIP verification failed")
        for name,data in members.items():
            if z.read(name) != data: raise ValueError("ZIP mismatch: " + name)
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir",type=Path,default=ROOT/"dist")
    args=parser.parse_args()
    config=configparser.ConfigParser()
    config.read(ROOT/"pluginst.inf",encoding="utf-8")
    manifest=config["plugininstall"]; version=manifest["version"]
    if manifest["type"] != "wfx" or manifest["file"] != "kitty-ng-fs.wfx" or manifest["defaultdir"] != "kitty-ng-fs":
        raise ValueError("Invalid TC installer manifest")
    runtime={name:(ROOT/name).read_bytes() for name in (*DOCS,"pluginst.inf")}
    for name,expected in (("kitty-ng-fs.wfx",0x14c),("kitty-ng-fs.wfx64",0x8664)):
        data=(ROOT/"dist"/name).read_bytes()
        if machine(data)!=expected: raise ValueError("Wrong architecture: "+name)
        runtime[name]=data
    source={name:(ROOT/name).read_bytes() for name in SOURCE_FILES}
    allowed={".c",".h",".def",".rc",".md",".py",".svg",".ico",".png"}
    for folder in ("src","tests","assets","tools"):
        for f in sorted((ROOT/folder).rglob("*")):
            if f.is_file() and f.suffix in allowed and "__pycache__" not in f.parts:
                source[f.relative_to(ROOT).as_posix()]=f.read_bytes()
    out=args.output_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
    checks=[]
    for suffix,members in (("x86-x64",runtime),("source",source)):
        path=out/f"kitty-ng-fs-{version}-{suffix}.zip"
        checks.append(f"{write_zip(path,members)}  {path.name}\n")
        print(f"Verified {path.name}: {len(members)} files plus manifest")
    (out/"SHA256SUMS.txt").write_text("".join(checks),encoding="ascii")

if __name__=="__main__": main()
