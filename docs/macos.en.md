# KiTTY-NG for macOS

**macOS Portable + shared Windows profiles.** No installation is required. Keep `KiTTY-NG.app` beside the Windows EXEs and use the same `Sessions` folder, host keys and relative SSH-key paths. Settings stay outside the app bundle. The original KiTTY portable profile format is supported.

[Русский](macos.md) | **English**

Download the Universal DMG from [Releases](https://github.com/nr084/kitty-ng/releases). macOS 13 or newer is required. One app and all five bundled helpers contain both ARM64 and Intel x86_64 slices.

## Start and portable profiles

Open the DMG and copy `KiTTY-NG.app` into a writable folder. Eject the image before launching. For shared Windows/Mac profiles, put the app beside the Windows EXEs, `kitty.ini`, `Sessions`, `SshHostKeys` and your key folder. Keep settings outside the app bundle. The folder picker selects the shared portable root; `KITTY_NG_PORTABLE_DIR` and `--portable-dir` can also set it. Do not launch the portable app from the read-only DMG.

The app is locally ad-hoc signed, without Developer ID or notarization. If Gatekeeper blocks it, use System Settings → Privacy & Security → Open Anyway. If App Translocation changes the path, choose your portable root manually.

## Use

Folders appear before profiles, `..` goes up, and Quick search ignores case. New, Edit, Delete and Duplicate manage profiles. Duplicate asks for a new name and opens the settings editor; the original stays unchanged. Choose English/Russian and Apply to save the language. Saved key/certificate/script paths inside the root are relative.

Right-click a terminal for Send files or Download files. In applications that capture mouse reporting, use Shift+right-click. Ctrl+F3 uploads and Ctrl+F4 downloads when KiTTY shortcuts are enabled and not overridden. Dragging files uploads without a destination prompt: known current directory, configured remote path, or `.` on the new connection. Private SSH settings are passed to bundled kscp through a private pipe, not password command-line arguments.

KiTTYgen is in the app menu. Kageant has a menu bar status icon, key management and language selection. Automation, scripts, reconnection, custom shortcuts and additional fonts are configured in the session editor. All settings exposes persisted PuTTY/KiTTY options; complex map values retain their storage syntax.

Windows and macOS use separate default random-seed files. macOS reads `macseedfile` from putty.conf if explicitly set. Existing keys/profiles do not depend on the seed filename.

## Verification and limits

Basic operation is confirmed by the user on an M3 MacBook Air. SSH CA, Kerberos/GSSAPI and other specialized scenarios need separate native verification. Unix utilities are bundled; no GTK or third-party runtime is required. Printing and retired platform-specific extensions are excluded; see the [migration matrix](macos-migration.json).

Smoke-test KiTTY-NG.command uses temporary synthetic keys and profiles; Diagnose KiTTY-NG.command enables NSZombie diagnostics and writes a log beside the app. These are in the image's Instructions folder: copy them beside the app before running.

Build locally on Windows with `python tools/local-build.py --platform macos`, then package the disk image using `tools/macos-dmg.py`. Build tools and the Apple SDK stay under ignored `_tools`; intermediate files are removed. Sources are based on PuTTY by Simon Tatham and contributors, and KiTTY by Cyril Dupont.

Language switching preserves the native search control and the names English/Русский. New SSH profiles default to port 22 before any terminal has been opened; explicitly saved ports are retained. The smoke script contains a native AppKit regression check for these cases.

```text
KiTTY-NG/
├── KiTTY-NG.app
├── kitty-ng-x64.exe
├── kscp-x64.exe
├── kitty-ng.portable
├── kitty.ini
├── Sessions/
├── SshHostKeys/
└── SSH-Keys/
```

KiTTYgen keeps its language names, algorithm selection, bit count, key comment, file path and public-key data unchanged when switching the interface language.

## Interface

![KiTTY-NG](screenshots/macos-session-en.png)

![KiTTYgen](screenshots/macos-kittygen-en.png)
