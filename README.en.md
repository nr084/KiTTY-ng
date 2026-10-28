# KiTTY-ng

**KiTTY-ng 0.85-9 for Windows, Linux and macOS**: a shared English/Russian localization engine, translated SSH messages, and tab fixes. [Release notes](docs/releases/0.85-9.md) · [Download](https://github.com/nr084/KiTTY-ng/releases/tag/v0.85-9).

**macOS Universal 2 · Portable**: runs on Apple Silicon and Intel. Keep profiles and keys outside the app bundle and share the same portable folder with Windows. Native session tabs are included.

**Total Commander plugin: [kitty-ng-fs 1.1.1](https://github.com/nr084/KiTTY-ng/releases/tag/kitty-ng-fs-v1.1.1)** — embedded SSH/SFTP without Klink, with password prompts and server-key confirmation in TC. Installer and source code are available as separate archives.

[Русский](README.md) | **English**

<img src="icons/kitty-ng-forum.png" width="160" alt="KiTTY-ng">

**SSH, files and portable sessions on Windows, macOS and Linux.** KiTTY-ng builds on KiTTY and PuTTY 0.85 with a terminal, connection management, keys and automation in English and Russian. The separate **kitty-ng-fs** SFTP plugin brings saved sessions to Total Commander.

**Portable macOS app with shared Windows profiles.** Keep `KiTTY-ng.app` beside the Windows EXEs and use one `Sessions` directory. Settings and keys stay outside the app; internal file references can be relative to the shared folder.

## Download

| Platform | Release 0.85-9 | Launch |
| --- | --- | --- |
| Linux x86_64 | [AppImage](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x86_64.AppImage) · [AppDir](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x86_64.tar.zst) | `chmod +x`, launch |
| Windows x64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x64.zip) | `kitty-ng-x64.exe` |
| Windows x86 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-x86.zip) | `kitty-ng-x86.exe` |
| Windows arm64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-arm64.zip) | `kitty-ng-arm64.exe` |
| macOS 13+ · Intel / Apple Silicon | [Universal DMG](https://github.com/nr084/kitty-ng/releases/download/v0.85-9/kitty-ng-0.85-9-macos-universal.dmg) | `KiTTY-ng.app` |

[All releases and checksums](https://github.com/nr084/kitty-ng/releases). Each package includes the client and five utilities. Windows EXE names include their architecture; versions are embedded in the programs and included in archive names.

## First launch

1. Download your platform build. Extract the Windows ZIP; on Mac, copy the `.app` from the DMG into a writable folder and eject the image. On Linux, make the AppImage executable (`chmod +x file.AppImage`) and run it; use AppDir or `--appimage-extract-and-run` without FUSE.
2. Place the client beside your existing `kitty.ini`, `Sessions` and `SshHostKeys`, or select the profiles folder. Windows archives include `kitty-ng.portable`. On Mac, use the directory containing `KiTTY-ng.app`, outside the bundle. Sessions beside the app are discovered automatically; a manually selected folder is remembered.
3. Open a profile or create an SSH session. Choose a language and click **Apply**.

The Mac app uses an ad-hoc signature without Developer ID or notarization. If blocked, use **System Settings → Privacy & Security → Open Anyway**.

## Features

- macOS (previously released features): native tabs, temporary connection duplication without saving profiles, and a top menu for the active session. [Details](docs/tabs.en.md).
- Windows/Linux tabs in 0.85-9: duplicate in the same window, drag to reorder, and individual close buttons. [Details](docs/tabs.en.md).

- Session folders, case-insensitive quick search, editing and duplication.
- Original KiTTY profile format and portable key/certificate references.
- SCP/SFTP through kscp, drag-and-drop uploads and downloads.
- KiTTYgen, Kageant and authentication settings from the current session.
- Post-login commands and UTF-8 scripts, delays and reconnection.
- Terminal settings, shortcuts, English and Russian interfaces.

## Total Commander · kitty-ng-fs

[KiTTY-ng FS](kitty-ng-fs/README.en.md) is a separate **Windows x86/x64** WFX plugin. Access SFTP in Total Commander through the embedded PuTTY/KiTTY-ng engine and existing portable SSH sessions: nested groups, transfers, renaming, directories and Unix permissions.

[Download plugin 1.1.1](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.1.1/kitty-ng-fs-1.1.1-x86-x64.zip) · [Separate source archive](https://github.com/nr084/KiTTY-ng/releases/download/kitty-ng-fs-v1.1.1/kitty-ng-fs-1.1.1-source.zip) · [Setup and limitations](kitty-ng-fs/README.en.md)

**Name compatibility:** `mail.example.ru` is displayed as `mail·example·ru`, without moving its suffix to the extension column. Original connection names and remote filenames are preserved; old session paths remain supported.

**New in 1.1.1:** pipelined downloads improve throughput on higher-latency connections; the log shows transferred MiB. File connections run inside WFX without external executables. Passwords, keyboard-interactive, PPK keys and server-key confirmation are handled through TC. Client and plugin releases are independent; installed KiTTY-ng is only needed for the optional terminal command.

## Screenshots

### Windows

<img src="docs/screenshots/session-en.png" width="760" alt="KiTTY-ng — Windows">

### macOS Universal

<img src="docs/screenshots/macos-tabs-en.png" width="760" alt="KiTTY-ng — macOS">

<details>
<summary>KiTTYgen — macOS</summary>

<img src="docs/screenshots/macos-kittygen-en.png" width="760" alt="KiTTYgen — macOS">

</details>

### Linux · GTK 3

<img src="docs/screenshots/linux-session-en.png" width="760" alt="KiTTY-ng — Linux">

## Guides

- [Documentation](docs/README.en.md).
- [Windows](docs/windows.en.md) · [macOS](docs/macos.en.md) · [Linux](docs/linux.en.md).
- [Portable profiles and migration](docs/portable.en.md).
- [Local builds](docs/build.en.md) · [Migration report](docs/migration.md) (Russian).
- [0.85-9 release notes](docs/releases/0.85-9.md) · [Roadmap](docs/planned.md) (Russian).

## Compatibility and validation

KiTTY saved passwords use a reversible format, not a secure secrets vault. Entire profiles encrypted with a master password are unsupported. Retired extensions are listed in the [decision matrix](docs/kitty-migration-decisions.json).

The user confirmed operation on a MacBook Air M3 and Windows ARM64. Automated native Windows checks ran on x64/x86. The current local macOS build is compiled and tested on Mac: ARM64 and x86_64 through Rosetta, SSH, keys, transfers and AppKit. Physical Intel Macs, macOS 13, GSSAPI and real SSH CA handshakes still require dedicated validation. See the [native Mac report](docs/macos-native-validation.md) (Russian).

## Acknowledgements and licenses

Thank you to **Simon Tatham** and all [PuTTY contributors](https://www.chiark.greenend.org.uk/~sgtatham/putty/) for the terminal and SSH implementation that forms the foundation of kitty-ng. Special thanks to **Cyril Dupont (cyd01)**, the author of [KiTTY](https://github.com/cyd01/KiTTY), for its extensions and portable workflow, whose compatibility we continue to preserve.

Original copyright notices and license texts are retained separately in [the PuTTY license](LICENCE) and [the KiTTY license](LICENCE-KiTTY.txt). kitty-ng is an independent continuation maintained by **nr084**.

The base is [PuTTY 0.85](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html). The original KiTTY history is preserved through commit `75fa2abcd220c17249ff7252f8d5224137001f2d`.

The new connected-terminal icon is available as [the image source](icons/kitty-ng.png), with [generation details](icons/kitty-ng-prompt.txt).

English and Russian are built in. Additional language DLLs are deferred; see the [localization architecture](docs/localization.md) (Russian).
