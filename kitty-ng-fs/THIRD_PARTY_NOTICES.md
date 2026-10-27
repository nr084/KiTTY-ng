# Third-party interfaces and external tools

KiTTY-ng FS is authored by nr084.
Project homepage: https://github.com/nr084/KiTTY-ng .
It is not an official Total Commander release.

The plugin's original C code and icon artwork are licensed under MIT.
The terminal motif references the type of application; no KiTTY-ng bitmap,
logo file or source icon is bundled. SVG/ICO sources are in `assets/`.
The plugin's ABI declarations
follow Christian Ghisler's published Total Commander filesystem plugin SDK:
https://ghisler.github.io/WFX-SDK/contents.htm . The SDK sample implementation
is not incorporated into this plugin.

KiTTY-ng / Klink is launched as an external executable supplied by the user.
No KiTTY-ng executable, private key, saved session, password, host-key cache,
or source implementation is bundled. KiTTY-ng's own license and notices apply
to that separately obtained software: https://github.com/nr084/KiTTY-ng .

The SFTP implementation was written for this plugin against the public SFTP
v3 wire description and OpenSSH extension documentation. OpenSSH source is
not linked or bundled. A system OpenSSH sftp-server was used in tests.

The shipped binaries were compiled with LLVM-MinGW (UCRT). Compiler runtime
support and Windows SDK import interfaces are provided under their respective
LLVM and mingw-w64 licenses. Windows system DLLs/UCRT are provided by Windows;
no non-system runtime DLL is required by the import table.

Tests were executed using temporary Wine and Paramiko installations and an
official KiTTY-ng release. These testing tools are not bundled.
