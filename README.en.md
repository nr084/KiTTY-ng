# KiTTY-ng

**SSH client, file transfers and portable sessions for Windows, macOS and Linux.**

KiTTY-ng develops KiTTY's features on the modern **PuTTY 0.85** codebase. Manage connections, work in multiple tabs, transfer files, create SSH keys and automate everyday commands. English and Russian interfaces are built into the client and utilities.

[Русский](README.md) | **English**

<img src="icons/kitty-ng-forum.png" width="160" alt="KiTTY-ng">

[Download 0.85-10](https://github.com/nr084/KiTTY-ng/releases/tag/v0.85-10) · [Release notes](docs/releases/0.85-10.md) · [Guides](docs/README.md) · [Total Commander plugin](kitty-ng-fs/README.en.md)

**macOS Universal 2 and portable mode.** One app for Apple Silicon and Intel. Profiles are compatible with Windows: share a `Sessions` folder, keys and `kitty.ini`. Data stays outside the `.app` bundle, and files within the portable folder can use relative paths.

## Download

Release 0.85-10 includes six verified packages for Windows, macOS and Linux.

| Platform | Release 0.85-10 | Launch |
| --- | --- | --- |
| Linux x86_64 | [AppImage](https://github.com/nr084/kitty-ng/releases/download/v0.85-10/kitty-ng-0.85-10-x86_64.AppImage) · [AppDir](https://github.com/nr084/kitty-ng/releases/download/v0.85-10/kitty-ng-0.85-10-x86_64.tar.zst) | `chmod +x`, launch |
| Windows x64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-10/kitty-ng-0.85-10-x64.zip) | `kitty-ng-x64.exe` |
| Windows x86 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-10/kitty-ng-0.85-10-x86.zip) | `kitty-ng-x86.exe` |
| Windows arm64 | [ZIP](https://github.com/nr084/kitty-ng/releases/download/v0.85-10/kitty-ng-0.85-10-arm64.zip) | `kitty-ng-arm64.exe` |
| macOS 13+ · Intel / Apple Silicon | [Universal DMG](https://github.com/nr084/KiTTY-ng/releases/download/v0.85-10/kitty-ng-0.85-10-macos-universal.dmg) | `KiTTY-ng.app` |

[All releases and checksums](https://github.com/nr084/kitty-ng/releases). Each package includes the client and five utilities. Windows EXE names include their architecture; versions are embedded in the programs and included in archive names.

## First launch

1. Download your platform build. Extract the Windows ZIP; on Mac, copy the `.app` from the DMG into a writable folder and eject the image. On Linux, make the AppImage executable (`chmod +x file.AppImage`) and run it; use AppDir or `--appimage-extract-and-run` without FUSE.
2. Place the client beside your existing `kitty.ini`, `Sessions` and `SshHostKeys`, or select the profiles folder. Windows archives include `kitty-ng.portable`. On Mac, use the directory containing `KiTTY-ng.app`, outside the bundle. Sessions beside the app are discovered automatically; a manually selected folder is remembered.
3. Open a profile or create an SSH session. Choose a language and click **Apply**.

The Mac app uses an ad-hoc signature without Developer ID or notarization. If blocked, use **System Settings → Privacy & Security → Open Anyway**.

## Features

### Connections and SSH security

- Password, SSH key and agent authentication, including keyboard-interactive and SSH certificates.
- Server host-key verification and trusted-key storage; configurable key-exchange and encryption algorithms.
- Local, remote and dynamic SSH port forwarding.
- Proxy connections and shared profiles from the `Proxies` directory.
- Configurable reconnection after connection loss. See each platform guide for resume-from-sleep and authentication details.

### Sessions and tabs

- Saved connections in nested folders, case-insensitive quick search, and profile creation, editing, deletion and duplication.
- Duplicate an active session into a new tab; click to switch, drag to reorder, and close tabs individually.
- Tab context menu: duplicate, close, move left or right. The tab bar is hidden for a single session; tabs can be disabled.
- Quick connections without saving a profile on macOS and Linux; their duplicates remain temporary too.
- Session notes and settings changes for an open connection.

[Working with tabs](docs/tabs.en.md)

### Files and external tools

- Upload and download over SCP/SFTP using the bundled `kscp`, including multiple files and directories.
- Drag local files into an SSH terminal window to upload them.
- Pass the current connection's host, port, proxy and authentication settings to the transfer helper.
- Manage remote files interactively with `ksftp`.
- **Windows:** launch an external WinSCP installation from the menu with a configurable executable path. WinSCP is not bundled.
- **Total Commander:** the separate kitty-ng-fs plugin opens SFTP directly in its file panels; see below.

### Automation

- Automatic commands after SSH login and UTF-8 startup scripts.
- Configurable delays after login, between lines and between characters; manually send a command or script file.
- Keyboard shortcuts for client actions and sending text.
- **Quick commands:** up to 20 named commands shared across sessions, each with an optional Enter action. Edit them under **Window → Quick commands**; commands live in global settings rather than individual profiles.

Scripts are sent to the remote terminal. [Quick command setup](docs/quick-commands.en.md) · [Migrated KiTTY extensions](docs/migration.md)

### Terminal and appearance

- Configurable fonts, colours, encoding, keyboard, scrollback and window behaviour; Unicode and full-screen terminal applications.
- Copy and paste, input protection, full-screen mode and a connection event log.
- Open HTTP/HTTPS links with Ctrl + click. OSC 52 clipboard writes require explicit permission; remote clipboard reads are not supported.
- **Windows:** light, dark or automatic system themes for the client and GUI utilities, tray minimisation, transparency and always-on-top mode.
- **macOS:** native AppKit interface and active-session menus. **Linux:** GTK 3 interface styled by the desktop environment.
- English and Russian interfaces through a shared language core. User data and remote server output are not translated.

[Windows themes](docs/windows-themes.en.md)

### Portable settings

Compatible with the original KiTTY file format: `Sessions`, `SshHostKeys`, `Proxies` and `kitty.ini`. Share a settings folder between Windows, macOS and Linux. Keys, detached certificates, scripts and icons inside the portable root use relative references; external files use absolute paths.

Windows supports portable and registry storage. Shared profiles do not imply identical platform features: see the [Windows](docs/windows.en.md), [macOS](docs/macos.en.md) and [Linux](docs/linux.en.md) guides for details and limitations.

[Portable mode and migration from KiTTY](docs/portable.en.md)

### Five bundled utilities

| Utility | Purpose |
| --- | --- |
| **KiTTYgen** | Generate, load and export SSH keys, change passphrases and copy public keys. |
| **Kageant** | SSH agent: keep loaded keys in memory and authenticate connections. |
| **Klink** | Command-line connections and command execution. |
| **Kscp** | Transfer files and directories over SCP/SFTP. |
| **Ksftp** | Interactive SFTP client for managing remote files. |

Windows utilities match the client's architecture; all five macOS helpers are Universal 2. Linux packages include the client and helpers in one AppImage/AppDir.

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
- [0.85-10 release notes](docs/releases/0.85-10.md) · [Roadmap](docs/planned.md) (Russian).

## Compatibility and validation

KiTTY saved passwords use a reversible format, not a secure secrets vault. Entire profiles encrypted with a master password are unsupported. Retired extensions are listed in the [decision matrix](docs/kitty-migration-decisions.json).

The user confirmed operation on a MacBook Air M3 and Windows ARM64. Automated native Windows checks ran on x64/x86. The current local macOS build is compiled and tested on Mac: ARM64 and x86_64 through Rosetta, SSH, keys, transfers and AppKit. Physical Intel Macs, macOS 13 and live Kerberos credentials still require dedicated validation. Real OpenSSH host/user certificate handshakes have passed on both architectures. See the [native Mac report](docs/macos-native-validation.md) (Russian).

## Acknowledgements and licenses

Thank you to **Simon Tatham** and all [PuTTY contributors](https://www.chiark.greenend.org.uk/~sgtatham/putty/) for the terminal and SSH implementation that forms the foundation of kitty-ng. Special thanks to **Cyril Dupont (cyd01)**, the author of [KiTTY](https://github.com/cyd01/KiTTY), for its extensions and portable workflow, whose compatibility we continue to preserve.

Original copyright notices and license texts are retained separately in [the PuTTY license](LICENCE) and [the KiTTY license](LICENCE-KiTTY.txt). kitty-ng is an independent continuation maintained by **nr084**.

The base is [PuTTY 0.85](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html). The original KiTTY history is preserved through commit `75fa2abcd220c17249ff7252f8d5224137001f2d`.

The new connected-terminal icon is available as [the image source](icons/kitty-ng.png), with [generation details](icons/kitty-ng-prompt.txt).

English and Russian are built in. Additional language DLLs are deferred; see the [localization architecture](docs/localization.md) (Russian).
