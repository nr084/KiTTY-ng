# KiTTY-ng FS · Total Commander

[Русский](README.md) | **English** · [KiTTY-ng](https://github.com/nr084/KiTTY-ng)

**SFTP in Total Commander using portable KiTTY-ng profiles.** Version 1.1.1 embeds SSH and SFTP into the x86/x64 WFX DLL. File transfers require no Klink, Plink or separate crypto DLL.

> **1.1.1:** faster downloads on higher-latency connections, with transferred MiB shown in the TC log. Embedded SSH/SFTP requires no Klink. See [implementation and validation notes](EMBEDDED-SSH.md).

## Download 1.1.1

| Package | Contents |
| --- | --- |
| [Plugin x86 + x64](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.1.1/kitty-ng-fs-1.1.1-x86-x64.zip) | TC installer with documentation; no source code |
| [Source code](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.1.1/kitty-ng-fs-1.1.1-source.zip) | C11, CMake, artwork, tests and packaging; no compiled DLLs |
| [Checksums](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.1.1/SHA256SUMS.txt) | SHA-256 for both archives |

The plugin has its own release cycle: plugin **1.1.1**, client **0.85-6**. KiTTY-ng/Klink, user profiles and keys are not bundled.

## Dots in session names

`mail.example.ru` is displayed as **mail·example·ru**, keeping the full session label in TC's Name column. Only virtual session labels change: the SSH engine still loads the original profile name, profiles are not renamed, and remote filenames such as `file.tar.gz` are untouched.

A literal middle dot in a source name is displayed as `%C2%B7`; literal percent signs are escaped. Existing `[SSH]` collision suffixes remain supported. Old nested paths and flat bookmarks remain compatible aliases, with current visible names taking precedence in ambiguous cases. Opening an alias returns the new canonical path.

## Features

- Nested session groups in TC's Network Neighborhood; groups precede connection links in normal folders-first sorting.
- Browse, upload, download, rename, create and delete; Unix permissions and timestamps.
- Unicode names, cancellation and temporary files before replacing destinations.
- Open the current session in KiTTY-ng with the `terminal` command.
- Relative portable-client paths and plugin-local configuration.

## Installation

1. Prepare portable KiTTY-ng SSH profiles and any referenced private keys. The embedded development build needs no client executable for file transfers.
2. Open the installer ZIP in Total Commander and accept WFX installation. For manual installation select `kitty-ng-fs.wfx`, keeping `kitty-ng-fs.wfx64` beside it.
3. Open **Network Neighborhood → KiTTY-ng FS → [Настройки KiTTY-ng]** and select your portable KiTTY-ng folder. The plugin settings dialog is currently in Russian.
4. Open a group and connection. Initial path `/` is the server root; `.` is the SSH user's home directory.

The embedded build requires Windows 10/11, Unicode WFX support and an SFTP server. New or changed server keys require confirmation in TC. Passwords and private-key passphrases are requested through TC as well. Settings remain in **`kittyngfs.ini`** for compatibility. When upgrading the old `kittyngfs` package, select its existing directory and update the WFX registration to the new filename, then restart TC. Keep your existing INI.

## Limits and validation

Saved passwords, interactive SSH prompts, PPK keys and Kageant/Pageant are supported. No transfer resume, background queue or direct server-to-server copy. External proxy commands are disabled; built-in SSH/HTTP/SOCKS proxies remain available. This is a TC filesystem plugin, not a Windows drive mount.

[Detailed Russian guide](README.ru.md) · [Upstream delivery validation](VALIDATION.md) · [Publication checks](PUBLICATION.md) · [History](CHANGELOG.md)

[Code review (Russian)](REVIEW.md) · [Plan (Russian)](PLAN.md)

## Development

Build locally with `python build-local.py --toolchain C:/tools/llvm-mingw-20261020` (CMake and Ninja required). Output goes to `dist/development/`, preserving published packages. The engine is compiled from the parent repository, or from `engine/` in a standalone source package. `build-windows.cmd` uses the same workflow; `build-cross.sh` cross-compiles without claiming native tests. See [test recipes](tests/README.md).

**Author: nr084.** Original code and artwork: [MIT](LICENSE). [Third-party notices](THIRD_PARTY_NOTICES.md). Not an official Total Commander product. KiTTY-ng retains its PuTTY and KiTTY acknowledgements.
