#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
# TOOLCHAIN_BIN may point at the bin directory of LLVM-MinGW or MinGW-w64.
if [[ -n "${TOOLCHAIN_BIN:-}" ]]; then export PATH="$TOOLCHAIN_BIN:$PATH"; fi
mkdir -p build/x86 build/x64 dist
for arch in x86 x64; do
  if [[ "$arch" == x86 ]]; then target=i686; suffix=wfx; else target=x86_64; suffix=wfx64; fi
  cc="${target}-w64-mingw32-gcc"
  rc="${target}-w64-mingw32-windres"
  command -v "$cc" >/dev/null
  "$rc" --codepage=65001 -I src src/plugin.rc -O coff -o "build/$arch/plugin.res.o"
  extra=()
  if [[ "$arch" == x86 ]]; then extra+=(-Wl,--kill-at); fi
  "$cc" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
    -shared -static-libgcc -Wl,--no-insert-timestamp -Wl,--dynamicbase -Wl,--nxcompat \
    "${extra[@]}" src/plugin.c src/pathutil.c src/profiles.c src/session_tree.c src/sftp.c src/transport.c \
    "build/$arch/plugin.res.o" src/kittyngfs.def \
    -luser32 -lshell32 -lole32 -lshlwapi -ladvapi32 -lbcrypt \
    -o "dist/kitty-ng-fs.$suffix"
  echo "Built dist/kitty-ng-fs.$suffix"
done
