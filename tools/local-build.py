"""Local Windows release pipeline. Run with Python 3.10+ and Git on PATH."""
import argparse
import hashlib
import io
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from build_names import main_executable, utility_executable
from local_toolchain import ensure_local_toolchain

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable
BASELINE_FILES = {}


def digest_files(files, baseline=None):
    digest = hashlib.sha256()
    if baseline:
        missing = sorted(name for name in set(files) if (baseline, name) not in BASELINE_FILES)
        for offset in range(0, len(missing), 100):
            names = missing[offset:offset + 100]
            result = subprocess.run(
                ["git", "-c", "safe.directory=" + str(ROOT), "archive", baseline, "--", *names],
                cwd=ROOT, capture_output=True)
            if result.returncode:
                return None
            with tarfile.open(fileobj=io.BytesIO(result.stdout)) as archive:
                for name in names:
                    try:
                        stream = archive.extractfile(name)
                        if stream is None:
                            return None
                        BASELINE_FILES[baseline, name] = stream.read()
                    except KeyError:
                        return None
    for name in sorted(set(files)):
        path = ROOT / name
        if baseline:
            data = BASELINE_FILES[baseline, name].replace(b"\r\n", b"\n")
        else:
            if not path.is_file():
                return None
            data = path.read_bytes().replace(b"\r\n", b"\n")
        digest.update(name.encode() + b"\0" + data + b"\0")
    return digest.hexdigest()


def fetch_wheel(package, destination):
    # CMake and Ninja wheels contain native tools, no installation is needed.
    from pip._vendor.packaging.tags import sys_tags
    from pip._vendor.packaging.utils import parse_wheel_filename
    from pip._vendor.packaging.version import Version
    with urllib.request.urlopen(
            f"https://pypi.org/pypi/{package}/json", timeout=60) as response:
        metadata = json.load(response)
    assert not Version(metadata["info"]["version"]).is_prerelease
    tags = set(sys_tags())
    asset = next(a for a in metadata["urls"] if
                 a["filename"].endswith(".whl") and not a["yanked"] and
                 parse_wheel_filename(a["filename"])[3] & tags)
    with urllib.request.urlopen(asset["url"], timeout=120) as response:
        data = response.read()
    assert hashlib.sha256(data).hexdigest() == asset["digests"]["sha256"]
    with zipfile.ZipFile(io.BytesIO(data)) as bundle:
        for name in bundle.namelist():
            if not (destination / name).resolve().is_relative_to(destination):
                raise ValueError("Unsafe wheel member")
        bundle.extractall(destination)
    return {"version": metadata["info"]["version"], "sha256": asset["digests"]["sha256"]}


def source_dependencies(ninja, directory, targets):
    """Read the actual build graph and compiler-discovered header dependencies."""
    inputs = subprocess.check_output(
        [str(ninja), "-C", str(directory), "-t", "inputs", *targets],
        text=True, encoding="utf8").splitlines()
    dependencies = subprocess.check_output(
        [str(ninja), "-C", str(directory), "-t", "deps"],
        text=True, encoding="utf8")
    graph = subprocess.check_output(
        [str(ninja), "-C", str(directory), "-t", "graph", *targets],
        text=True, encoding="utf8")
    objects = set(re.findall(r'\[label="([^"]+)"', graph))
    active = False
    for line in dependencies.splitlines():
        if line and not line.startswith(" "):
            active = line.rsplit(": #deps", 1)[0] in objects
        elif active and line.startswith("    "):
            inputs.append(line.strip())
    files = set()
    for item in set(inputs):
        path = Path(item)
        if not path.is_absolute():
            path = directory / path
        path = path.resolve()
        if (path.is_relative_to(ROOT) and not path.is_relative_to(directory.parent)
                and not path.is_relative_to(ROOT / "_tools")):
            if path.is_file():
                files.add(path.relative_to(ROOT).as_posix())
    if not files:
        raise RuntimeError("No source dependencies found; cannot reuse test results")
    # Changes to build definitions can alter feature macros and compiler flags.
    # Read repository build definitions, excluding installed tools and all
    # generated/untracked trees. Otherwise a temporary CMake wheel changes
    # every suite fingerprint and causes unrelated tests to run again.
    tracked = subprocess.check_output(
        ["git", "-c", "safe.directory=" + str(ROOT), "ls-files", "--", "*CMakeLists.txt"],
        cwd=ROOT, text=True, encoding="utf8").splitlines()
    files.update(tracked)
    files.update(p.relative_to(ROOT).as_posix() for p in (ROOT / "cmake").glob("*.cmake"))
    files.add("tools/build-llvm.py")
    # These suites do not test release strings. PE/version validation runs
    # separately on every package, so a new test-stage suffix needs no retest.
    files -= {"version.h", "utils/version.c", "windows/version.rc2"}
    return files


def main():
    platform_parser = argparse.ArgumentParser(add_help=False)
    platform_parser.add_argument("--platform", choices=["windows", "macos", "linux"], default="windows")
    platform_args, remaining = platform_parser.parse_known_args()
    if platform_args.platform == "linux":
        subprocess.run([sys.executable, str(ROOT / "tools/linux-cross-build.py"), *remaining], cwd=ROOT, check=True)
        return
    if platform_args.platform == "macos":
        if sys.platform != "darwin":
            platform_parser.error("macOS builds now run on a Mac; use python3 tools/local-build.py --platform macos on macOS")
        subprocess.run([sys.executable, str(ROOT / "tools/macos-native-build.py"), *remaining], cwd=ROOT, check=True)
        return
    # Keep diagnostics readable in Russian paths and redirected Windows logs.
    os.environ["PYTHONUTF8"] = "1"
    sys.stdout.reconfigure(encoding="utf8", errors="replace")
    sys.stderr.reconfigure(encoding="utf8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=["windows", "macos", "linux"], default="windows")
    parser.add_argument("--arch", nargs="+", choices=["x64", "x86", "arm64"],
                        default=["x64", "x86", "arm64"])
    parser.add_argument("--toolchain", type=Path, help="Existing LLVM-MinGW directory")
    parser.add_argument("--cmake", type=Path)
    parser.add_argument("--ninja", type=Path)
    parser.add_argument("--offline", action="store_true", help="Requires all three tool paths")
    parser.add_argument("--force-tests", action="store_true", help="Explicitly rerun cached checks")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Run this pipeline on Windows (native x64/x86 tests)")
    if not shutil.which("git"):
        parser.error("Git must be available on PATH")
    if args.offline and not all([args.toolchain, args.cmake, args.ninja]):
        parser.error("--offline requires --toolchain, --cmake and --ninja")
    os.chdir(ROOT)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache_path = output / "local-tests.json"
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    report = {"started": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "status": "running",
              "version": (ROOT / "VERSION").read_text().strip(), "tests": [], "archives": []}
    report["source_commit"] = subprocess.check_output(
        ["git", "-c", "safe.directory=" + str(ROOT), "rev-parse", "HEAD"],
        text=True).strip()
    report["working_tree_dirty"] = bool(subprocess.check_output(
        ["git", "-c", "safe.directory=" + str(ROOT), "status", "--porcelain"]))
    logs = output / "local-build-logs"
    logs.mkdir(exist_ok=True)
    prior_path = output / "validation.json"
    prior = json.loads(prior_path.read_text()) if prior_path.exists() else {}

    def run(command, label, env=None):
        print(label, flush=True)
        result = subprocess.run([str(arg) for arg in command], cwd=ROOT,
                                env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        (logs / (label + ".log")).write_bytes(result.stdout)
        if result.returncode:
            print(result.stdout.decode("utf8", errors="replace")[-10000:])
            raise RuntimeError(f"{label}: exit {result.returncode}")

    def check(key, files, commands, compiler=None, baseline_allowed=False):
        fingerprint = digest_files(files)
        if fingerprint is None:
            raise RuntimeError("Missing test input: " + key)
        entry = cache.get(key, {})
        reused = (not args.force_tests and entry.get("fingerprint") == fingerprint and
                  entry.get("compiler") == compiler)
        if not reused and not args.force_tests and baseline_allowed:
            arch = key.split("/", 1)[0]
            prior_compilers = [f.get("compiler") for f in prior.get("files", [])
                              if f.get("file", "").endswith("-" + arch + ".zip")]
            commit = prior.get("source_commit")
            if (commit and prior.get("ci", {}).get("status") == "success" and
                    arch in prior.get("native_tests", []) and compiler in prior_compilers and
                    digest_files(files, commit) == fingerprint):
                reused = True
                entry = {"evidence": prior["ci"]["url"], "source_commit": commit}
        if reused:
            print("REUSE:", key, flush=True)
        else:
            for index, (command, env) in enumerate(commands):
                run(command, key.replace("/", "-") + f"-{index}", env)
            entry = {"source_commit": report["source_commit"], "evidence": "local"}
        entry.update(fingerprint=fingerprint, compiler=compiler, inputs=sorted(files))
        cache[key] = entry
        cache_path.write_text(json.dumps(cache, indent=2) + "\n")
        report["tests"].append({"suite": key, "result": "reused" if reused else "passed",
                                "evidence": entry["evidence"]})

    def ssh_environment(work):
        environment = os.environ.copy()
        if args.offline:
            __import__("paramiko")
            return environment
        dependencies = work / "ssh-dependencies"
        if not dependencies.exists():
            dependencies.mkdir()
            # Verified wheels avoid a second Python installation or pip's
            # potentially interactive subprocesses. All files are temporary.
            report["ssh_dependencies"] = {
                name: fetch_wheel(name, dependencies) for name in
                ("paramiko", "bcrypt", "pynacl", "invoke", "cryptography", "cffi", "pycparser")}
        environment["PYTHONPATH"] = str(dependencies)
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        return environment

    try:
        # This directory is owned by this invocation, including on failure.
        # Compile outside the synchronized source folder: sync clients can lock Ninja depfiles.
        with tempfile.TemporaryDirectory(prefix="kitty-ng-local-build-") as temporary:
            work = Path(temporary).resolve()
            toolchain = args.toolchain.resolve() if args.toolchain else ensure_local_toolchain(ROOT, run)
            wheel_info = {}
            wheels = work / "python"
            if not args.cmake or not args.ninja:
                wheels.mkdir()
            if not args.cmake:
                wheel_info["cmake"] = fetch_wheel("cmake", wheels)
            if not args.ninja:
                wheel_info["ninja"] = fetch_wheel("ninja", wheels)
            cmake = args.cmake.resolve() if args.cmake else wheels / "cmake/data/bin/cmake.exe"
            ninja = args.ninja.resolve() if args.ninja else next(wheels.glob("ninja-*.data/scripts/ninja.exe"))
            report["bootstrap"] = wheel_info
            check("translations", {"config.c", "windows/config.c", "windows/kittyng-language.c", "windows/kittyng-proxy-ui.c", "ssh/ca-config.c",
                  "locale/ru.json", "tools/check-translations.py"},
                  [([PYTHON, "tools/check-translations.py"], None)])
            check("utility-translations", {"windows/puttygen.c", "windows/pageant.c",
                  "windows/puttygen.rc", "windows/pageant.rc", "windows/plink.c", "pscp.c", "psftp.c",
                  "locale/ru.json", "tools/check-utility-translations.py"},
                  [([PYTHON, "tools/check-utility-translations.py"], None)])
            staged = work / "release"
            for arch in dict.fromkeys(args.arch):
                directory = work / arch
                executable = main_executable(arch)
                cli_executable = utility_executable("plink", arch)
                command = [PYTHON, "tools/build-llvm.py", arch, "--toolchain", toolchain,
                           "--build-dir", directory, "--cmake", cmake, "--ninja", ninja]
                if arch != "arm64":
                    command.append("--tests")
                try:
                    run(command, "build-" + arch)
                finally:
                    if directory.exists():
                        for name in ("configure.log", "build.log", "compiler.json"):
                            if (directory / name).exists():
                                shutil.copy2(directory / name, logs / (arch + "-" + name))
                compiler = json.loads((directory / "compiler.json").read_text())["compiler"].splitlines()[0]
                if arch != "arm64":
                    suites = [
                        ("extensions", "test_extensions", ["test/extensions.py"],
                         [PYTHON, "test/extensions.py", directory / "test_extensions.exe"]),
                        ("extensions-assets", "test_key_paths", ["test/extensions_assets.py"],
                         [PYTHON, "test/extensions_assets.py", directory / "test_key_paths.exe"]),
                        ("profile-update", "test_key_paths", ["test/profile_update.py"],
                         [PYTHON, "test/profile_update.py", directory / "test_key_paths.exe"]),
                        ("portable-key-paths", "test_key_paths", ["test/portable_key_paths.py"],
                         [PYTHON, "test/portable_key_paths.py", directory / "test_key_paths.exe"]),
                        ("session-browser", "test_session_browser", ["test/session_browser.py"],
                         [PYTHON, "test/session_browser.py", directory / "test_session_browser.exe"]),
                        ("i18n", "test_i18n", ["test/test_i18n.c", "locale/ru.json", "locale/ru-contexts.json"],
                         [directory / "test_i18n.exe"]),
                        ("portable", "test_kittyng", ["test/run_kittyng.py"],
                         [PYTHON, "test/run_kittyng.py", directory / "test_kittyng.exe"]),
                        ("legacy", "test_kittyng_legacy", ["test/run_kittyng_legacy.py", "test/kittyng-legacy-vectors.json"],
                         [PYTHON, "test/run_kittyng_legacy.py", directory / "test_kittyng_legacy.exe"]),
                        ("host", "test_host_strfoo", [], [directory / "test_host_strfoo.exe"]),
                        ("bpp", "test_bpp", [], [directory / "test_bpp.exe"]),
                        ("crypto", "testcrypt", ["test/cryptsuite.py", "test/testcrypt.py",
                         "test/eccref.py", "test/numbertheory.py", "test/ssh.py", "test/ca.py"],
                         [PYTHON, "test/cryptsuite.py"]),
                    ]
                    for suite, target, scripts, test_command in suites:
                        files = source_dependencies(ninja, directory, [target + ".exe"]) | set(scripts)
                        # This separate object is not referenced by the core
                        # test executables; dispatch is exercised below.
                        files -= {"windows/kittyng-companions.c", "windows/kittyng-companions.h"}
                        environment = os.environ.copy()
                        if suite == "crypto":
                            environment["PUTTY_TESTCRYPT"] = '"' + str(directory / "testcrypt.exe") + '"'
                        check(arch + "/" + suite, files, [(test_command, environment)],
                              compiler=compiler, baseline_allowed=True)
                    files = source_dependencies(ninja, directory, ["test_kittyng.exe"])
                    files.update({"test/registry_storage.py"})
                    check(arch + "/registry", files,
                          [([PYTHON, "test/registry_storage.py", directory / "test_kittyng.exe"], None)],
                          compiler=compiler)
                    files = source_dependencies(ninja, directory, ["test_language_controls.exe"])
                    files.update({"test/language_apply.py"})
                    check(arch + "/language-apply", files,
                          [([PYTHON, "test/language_apply.py", directory / "test_language_controls.exe"], None)],
                          compiler=compiler)
                    files = source_dependencies(ninja, directory, ["test_utility_ui.exe",
                        utility_executable("puttygen", arch), cli_executable,
                        utility_executable("pscp", arch), utility_executable("psftp", arch)])
                    files.update({"test/utility_languages.py"})
                    check(arch + "/utility-languages", files,
                          [([PYTHON, "test/utility_languages.py", directory, arch], None)], compiler=compiler)
                    files = source_dependencies(ninja, directory, [utility_executable("pageant", arch), "test_utility_ui.exe"])
                    files.add("test/kageant_language.py")
                    check(arch + "/kageant-language", files,
                          [([PYTHON, "test/kageant_language.py", directory, arch], None)], compiler=compiler)
                    # Session-dialog painting has no bearing on SSH authentication.
                    files = source_dependencies(ninja, directory, [executable, cli_executable])
                    files -= {"windows/kittyng-companions.c", "windows/kittyng-companions.h"}
                    files -= {"windows/config.c", "windows/controls.c", "windows/dialog.c",
                              "windows/kittyng-translations.h", "windows/kittyng-language.c", "config.c", "session-browser.h"}
                    files = {f for f in files if not f.endswith((".rc", ".rc2", ".ico"))}
                    files.update({"test/ssh_saved_password.py", "test/kittyng-password-vector.json"})
                    key = arch + "/ssh"
                    # Install Paramiko only when the SSH suite actually needs to run.
                    cached = cache.get(key, {})
                    fingerprint = digest_files(files)
                    baseline_ok = (prior.get("ci", {}).get("status") == "success" and
                                   arch in prior.get("native_tests", []) and prior.get("source_commit") and
                                   digest_files(files, prior["source_commit"]) == fingerprint and
                                   any(f.get("compiler") == compiler for f in prior.get("files", [])
                                       if f.get("file", "").endswith("-" + arch + ".zip")))
                    if args.force_tests or not (baseline_ok or
                        (cached.get("fingerprint") == fingerprint and cached.get("compiler") == compiler)):
                        ssh_env = ssh_environment(work)
                    else:
                        ssh_env = None
                    check(key, files, [([PYTHON, "test/ssh_saved_password.py", directory / executable, directory / cli_executable], ssh_env),
                          ([PYTHON, "test/ssh_saved_password.py", "--reconnect", directory / executable], ssh_env)],
                          compiler=compiler, baseline_allowed=True)
                    files = source_dependencies(ninja, directory, [executable, "test_companion_probe.exe"])
                    files.update({"test/companion_dispatch.py", "tools/build_names.py"})
                    check(arch + "/companions", files,
                          [([PYTHON, "test/companion_dispatch.py", directory, arch], None)],
                          compiler=compiler)
                    files = source_dependencies(ninja, directory, [executable])
                    files.update({"test/windows_tabs.py", "test/duplicate_auth_fixture.py"})
                    check(arch + "/session-tabs", files,
                          [([PYTHON, "test/windows_tabs.py", directory / executable], ssh_environment(work)),
                           ([PYTHON, "test/windows_tabs.py", directory / executable, "--russian", "--maximized"], ssh_environment(work)),
                           ([PYTHON, "test/windows_tabs.py", directory / executable, "--key"], ssh_environment(work)),
                           ([PYTHON, "test/windows_tabs.py", directory / executable, "--fullscreen"], ssh_environment(work))],
                          compiler=compiler)
                    files = source_dependencies(ninja, directory, [executable])
                    files.update({"test/duplicate_password_retry.py", "test/kittyng-password-vector.json"})
                    check(arch + "/duplicate-password-retry", files,
                          [([PYTHON, "test/duplicate_password_retry.py", directory / executable, "--report", logs / (arch + "-duplicate-password-retry.json")], ssh_environment(work))],
                          compiler=compiler)
                    files = source_dependencies(ninja, directory, [executable, "test_companion_probe.exe"])
                    files.add("test/windows_menu_runtime.py")
                    check(arch + "/eventlog-language", files,
                          [([PYTHON, "test/windows_menu_runtime.py", directory / executable, "--eventlog-only"], ssh_environment(work)),
                           ([PYTHON, "test/windows_menu_runtime.py", directory / executable, "--eventlog-only", "--english"], ssh_environment(work))], compiler=compiler)
                    if arch == "x64":
                        files = source_dependencies(ninja, directory, [executable, "test_companion_probe.exe"])
                        files.add("test/windows_menu_runtime.py")
                        check(arch + "/winscp", files,
                              [([PYTHON, "test/windows_menu_runtime.py", directory / executable, "--winscp-only"], ssh_environment(work))],
                              compiler=compiler)
                    files = source_dependencies(ninja, directory, [executable, utility_executable("pscp", arch)])
                    files.update({"test/file_transfer.py", "test/kittyng-password-vector.json"})
                    for scenario in ("sftp", "scp", "drop", "key"):
                        key = arch + "/transfer-" + scenario
                        entry = cache.get(key, {})
                        environment = None
                        if (args.force_tests or entry.get("fingerprint") != digest_files(files)
                                or entry.get("compiler") != compiler):
                            environment = ssh_environment(work)
                        check(key, files,
                              [([PYTHON, "test/file_transfer.py", directory / executable, scenario], environment)],
                              compiler=compiler)
                    files = source_dependencies(ninja, directory, [executable, "test_key_paths.exe",
                        cli_executable, utility_executable("pscp", arch), utility_executable("psftp", arch)])
                    files.update({"test/file_transfer.py", "test/kittyng-password-vector.json"})
                    key = arch + "/portable-key-runtime"
                    entry = cache.get(key, {})
                    environment = None
                    if args.force_tests or entry.get("fingerprint") != digest_files(files) or entry.get("compiler") != compiler:
                        environment = ssh_environment(work)
                    check(key, files,
                          [([PYTHON, "test/file_transfer.py", directory / executable, "portable-key"], environment)],
                          compiler=compiler)
                    files = source_dependencies(ninja, directory, [executable])
                    files.update({"test/extensions_runtime.py", "test/kittyng-password-vector.json"})
                    key = arch + "/extensions-runtime"
                    environment = None
                    entry = cache.get(key, {})
                    if args.force_tests or entry.get("fingerprint") != digest_files(files) or entry.get("compiler") != compiler:
                        environment = ssh_environment(work)
                    check(key, files, [([PYTHON, "test/extensions_runtime.py", directory / executable], environment)],
                          compiler=compiler)
                run([PYTHON, "tools/package-release.py", arch, directory, "--output", staged], "package-" + arch)
                archive = staged / f"kitty-ng-{report['version']}-{arch}.zip"
                report["archives"].append({"file": archive.name, "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                                           "compiler": compiler})
            for archive in report["archives"]:
                with tempfile.NamedTemporaryFile(dir=output, suffix=".zip", delete=False) as pending:
                    pending_path = Path(pending.name)
                try:
                    shutil.copyfile(staged / archive["file"], pending_path)
                    pending_path.replace(output / archive["file"])
                finally:
                    pending_path.unlink(missing_ok=True)
            (output / "SHA256SUMS.txt").write_text("".join(
                f"{a['sha256']}  {a['file']}\n" for a in report["archives"]), encoding="ascii")
            for arch in args.arch:
                (output / f"SHA256SUMS-{arch}.txt").unlink(missing_ok=True)
        report["status"] = "success"
        print("PASS: local build, tests and ZIP packaging; temporary files removed", flush=True)
    except BaseException as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        (output / "local-build.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
