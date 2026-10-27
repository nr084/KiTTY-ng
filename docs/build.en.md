# Local builds and versioning

Before compiling, check the latest stable compiler with its official provider. If it is newer than the compiler used by an existing package, rebuild the package before testing it. Results from the previous binary do not validate the rebuilt one.

[Documentation](README.en.md) | [Русский](build.md)

## Windows

The main build pipeline runs locally on Windows x64. It requires **Python 3.10+ with pip**, **Git on PATH**, and internet access to prepare or update tools. Visual Studio and a separately installed Windows SDK are not required. GitHub Actions are not used for builds.

```powershell
python tools/local-build.py
```

The pipeline checks the latest stable LLVM-MinGW release and installs it under `_tools/llvm-mingw/<release>/toolchain`. The local compiler is retained between builds. CMake and Ninja are prepared in a temporary directory. Download SHA-256 digests are verified. The pipeline builds all three architectures, runs the required checks, and writes three ZIP files to `dist`.

Temporary dependencies and intermediate files are removed even after failure. Logs are retained in `dist/local-build-logs`, the build report in `dist/local-build.json`, and checksums in `dist/SHA256SUMS.txt`. Nothing is published to GitHub by this command.

Previously successful checks are reused when their source files, headers, test scenarios and compiler are unchanged. Keep `dist/local-tests.json` between builds. An existing `dist/validation.json` can also provide previous successful results when dependencies match its recorded commit. Without prior evidence, the first build runs the full x64/x86 checks. ARM64 executables are checked for PE architecture and embedded versions; native testing on an ARM64 device is still required. Builds are not Authenticode-signed.

Useful commands:

```powershell
python tools/local_toolchain.py
python tools/local-build.py --arch x64
python tools/local-build.py --offline --toolchain C:\Toolchains\llvm-mingw --cmake C:\Tools\cmake.exe --ninja C:\Tools\ninja.exe
```

Offline mode requires Paramiko to be installed in your Python environment if changed SSH logic needs testing. Online builds prepare it in a temporary environment only when needed. `--force-tests` explicitly reruns all checks, for example when reproducing an error. Build tool information and the source commit are included in `BUILD-INFO.json` inside each archive. A failed build or test stops the pipeline and records the error.

Visual Studio 2022 with the appropriate C++ components and Windows SDK remains an alternative:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel
```

Use `python tools/set-release-stage.py 1` for `0.85-1`, `2` for `0.85-2`, and so on. Change the version only after an explicit instruction to publish a release. After editing translations, run `python tools/generate-translations.py`.

## macOS Universal / DMG

Build macOS on a **Mac**, separately from Windows/Linux. Python 3.14+ with pip and Apple Command Line Tools (`xcode-select --install`) are required.

```sh
python3 tools/local-build.py --platform macos
# Reuse installed tools without network access:
python3 tools/local-build.py --platform macos --offline
```

The command checks the latest stable LLVM, retains verified LLVM/CMake/Ninja/Pillow under `_tools/macos/native`, uses the system Apple SDK, and records exact tool versions and provenance. It builds all six ARM64/x86_64 executables, merges Universal 2, applies ad-hoc signatures, and runs native unit/CLI/AppKit tests. Rosetta is required for x86_64 execution on Apple Silicon. ZIP and HFS+ DMG packaging uses `ditto`/`hdiutil`; the read-only mounted image is checked for identical contents, valid signatures, and executable permissions.

Output is under `dist/macos-native`: `KiTTY-ng.app`, Universal ZIP/DMG, `macos-build.json`, `macos-dmg.json`, `SHA256SUMS.txt`, and logs. Existing archives in `dist` are preserved. Temporary directories are removed; installed build tools remain. `--skip-dmg` is available for diagnostics. `--compiler /usr/bin/clang --offline` selects Apple Clang for diagnostic builds; regular builds use the latest stable LLVM.

`test/macos_native_runtime.py` provides network acceptance with synthetic credentials. Paramiko is only needed for its loopback server. See the [native validation report](macos-native-validation.md) for commands and coverage limits. Ad-hoc signatures do not provide Developer ID or Apple notarization.

## Release rules

`0.85` is the imported PuTTY base version. The suffix is the KiTTY-ng revision: `0.85-1`, `0.85-2`, `0.85-3`, `0.85-5`. Tags use `v0.85-5`. The client and all utilities share the embedded version. ZIP/DMG filenames carry the version; Windows EXE filenames carry architecture only.

Increment the revision only after an explicit release instruction. Documentation updates do not create a release. Existing published releases are retained.

## Linux · AppImage / AppDir

```powershell
python tools/local-build.py --platform linux
python tools/linux-package-windows.py
```

[Linux guide](linux.en.md) · [Руководство Linux](linux.md).

## Complete release build

```powershell
# Windows: Windows/Linux packages
python tools/release-build.py --build-only
```

```sh
# Mac: macOS package, then assemble matching Windows/Linux artifacts:
python3 tools/release-build.py --build-only
python3 tools/release-build.py --assemble-only
```

Windows/Linux builds are prepared separately; macOS builds run on a Mac. Release assembly verifies version and commit provenance, matching native macOS/Linux acceptance, and checksums. Publication to the existing GitHub repository requires a separate explicit release request.

An explicit request to update only the Mac asset of an existing release uses `python3 tools/update-macos-release.py --publish` after committing/pushing sources, building on Mac and completing acceptance. It preserves the existing tag and Windows/Linux assets, renames the previous DMG as an archived asset, uploads the new primary DMG and updates Russian release notes. Without `--publish`, it only saves a GitHub state snapshot. New full releases continue to use a new revision.
