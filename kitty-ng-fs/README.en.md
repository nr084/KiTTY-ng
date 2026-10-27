# KiTTY-ng FS · Total Commander

[Русский](README.md) | **English** · [KiTTY-ng](https://github.com/nr084/KiTTY-ng)

**SFTP in Total Commander using portable KiTTY-ng sessions.** The `kitty-ng-fs` Windows x86/x64 plugin launches external Klink, sharing connection profiles with the terminal client.

## Download 1.0.2

| Package | Contents |
| --- | --- |
| [Plugin x86 + x64](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.0.2/kitty-ng-fs-1.0.2-x86-x64.zip) | TC installer with documentation; no source code |
| [Source code](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.0.2/kitty-ng-fs-1.0.2-source.zip) | C11, CMake, artwork, tests and packaging; no compiled DLLs |
| [Checksums](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.0.2/SHA256SUMS.txt) | SHA-256 for both archives |

The plugin has its own release cycle: plugin **1.0.2**, client **0.85-6**. KiTTY-ng/Klink, user profiles and keys are not bundled.

## Features

- Nested session groups in TC's Network Neighborhood; groups precede connection links in normal folders-first sorting.
- Browse, upload, download, rename, create and delete; Unix permissions and timestamps.
- Unicode names, cancellation and temporary files before replacing destinations.
- Open the current session in KiTTY-ng with the `terminal` command.
- Relative portable-client paths and plugin-local configuration.

## Installation

1. Prepare KiTTY-ng **0.85-6** and a working saved SSH session. Verify and accept the server host key in the client first.
2. Open the installer ZIP in Total Commander and accept WFX installation. For manual installation select `kitty-ng-fs.wfx`, keeping `kitty-ng-fs.wfx64` beside it.
3. Open **Network Neighborhood → KiTTY-ng FS → [Настройки KiTTY-ng]** and select your portable KiTTY-ng folder. The plugin settings dialog is currently in Russian.
4. Open a group and connection. Initial path `/` is the server root; `.` is the SSH user's home directory.

TC x86 requires `klink-x86.exe`; TC x64 requires `klink-x64.exe`. Windows 10/11, Unicode WFX support and a server SFTP subsystem are required. Settings remain in **`kittyngfs.ini`** for compatibility. When upgrading the old `kittyngfs` package, select its existing directory and update the WFX registration to the new filename, then restart TC. Keep your existing INI.

## Limits and validation

No interactive password/MFA prompts, transfer resume, background queue or direct server-to-server copy. Load passphrase-protected keys into an agent beforehand. This is a TC filesystem plugin, not a Windows drive mount.

[Detailed Russian guide](README.ru.md) · [Upstream delivery validation](VALIDATION.md) · [Publication checks](PUBLICATION.md) · [History](CHANGELOG.md)

[Code review (Russian)](REVIEW.md) · [Plan (Russian)](PLAN.md)

## Development

This directory contains `src/`, `assets/`, `tests/` and `tools/`. Build and test locally with `python build-local.py --toolchain C:/tools/llvm-mingw-20261020`. Alternatives: `build-windows.cmd` (Visual Studio/CMake) or `build-cross.sh` (LLVM-MinGW/MinGW-w64). Put the resulting WFX files in `dist/`, then run `python package.py` to create **separate installer and source archives**. See [test recipes](tests/README.md).

**Author: nr084.** Original code and artwork: [MIT](LICENSE). [Third-party notices](THIRD_PARTY_NOTICES.md). Not an official Total Commander product. KiTTY-ng retains its PuTTY and KiTTY acknowledgements.
