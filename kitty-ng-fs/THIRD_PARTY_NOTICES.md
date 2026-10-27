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

The development build statically links the existing PuTTY/KiTTY-ng SSH,
cryptography, settings and portable-storage code from this repository.
PuTTY: Simon Tatham and contributors; KiTTY: Cyril Dupont and contributors.
The full MIT notices are included in PUTTY-LICENCE.txt and KITTY-LICENCE.txt.
Standalone source packages include the exact engine sources under engine/.
No Klink executable, user private key, session, password or host-key cache
is bundled. The optional terminal command still launches a separately
installed KiTTY-ng GUI; it is not used for file transfers.

The SFTP implementation was written for this plugin against the public SFTP
v3 wire description and OpenSSH extension documentation. OpenSSH source is
not linked or bundled. A system OpenSSH sftp-server was used in tests.

The shipped binaries were compiled with LLVM-MinGW (UCRT). Compiler runtime
support and Windows SDK import interfaces are provided under their respective
LLVM and mingw-w64 licenses. Windows system DLLs/UCRT are provided by Windows;
no non-system runtime DLL is required by the import table.

Historical release tests used Wine and an official KiTTY-ng release. The embedded development tests use native Windows WFX harnesses and a loopback Paramiko server with synthetic keys. These testing tools are not bundled.
