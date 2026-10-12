# Local builds and versioning

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

```powershell
python tools/local-build.py --platform macos
python tools/macos-dmg.py
```

The first command builds all six executables for ARM64 and x86_64, merges and signs them, and creates an intermediate Universal ZIP. The second packages the app and instructions as an HFS+ DMG, preserving executable permissions. Reports: `dist/macos-build.json` and `dist/macos-dmg.json`. The ZIP may be removed after the DMG is prepared.

Tools are retained under `_tools/macos`; temporary directories are cleaned. Rebuild without network access with `python tools/local-build.py --platform macos --offline`. Structural and signature checks on Windows do not replace native AppKit and Gatekeeper checks on Mac.

## Release rules

`0.85` is the imported PuTTY base version. The suffix is the KiTTY-NG revision: `0.85-1`, `0.85-2`, `0.85-3`. Tags use `v0.85-3`. The client and all utilities share the embedded version. ZIP/DMG filenames carry the version; Windows EXE filenames carry architecture only.

Increment the revision only after an explicit release instruction. Documentation updates do not create a release. Existing published releases are retained.
