#!/usr/bin/env python3
"""Build x86/x64 WFX and run native Windows tests with an explicit LLVM-MinGW."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil

ROOT = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--tests", nargs="+", choices=("sftp", "pathutil", "profiles", "session_tree", "icons"),
                        default=["sftp", "pathutil", "profiles", "session_tree", "icons"])
    parser.add_argument("--cmake", type=Path)
    parser.add_argument("--ninja", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/development")
    args = parser.parse_args()
    engine = ROOT / "engine" if (ROOT / "engine/CMakeLists.txt").exists() else ROOT.parent
    cmake = args.cmake or shutil.which("cmake") or engine / "_tools/macos/cmake/cmake/data/bin/cmake.exe"
    ninja = args.ninja or shutil.which("ninja") or engine / "_tools/macos/ninja/ninja-1.13.2.data/scripts/ninja.exe"
    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    tools = args.toolchain.resolve() / "bin"
    records = []
    def run(command):
        result = subprocess.run([str(x) for x in command], cwd=ROOT,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = result.stdout.decode("utf-8", errors="replace")
        records.append({"command": [str(x) for x in command], "exit_code": result.returncode, "output": output})
        print(output, end="", flush=True)
        if result.returncode: raise RuntimeError("Command failed: " + str(command[0]))
        return output
    (ROOT / "dist").mkdir(exist_ok=True)
    try:
        version = run([tools / "clang.exe", "--version"])
        for arch, target, suffix in (("x86", "i686", "wfx"), ("x64", "x86_64", "wfx64")):
            build = ROOT / "build" / arch
            build.mkdir(parents=True, exist_ok=True)
            cc = tools / (target + "-w64-mingw32-clang.exe")
            rc = tools / (target + "-w64-mingw32-windres.exe")
            flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                     "-DUNICODE", "-D_UNICODE", "-D_WIN32_WINNT=0x0A00", "-DWINVER=0x0A00"]
            dll = output_dir / ("kitty-ng-fs." + suffix)
            embedded = ROOT / "build" / ("embedded-" + arch)
            run([cmake, "-S", engine, "-B", embedded, "-G", "Ninja",
                 "-DCMAKE_PROJECT_kitty-ng_INCLUDE=" + (ROOT / "cmake/embed.cmake").as_posix(),
                 "-DKITTYNGFS_DIR=" + ROOT.as_posix(), "-DCMAKE_MAKE_PROGRAM=" + Path(ninja).as_posix(),
                 "-DCMAKE_C_COMPILER=" + cc.as_posix(), "-DCMAKE_RC_COMPILER=" + rc.as_posix(),
                 "-DCMAKE_BUILD_TYPE=Release"])
            run([cmake, "--build", embedded, "--target", "kittyngfs", "kg_test_agent", "--parallel", "8"])
            shutil.copy2(embedded / dll.name, dll)
            run([embedded / "kg_test_agent.exe"])
            suites = [
                ("sftp", ["src/sftp.c"], [], []),
                ("pathutil", ["src/pathutil.c"], ["-lbcrypt", "-lshell32"], []),
                ("profiles", ["src/profiles.c"], ["-lshlwapi", "-lshell32"], []),
                ("session_tree", ["src/session_tree.c"], [], []),
                ("icons", [], ["-municode", "-lshell32", "-lgdi32", "-luser32"], [dll]),
            ]
            for name, sources, links, params in suites:
                if name not in args.tests: continue
                exe = build / ("test_" + name + ".exe")
                run([cc, *flags, *sources, "tests/test_" + name + ".c", *links, "-o", exe])
                run([exe, *params])
            # Integration executable is used separately with an isolated SSH fixture.
            run([cc, *flags, "tests/test_wfx.c", "-municode", "-o", build / "test_wfx.exe"])
            run([cc, *flags, "tests/test_auth.c", "-municode", "-o", build / "test_auth.exe"])
            if arch == "x64":
                exe = build / "test_settings.exe"
                run([cc, *flags, "tests/test_settings_resource.c", "-municode", "-lversion", "-o", exe])
                run([exe, output_dir / "kitty-ng-fs.wfx", output_dir / "kitty-ng-fs.wfx64"])
                before = ROOT / "build/session_tree-before.c"
                if before.exists():
                    exe = build / "test_tree_before.exe"
                    run([cc, *flags, "-I", "src", before, "tests/test_session_tree.c", "-o", exe])
                    run([exe])
        result = {"status": "success", "compiler": version,
                  "binaries": {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in output_dir.glob("*.wfx*")}}
    except Exception:
        result = {"status": "failed"}
        raise
    finally:
        result["commands"] = records
        (output_dir / "build-validation.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

if __name__ == "__main__": main()
