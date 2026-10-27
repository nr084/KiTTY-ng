#!/usr/bin/env python3
"""Build x86/x64 WFX and run native Windows tests with an explicit LLVM-MinGW."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    args = parser.parse_args()
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
            run([rc, "--codepage=65001", "-I", "src", "src/plugin.rc", "-O", "coff", "-o", build / "plugin.o"])
            dll = ROOT / "dist" / ("kitty-ng-fs." + suffix)
            run([cc, *flags, "-shared", "-static-libgcc", "-Wl,--no-insert-timestamp", "-Wl,--dynamicbase", "-Wl,--nxcompat",
                 *(["-Wl,--kill-at"] if arch == "x86" else []),
                 *["src/" + x + ".c" for x in ("plugin", "pathutil", "profiles", "session_tree", "sftp", "transport")],
                 build / "plugin.o", "src/kittyngfs.def", "-luser32", "-lshell32", "-lole32", "-lshlwapi", "-ladvapi32", "-lbcrypt", "-o", dll])
            suites = [
                ("sftp", ["src/sftp.c"], [], []),
                ("pathutil", ["src/pathutil.c"], ["-lbcrypt", "-lshell32"], []),
                ("profiles", ["src/profiles.c"], ["-lshlwapi", "-lshell32"], []),
                ("session_tree", ["src/session_tree.c"], [], []),
                ("icons", [], ["-municode", "-lshell32", "-lgdi32", "-luser32"], [dll]),
            ]
            for name, sources, links, params in suites:
                exe = build / ("test_" + name + ".exe")
                run([cc, *flags, *sources, "tests/test_" + name + ".c", *links, "-o", exe])
                run([exe, *params])
            # Integration executable is used separately with an isolated SSH fixture.
            run([cc, *flags, "tests/test_wfx.c", "-municode", "-o", build / "test_wfx.exe"])
            if arch == "x64":
                exe = build / "test_settings.exe"
                run([cc, *flags, "tests/test_settings_resource.c", "-municode", "-lversion", "-o", exe])
                run([exe, ROOT / "dist/kitty-ng-fs.wfx", ROOT / "dist/kitty-ng-fs.wfx64"])
                before = ROOT / "build/session_tree-before.c"
                if before.exists():
                    exe = build / "test_tree_before.exe"
                    run([cc, *flags, "-I", "src", before, "tests/test_session_tree.c", "-o", exe])
                    run([exe])
        result = {"status": "success", "compiler": version,
                  "binaries": {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in (ROOT / "dist").glob("*.wfx*")}}
    except Exception:
        result = {"status": "failed"}
        raise
    finally:
        result["commands"] = records
        (ROOT / "dist/build-validation.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

if __name__ == "__main__": main()
