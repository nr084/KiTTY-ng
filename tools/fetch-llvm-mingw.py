"""Download the latest stable official LLVM-MinGW, verifying its SHA-256."""
import argparse
import hashlib
import json
import os
import re
import tempfile
import urllib.request
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=Path("build/toolchain"))
args = parser.parse_args()
destination = args.output.resolve()
if destination.exists():
    raise SystemExit("Toolchain destination already exists; use a fresh directory")
headers = {"User-Agent": "kitty-ng-build"}
if os.environ.get("GH_TOKEN"):
    headers["Authorization"] = "Bearer " + os.environ["GH_TOKEN"]
request = urllib.request.Request("https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest", headers=headers)
with urllib.request.urlopen(request, timeout=60) as response:
    release = json.load(response)
assert not release["prerelease"] and not release["draft"]
asset = next(a for a in release["assets"] if re.fullmatch(r"llvm-mingw-\d+-ucrt-x86_64\.zip", a["name"]))
digest = asset.get("digest", "")
assert re.fullmatch(r"sha256:[0-9a-f]{64}", digest), "Official release has no SHA-256 digest"
destination.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="llvm-download-", dir=destination.parent) as directory:
    temporary = Path(directory)
    archive = temporary / "toolchain.zip"
    # Do not forward an API authorization token to download redirects.
    with urllib.request.urlopen(asset["browser_download_url"], timeout=120) as response, archive.open("wb") as output:
        while chunk := response.read(1024 * 1024): output.write(chunk)
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == digest.split(":")[1]
    extracted = temporary / "extracted"
    with zipfile.ZipFile(archive) as bundle:
        for name in bundle.namelist():
            target = (extracted / name).resolve()
            assert target.is_relative_to(extracted.resolve()), "Unsafe archive member"
        bundle.extractall(extracted)
    sources = list(extracted.iterdir())
    assert len(sources) == 1 and (sources[0] / "bin/clang.exe").is_file()
    sources[0].rename(destination)
metadata = {"release": release["tag_name"], "release_url": release["html_url"],
            "asset": asset["name"], "sha256": digest.split(":")[1]}
(destination.parent / "toolchain-info.json").write_text(json.dumps(metadata, indent=2) + "\n")
print("Verified stable LLVM-MinGW:", metadata["release"])
