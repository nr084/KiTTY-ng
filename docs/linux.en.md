# Linux port: build and validation

[Русский](linux.md) · [Documentation](README.en.md)

The native GTK 3 Linux client ships with **0.85-5** as an x86_64 AppImage and an AppDir archive.

The GUI uses GTK 3 and the existing PuTTY terminal. SSH, cryptography, SCP/SFTP, terminal processing and key generation share the existing code. Portable profile storage, relative key paths, saved-password compatibility and private configuration pipes reuse the macOS port's common implementation.

The bundle contains `kitty-ng`, `kittygen`, `kageant`, `klink`, `kscp` and `ksftp`. The session browser supports search, nested folders and profile creation, editing, deletion and duplication. New SSH profiles use port **22**. English/Russian switching preserves user data.

## Portable distribution

The primary format is an **x86_64 AppImage**. A `tar.zst` AppDir archive provides an alternative for systems without FUSE. Native tests run on Ubuntu 26.04, using binaries linked against an Ubuntu 22.04 library baseline. Other distributions are not yet claimed as supported.

```sh
chmod +x kitty-ng-0.85-5-x86_64.AppImage
./kitty-ng-0.85-5-x86_64.AppImage --portable-dir '/path/to/shared folder'
./kitty-ng-0.85-5-x86_64.AppImage --keygen
./kitty-ng-0.85-5-x86_64.AppImage --agent
./kitty-ng-0.85-5-x86_64.AppImage --tool klink -V
./kitty-ng-0.85-5-x86_64.AppImage --appimage-extract-and-run
```

Select the shared Windows/macOS portable folder in the browser or pass `--portable-dir`. By default the portable root is beside the AppImage, or beside an extracted AppDir. `Sessions`, `kitty.ini`, `values.ini`, `SshHostKeys` and internal relative paths retain the legacy KiTTY format. Linux writes its random-state file to `kitty-ng-linux.rnd`.

## Local Windows build

Compilation and packaging run **locally on Windows**. The VM only provides target headers/libraries and runs finished binaries. Neither command below starts a compiler over SSH.

Use the installed Clang/LLD 23.1.3, CMake, Ninja and Perl. The target dependencies live in `_tools/linux/sysroot`; `_tools/linux/target.json` records GTK flags and package versions. These files, downloaded tools and the SSH key are excluded from Git.

To import target dependencies again from the prepared baseline: `python tools/linux-sysroot.py --host <address> --remote-root /opt/kitty-ng/baseline`. The tool uses pinned SSH host keys and the dedicated key in `_tools/linux/ssh`, verifies the archive SHA-256 and does not compile remotely.

```powershell
python tools/local-build.py --platform linux
python tools/linux-package-windows.py
```

ELF binaries are written to `build/linux-windows`, packages to `dist/linux`. Packaging uses the Windows SquashFS Tools NG executable and the official AppImage runtime. Linux permissions are explicitly stored in archive entries. Reports: `build/linux-windows/linux-cross-build.json` and `dist/linux/linux-package.json`.

## Validation scope

Native checks cover the browser, profile creation/duplication, SSH port 22, relative key paths, unknown legacy fields, actual OpenSSH connections, relocated portable folders, Unicode SFTP transfers, agent authentication and key operations, and KiTTYgen generation with English/Russian switching. GUI startup from the Windows-built AppImage is also checked as a regular user.

Additional native checks cover actual Shift+arrow selection and F2 in MC, URI drag-and-drop uploads to the home directory without a destination prompt, GUI downloads, and automatic reconnect after a dropped established SSH connection. Concurrent external profile updates are preserved.

The verified desktop is Ubuntu 26.04 with Xfce/X11, including regular-user operation. Wayland, other distributions and ARM64 have not been verified. Windows shell integrations are outside this Linux package. See the [Linux migration matrix](linux-migration.json).

New profiles use Xterm 216+ function keys to match `TERM=xterm`. Explicit saved modes take precedence; select Xterm 216+ in Keyboard settings if an old profile has incompatible F1–F4 behavior. Closing the terminal cancels its active file transfers.

Enable custom shortcuts in Window settings: Ctrl+F2 sends a command, Ctrl+F3 uploads, Ctrl+F4 downloads and Ctrl+F9 locks/unlocks input, including remote mouse motion, buttons and wheel. Ctrl+F6 minimizes, Ctrl+F7 toggles always-on-top, and Shift+F12 replays startup commands. The shared `[Shortcuts]` section supports custom text bindings. Ctrl-click opens HTTP/HTTPS links using the default browser. Kageant can remove an individual key by fingerprint or comment.
