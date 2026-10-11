# kitty-ng

[Русский](README.md) | **English**

SSH, Telnet and serial terminal for Windows, based on PuTTY 0.85, with English and Russian interfaces and support for KiTTY portable settings.

<img src="icons/kitty-ng.png" width="192" alt="Two connected terminals">

**Test version: 0.85-test.** Migration of KiTTY extensions is ongoing. This release uses a modern PuTTY codebase with selected KiTTY features; it does not yet include every feature of the original KiTTY. The [migration report](docs/migration.md) describes the current scope in Russian.

## Download and run

Download the appropriate archive from [Releases](https://github.com/nr084/kitty-ng/releases): **x64**, **x86** or **ARM64**. Extract it and run `kitty-ng-x64.exe`, `kitty-ng-x86.exe` or `kitty-ng-arm64.exe`. No installation is required.

Each release contains three archives: `kitty-ng-0.85-test-x64.zip`, `kitty-ng-0.85-test-x86.zip` and `kitty-ng-0.85-test-arm64.zip`. Every executable inside an archive uses the matching architecture suffix. For example, the x64 archive contains:

```text
kitty-ng-x64.exe
klink-x64.exe
kscp-x64.exe
ksftp-x64.exe
kittygen-x64.exe
kageant-x64.exe
```

The other archives use `-x86` or `-arm64`. Executable filenames do not include a release number; embedded version information is retained. Archive checksums are listed in the release description. The utilities are based on Plink, PSCP, PSFTP, PuTTYgen and Pageant from PuTTY 0.85.

Archives enable portable mode through the `kitty-ng.portable` marker. To use registry storage, remove the marker and ensure that `kitty.ini` does not set `savemode=dir`. Registry mode uses `HKCU\Software\KiTTY-NG`.

## Companion utilities

The client launches companion executables of its own architecture from its own directory. For example, the x64 client selects `kscp-x64.exe`. It checks the actual PE architecture and does not search PATH for a replacement. Windows taskbar tasks use the corresponding kageant and kittygen; kageant opens the matching kitty-ng client.

To transfer a file using the matching kscp:

```powershell
.\kitty-ng-x64.exe -launch-utility kscp -load "Production/root" "C:\Files\example.txt" root@example.com:/tmp/
```

`-launch-utility` also accepts `klink`, `ksftp`, `kittygen` and `kageant`. Remaining arguments are passed to the utility, and its exit code is returned. In an open SSH session, drop files or folders onto the terminal window, or use **Send files...** in the window system menu. **Receive file...** downloads a remote file.

![File transfer settings (Russian interface)](docs/screenshots/transfer-ru.png)

**Connection → File transfer** selects SFTP/SCP, the remote directory, kscp options and an optional alternate destination. Legacy KiTTY settings `PSCPRemoteDir`, `PSCPOptions`, `WinSCPProtocol`, `SFTPConnect`, `SCPAutoPwd`, plus `kitty.ini` options `pscpport` and `uploaddir`, are supported. The current-directory option queries `pwd` through an interactive POSIX shell and requires remote window-title changes to be enabled. Upload is cancelled if the directory cannot be obtained within five seconds.

Transfers inherit the current session's SSH port, proxy, keys and saved root password. The saved password reaches the helper through memory, without a temporary profile or a password argument. Multiple files, recursive folders and Unicode names are supported. Keep the matching architecture's client and kscp together.

```powershell
.\kitty-ng-x64.exe -load "Production/root" -upload "C:\Files\example.txt"
.\kitty-ng-x64.exe -load "Production/root" -download "/tmp/example.txt" "C:\Files\example.txt"
```

## Interface language

Open **Window → Appearance → Interface language** and select **English** or **Русский** from the dropdown. The choice is saved and the interface updates immediately. You can also run `kitty-ng-x64.exe --language en` or `--language ru`.

Configuration panels, menus, standard dialogs and client messages are translated. Session names, host keys, protocol identifiers and remote terminal output retain their original content. License texts remain in their original language.

The screenshots below show the Russian interface:

![Interface language dropdown](docs/screenshots/language-ru.png)

![Session configuration](docs/screenshots/session-ru.png)

## Migrating from portable KiTTY

First make a backup of your old KiTTY directory. Place kitty-ng in the copy alongside `kitty.ini`, `Sessions` and `SshHostKeys`, or specify the settings directory explicitly:

```powershell
.\kitty-ng-x64.exe --portable-dir "D:\Terminal\KiTTY"
```

The client supports legacy `Key\value\` session files, `%XX` encoding, nested directories, configured filename extensions, and paths from `putty.conf`. Unknown KiTTY fields are retained when a session is saved. The session window provides a folder selector and a name filter.

Saved passwords are read from the legacy `Password` field using `cryptsalt`. This includes root login, SSH password authentication and keyboard-interactive password prompts. The format remains compatible with KiTTY and is reversible: **it is not a secure secrets vault**. Someone with access to a session file can recover its password. Do not publish personal `Sessions`, `kitty.ini` files or private keys.

Entire profiles encrypted with a master password are not yet supported. If a legacy password cannot be decoded, its original value is preserved; enter the password manually to replace it. Background images, Zmodem, FAR2L integration, RuTTY/scripts, port knocking and other legacy extensions still require migration.

## Versions

The numeric version matches the base PuTTY release. Test stages use `0.85-test`, `0.85-test-2` and subsequent suffixes; a stable release uses `0.85`. Git tags start with `v`. Utilities and the main client share the same embedded release version. See the [upstream utilities audit](docs/putty-utilities-audit.json).

## Local build

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

Use `python tools/set-release-stage.py test-2` or `stable` to change the release stage. After editing translations, run `python tools/generate-translations.py`.

## Acknowledgements and licenses

Thank you to **Simon Tatham** and all [PuTTY contributors](https://www.chiark.greenend.org.uk/~sgtatham/putty/) for the terminal and SSH implementation that forms the foundation of kitty-ng. Special thanks to **Cyril Dupont (cyd01)**, the author of [KiTTY](https://github.com/cyd01/KiTTY), for its extensions and portable workflow, whose compatibility we continue to preserve.

Original copyright notices and license texts are retained separately in [the PuTTY license](LICENCE) and [the KiTTY license](LICENCE-KiTTY.txt). kitty-ng is an independent continuation maintained by **nr084**.

The base is [PuTTY 0.85](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html). The original KiTTY history is preserved through commit `75fa2abcd220c17249ff7252f8d5224137001f2d`.

The new connected-terminal icon is available as [the image source](icons/kitty-ng.png), with [generation details](icons/kitty-ng-prompt.txt).
