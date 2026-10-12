"""Install the current official stable host LLVM for native Windows macOS builds."""
import hashlib
import json
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "KiTTY-NG macOS build"}), timeout=60)
def download(url, path, digest):
    sha = hashlib.sha256()
    with fetch(url) as source, path.open("wb") as target:
        while chunk := source.read(1048576):
            target.write(chunk); sha.update(chunk)
    if sha.hexdigest() != digest: raise RuntimeError("Official SHA-256 mismatch: " + path.name)

def main():
    with fetch("https://api.github.com/repos/llvm/llvm-project/releases/latest") as source: release = json.load(source)
    assert not release["draft"] and not release["prerelease"]
    version = release["tag_name"].removeprefix("llvmorg-")
    asset = next(a for a in release["assets"] if a["name"] == f"clang+llvm-{version}-x86_64-pc-windows-msvc.tar.zst")
    digest = asset["digest"].removeprefix("sha256:")
    installed = ROOT / "_tools/macos/llvm" / version
    if (installed / "toolchain-info.json").is_file() and (installed / "bin/llvm-install-name-tool.exe").is_file():
        info = json.loads((installed / "toolchain-info.json").read_text())
        assert info["sha256"] == digest
        print("REUSE: official stable LLVM", version, flush=True); return
    ROOT.joinpath("build").mkdir(exist_ok=True)
    deps = ROOT / "_tools/macos/python-deps"
    with tempfile.TemporaryDirectory(prefix="macos-llvm-", dir=ROOT/"build") as temporary:
        work = Path(temporary)
        # Use a verified PyPI wheel without modifying the host Python installation.
        sys.path.insert(0, str(deps))
        try: import zstandard
        except ImportError:
            from pip._vendor.packaging.tags import sys_tags
            from pip._vendor.packaging.utils import parse_wheel_filename
            with fetch("https://pypi.org/pypi/zstandard/json") as source: metadata = json.load(source)
            tags = set(sys_tags())
            wheel = next(a for a in metadata["urls"] if a["filename"].endswith(".whl") and not a["yanked"] and parse_wheel_filename(a["filename"])[3] & tags)
            path = work / wheel["filename"]; download(wheel["url"], path, wheel["digests"]["sha256"])
            deps.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(path) as archive:
                for name in archive.namelist():
                    target = (deps/name).resolve()
                    if not target.is_relative_to(deps.resolve()): raise RuntimeError("Unsafe wheel path")
                archive.extractall(deps)
            (deps / "dependency-info.json").write_text(json.dumps({"zstandard":metadata["info"]["version"],"wheel":wheel["filename"],"sha256":wheel["digests"]["sha256"]},indent=2))
            import importlib
            importlib.invalidate_caches()
            import zstandard
        print("Downloading official LLVM", version, flush=True)
        path = work / asset["name"]; download(asset["browser_download_url"], path, digest)
        installed.mkdir(parents=True, exist_ok=True)
        wanted = {"clang.exe", "ld64.lld.exe", "lld.exe", "llvm-lipo.exe", "llvm-ar.exe", "llvm-ranlib.exe", "llvm-nm.exe", "llvm-readobj.exe", "llvm-objdump.exe", "llvm-objcopy.exe", "llvm-install-name-tool.exe", "llvm-otool.exe"}
        aliases = []
        with path.open("rb") as compressed, zstandard.ZstdDecompressor(max_window_size=2147483648).stream_reader(compressed) as reader, tarfile.open(fileobj=reader, mode="r|") as archive:
            for member in archive:
                relative = Path(*Path(member.name).parts[1:])
                if not relative.parts: continue
                keep = (relative.parts[0] == "bin" and (relative.name in wanted or relative.suffix.lower() == ".dll")) or relative.parts[:2] == ("lib", "clang") or relative.name.lower().startswith("license")
                if not keep: continue
                if member.islnk() or member.issym():
                    aliases.append((relative, Path(*Path(member.linkname).parts[1:]) if member.islnk() else relative.parent/member.linkname)); continue
                if not member.isfile(): continue
                target = (installed/relative).resolve()
                if not target.is_relative_to(installed.resolve()): raise RuntimeError("Unsafe LLVM path")
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, target.open("wb") as output: shutil.copyfileobj(source, output)
        for dest,source in aliases:
            source = (installed/source).resolve(); target = (installed/dest).resolve()
            if not source.is_relative_to(installed.resolve()) or not target.is_relative_to(installed.resolve()): raise RuntimeError("Unsafe LLVM alias")
            if source.is_file(): shutil.copyfile(source,target)
        assert (installed/"bin/clang.exe").is_file() and (installed/"bin/ld64.lld.exe").is_file()
        (installed/"toolchain-info.json").write_text(json.dumps({"version":version,"url":asset["browser_download_url"],"sha256":digest,"release":release["html_url"]},indent=2)+"\n")
        print("Installed:", installed, flush=True)
if __name__ == "__main__": main()
