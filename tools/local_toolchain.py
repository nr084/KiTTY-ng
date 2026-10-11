"""Keep the latest stable LLVM-MinGW installed locally in the project."""
import json
from pathlib import Path
import re
import subprocess
import sys
import urllib.request


def ensure_local_toolchain(root, run=None):
    request = urllib.request.Request(
        "https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest",
        headers={"User-Agent": "kitty-ng-local-build"})
    with urllib.request.urlopen(request, timeout=60) as response:
        release = json.load(response)
    if release["prerelease"] or release["draft"] or not re.fullmatch(r"\d+", release["tag_name"]):
        raise RuntimeError("Expected a stable LLVM-MinGW release")
    asset = next(a for a in release["assets"] if re.fullmatch(
        r"llvm-mingw-\d+-ucrt-x86_64\.zip", a["name"]))
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", asset.get("digest", "")):
        raise RuntimeError("Official toolchain SHA-256 is missing")
    installed = root / "_tools" / "llvm-mingw" / release["tag_name"]
    toolchain = installed / "toolchain"
    if toolchain.exists():
        try:
            # A checkout reused by another Windows account can contain an
            # unreadable installation. Preserve it and install this release
            # in a separate permanent directory with current permissions.
            list((toolchain / "bin").iterdir())
        except PermissionError:
            installed = root / "_tools" / "llvm-mingw" / (release["tag_name"] + "-local")
            toolchain = installed / "toolchain"
    metadata = installed / "toolchain-info.json"
    if toolchain.exists():
        if not metadata.is_file():
            raise RuntimeError("Local compiler has no verification metadata: " + str(toolchain))
        info = json.loads(metadata.read_text())
        if (info["release"] != release["tag_name"] or
                info["sha256"] != asset["digest"].split(":", 1)[1]):
            raise RuntimeError("Local compiler does not match the official stable release")
    else:
        command = [sys.executable, str(root / "tools/fetch-llvm-mingw.py"),
                   "--output", str(toolchain)]
        if run:
            run(command, "fetch-llvm")
        else:
            subprocess.run(command, check=True)
        info = json.loads(metadata.read_text())
        if info["release"] != release["tag_name"] or info["sha256"] != asset["digest"].split(":", 1)[1]:
            raise RuntimeError("Stable release changed during installation; retry")
    for target in ("x86_64", "i686", "aarch64"):
        compiler = toolchain / "bin" / (target + "-w64-mingw32-clang.exe")
        version = subprocess.check_output([str(compiler), "--version"], text=True)
        if any(word in version.lower() for word in ("development", "snapshot", "git main")):
            raise RuntimeError("Non-stable compiler: " + version.splitlines()[0])
    print("Local stable LLVM-MinGW:", release["tag_name"], flush=True)
    return toolchain


if __name__ == "__main__":
    print(ensure_local_toolchain(Path(__file__).resolve().parents[1]))
