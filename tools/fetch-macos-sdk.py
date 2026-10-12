"""Extract the latest stable macOS SDK from Apple's official update packages.

No macOS host or system installation is required. On Windows SDK symlinks are
materialised inside the SDK; private Apple SDK contents are never committed.
"""
import gzip
import hashlib
import io
import json
import lzma
import plistlib
import posixpath
import re
import shutil
import struct
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
CATALOG = "https://swscan.apple.com/content/catalogs/others/index-27-26-15-14-13-12-10.16-10.15-10.14-10.13-10.12-10.11-10.10-10.9-mountainlion-lion-snowleopard-leopard.merged-1.sucatalog"
def fetch(url):
    if not url.startswith("https://"): raise RuntimeError("Expected HTTPS Apple URL")
    return urllib.request.urlopen(url, timeout=60)
def sdk_package():
    with fetch(CATALOG) as f: catalog = plistlib.loads(f.read())
    products = sorted(catalog["Products"].items(), key=lambda pair: str(pair[1].get("PostDate")), reverse=True)
    for key, product in products:
        packages = [p for p in product.get("Packages", []) if p["URL"].endswith("/CLTools_macOSNMOS_SDK.pkg")]
        if not packages: continue
        with fetch(product["Distributions"]["English"]) as f: text = f.read().decode("utf8")
        title = re.search(r'"SU_TITLE"\s*=\s*"([^"]+)"', text).group(1)
        if re.search(r"beta|\bRC\b|preview", title, re.I): continue
        return key, title, product, packages[0]
    raise RuntimeError("No stable SDK package in Apple's catalog")
def xar_payload(package, target, expected_toc_digest):
    with package.open("rb") as f:
        magic, size, version, compressed, expanded, algorithm = struct.unpack(">4sHHQQI", f.read(28))
        if magic != b"xar!" or size < 28 or expanded > 16777216: raise RuntimeError("Invalid XAR header")
        f.seek(size); toc_data = f.read(compressed)
        # Apple catalog Digest authenticates the compressed XAR TOC, not the entire .pkg.
        if algorithm != 1 or hashlib.sha1(toc_data).digest() != expected_toc_digest: raise RuntimeError("Official Apple XAR TOC digest mismatch")
        toc = ET.fromstring(zlib.decompress(toc_data))
        entry = next(e for e in toc.findall(".//file") if e.findtext("name") == "Payload")
        data = entry.find("data"); offset = int(data.findtext("offset")); length = int(data.findtext("length"))
        checksum = data.find("archived-checksum")
        digest = hashlib.new(checksum.attrib["style"].replace("-", "")) if checksum is not None else None
        f.seek(size+compressed+offset)
        with target.open("wb") as out:
            while length:
                block = f.read(min(length,1048576))
                if not block: raise RuntimeError("Truncated XAR")
                length -= len(block); out.write(block)
                if digest: digest.update(block)
        if digest and digest.hexdigest() != checksum.text: raise RuntimeError("XAR payload checksum mismatch")
def unpack_payload(payload, cpio):
    with payload.open("rb") as source, cpio.open("wb") as target:
        magic = source.read(4); source.seek(0)
        if magic == b"pbzx":
            source.read(4); flags = struct.unpack(">Q", source.read(8))[0]
            while flags & (1 << 24):
                flags, size = struct.unpack(">QQ", source.read(16))
                block = source.read(size)
                if len(block) != size: raise RuntimeError("Truncated PBZX")
                target.write(lzma.decompress(block) if block.startswith(b"\xfd7zXZ\x00") else block)
        elif magic[:2] == b"\x1f\x8b":
            with gzip.GzipFile(fileobj=source) as stream: shutil.copyfileobj(stream,target)
        else: raise RuntimeError("Unknown SDK payload encoding")
def extract_sdk(cpio, work):
    links = {}; hardlinks = {}; pending_hardlinks = []
    sdk_name = None
    with cpio.open("rb") as stream:
        while True:
            magic = stream.read(6)
            if not magic: break
            if magic == b"070707":
                header = stream.read(70)
                if len(header) != 70: raise RuntimeError("Truncated CPIO")
                dev = int(header[0:6],8); inode = int(header[6:12],8); mode = int(header[12:18],8); nlinks = int(header[30:36],8)
                namesize = int(header[53:59],8); size = int(header[59:70],8); padding = 0
            elif magic in (b"070701", b"070702"):
                header = stream.read(104); fields = [int(header[i:i+8],16) for i in range(0,104,8)]
                inode, mode, nlinks, size, dev, namesize = fields[0],fields[1],fields[4],fields[6],(fields[7]<<32)|fields[8],fields[11]
                padding = (-(110+namesize))%4
            else: raise RuntimeError("Unsupported CPIO magic: " + repr(magic))
            if namesize < 1 or namesize > 32768 or size > 1073741824: raise RuntimeError("Invalid CPIO limits")
            name = stream.read(namesize).rstrip(b"\0").decode("utf8"); stream.read(padding)
            if name == "TRAILER!!!": break
            match = re.search(r"(?:^|/)(MacOSX[0-9.]+\.sdk)(?:/|$)(.*)", name)
            if match:
                if not match[2] and (mode & 0xF000) == 0xA000:
                    stream.seek(size,1)
                    if magic != b"070707": stream.read((-size)%4)
                    continue
                if sdk_name is None: sdk_name = match[1]
                if sdk_name != match[1]: raise RuntimeError(f"Multiple SDKs in selected package: {sdk_name}, {match[1]}, {match[2]}")
                relative = match[2]
                if relative == "usr/share" or relative.startswith("usr/share/"):
                    stream.seek(size,1)
                    if magic != b"070707": stream.read((-size)%4)
                    continue
                if any(p == ".." for p in relative.split("/")): raise RuntimeError("Unsafe SDK path")
                target = (work/sdk_name/relative).resolve()
                if not target.is_relative_to(work.resolve()): raise RuntimeError("Unsafe SDK extraction")
                kind = mode & 0xF000
                if kind == 0x4000: target.mkdir(parents=True,exist_ok=True); stream.read(size)
                elif kind == 0xA000: links[relative] = stream.read(size).decode("utf8")
                elif kind == 0x8000:
                    target.parent.mkdir(parents=True,exist_ok=True)
                    if nlinks > 1 and not size: pending_hardlinks.append((target,(dev,inode)))
                    else:
                        with target.open("wb") as f:
                            remaining = size
                            while remaining:
                                chunk = stream.read(min(remaining,1048576))
                                if not chunk: raise RuntimeError("Truncated CPIO file")
                                f.write(chunk); remaining -= len(chunk)
                        if nlinks > 1: hardlinks[dev,inode] = target
                else: stream.seek(size,1)
            else: stream.seek(size,1)
            if magic != b"070707": stream.read((-size)%4)
    if not sdk_name: raise RuntimeError("macOS SDK not found")
    root = work/sdk_name
    for dest,key in pending_hardlinks:
        if key not in hardlinks: raise RuntimeError("Missing SDK hardlink")
        shutil.copyfile(hardlinks[key],dest)
    def resolve(name):
        for _ in range(64):
            prefix = next((p for p in sorted(links,key=len,reverse=True) if name == p or name.startswith(p+"/")),None)
            if prefix is None: return name
            ref = links[prefix]
            if ref.startswith("/"): raise RuntimeError("Absolute SDK symlink")
            name = posixpath.normpath(posixpath.join(posixpath.dirname(prefix),ref)+name[len(prefix):])
            if name == ".." or name.startswith("../"): raise RuntimeError("Escaping SDK symlink")
        raise RuntimeError("Cyclic SDK symlink")
    # Materialise inner directory links before copying aliases of those trees.
    for name in sorted(links,key=lambda s:(s.count("/"),len(s)),reverse=True):
        source = root/resolve(name); dest = root/name
        if not source.exists(): raise RuntimeError("Dangling SDK symlink: " + name)
        dest.parent.mkdir(parents=True,exist_ok=True)
        if source.is_dir(): shutil.copytree(source,dest,dirs_exist_ok=True)
        else: shutil.copyfile(source,dest)
    return root

def main():
    key,title,product,package = sdk_package()
    print("Official stable SDK package:", title, flush=True)
    store = ROOT/"_tools/macos/sdk"; store.mkdir(parents=True,exist_ok=True)
    metadata = store/"sdk-info.json"
    if metadata.is_file():
        prior = json.loads(metadata.read_text())
        if prior["product"] == key and (store/prior["directory"]/"SDKSettings.json").is_file():
            print("REUSE:",store/prior["directory"],flush=True); return
    ROOT.joinpath("build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="macos-sdk-",dir=ROOT/"build") as temporary:
        work = Path(temporary); archive = work/"sdk.pkg"; sha1 = hashlib.sha1(); sha256 = hashlib.sha256()
        with fetch(package["URL"]) as source, archive.open("wb") as output:
            while chunk := source.read(1048576): output.write(chunk); sha1.update(chunk); sha256.update(chunk)
        expected = package["Digest"] if isinstance(package["Digest"], bytes) else bytes.fromhex(package["Digest"])
        if archive.stat().st_size != package["Size"]: raise RuntimeError("Official Apple package size mismatch")
        print("Verified Apple package; extracting SDK",flush=True)
        payload = work/"Payload"; cpio = work/"Payload.cpio"
        xar_payload(archive,payload,expected); unpack_payload(payload,cpio); sdk = extract_sdk(cpio,work/"extract")
        settings = json.loads((sdk/"SDKSettings.json").read_text())
        target = store/sdk.name
        if target.exists(): raise RuntimeError("Existing SDK without matching metadata; preserving it")
        shutil.move(str(sdk),target)
        metadata.write_text(json.dumps({"catalog":CATALOG,"product":key,"title":title,"sdk_version":settings["Version"],"directory":target.name,"url":package["URL"],"sha256":sha256.hexdigest(),"apple_toc_sha1":expected.hex()},indent=2)+"\n")
        print("Installed:",target,"SDK",settings["Version"],flush=True)
if __name__ == "__main__": main()
