"""Build Windows x64/x86/ARM64 with LLVM-MinGW and record compiler versions."""
import argparse
import json
import subprocess
import shutil
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("arch", choices=["x64", "x86", "arm64"])
parser.add_argument("--toolchain", type=Path, default=Path("build/toolchain"))
parser.add_argument("--build-dir", type=Path)
parser.add_argument("--cmake", default="cmake")
parser.add_argument("--ninja", default="ninja")
parser.add_argument("--tests", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
directory = (args.build_dir or root / "build" / args.arch).resolve()
directory.mkdir(parents=True, exist_ok=True)
triplet = {"x64": "x86_64", "x86": "i686", "arm64": "aarch64"}[args.arch]
tools = args.toolchain.resolve() / "bin"
compiler = tools / (triplet + "-w64-mingw32-clang.exe")
ninja = Path(shutil.which(args.ninja) or args.ninja).resolve()
assert ninja.is_file(), "Ninja executable not found"
version = subprocess.check_output([str(compiler), "--version"], text=True)
info = {"architecture": args.arch, "compiler": version.strip(),
        "cmake": subprocess.check_output([args.cmake, "--version"], text=True).splitlines()[0]}
metadata = args.toolchain.resolve().parent / "toolchain-info.json"
if metadata.exists(): info["toolchain"] = json.loads(metadata.read_text())
info["source_commit"] = subprocess.check_output(["git", "-c", "safe.directory=" + str(root), "rev-parse", "HEAD"], cwd=root, text=True).strip()
(directory / "compiler.json").write_text(json.dumps(info, indent=2) + "\n")
targets = ["kitty-ng", "plink", "pscp", "psftp", "puttygen", "pageant"]
if args.tests: targets += ["test_i18n", "test_kittyng", "test_kittyng_legacy", "testcrypt", "test_host_strfoo", "test_bpp", "test_companion_probe", "test_language_controls", "test_staticwrap", "test_quick_commands", "test_session_browser", "test_utility_ui", "test_key_paths", "test_extensions"]
commands = [
    [args.cmake, "-S", str(root), "-B", str(directory), "-G", "Ninja", "-DCMAKE_SYSTEM_NAME=Windows",
     "-DCMAKE_C_COMPILER=" + compiler.as_posix(),
     "-DCMAKE_RC_COMPILER=" + (tools / (triplet + "-w64-mingw32-windres.exe")).as_posix(),
     "-DCMAKE_MAKE_PROGRAM=" + ninja.as_posix(), "-DCMAKE_BUILD_TYPE=Release"],
    [args.cmake, "--build", str(directory), "--target", *targets, "--parallel", "8"]]
for index, command in enumerate(commands):
    result = subprocess.run(command, cwd=root, text=True, encoding="utf8", errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (directory / ("configure.log" if index == 0 else "build.log")).write_text(result.stdout, encoding="utf8")
    if result.returncode:
        print(result.stdout[-10000:]); raise SystemExit(result.returncode)
print("PASS:", args.arch, version.splitlines()[0])
