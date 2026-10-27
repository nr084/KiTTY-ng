#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
# Cross compilation only: runtime tests are performed natively on Windows.
plugin="$PWD"
engine="$(cd .. && pwd)"
if [[ -f engine/CMakeLists.txt ]]; then engine="$PWD/engine"; fi
if [[ -n "${TOOLCHAIN_BIN:-}" ]]; then export PATH="$TOOLCHAIN_BIN:$PATH"; fi
mkdir -p dist/development
for arch in x86 x64; do
  if [[ "$arch" == x86 ]]; then target=i686; suffix=wfx; else target=x86_64; suffix=wfx64; fi
  cmake -S "$engine" -B "build/cross-$arch" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$(command -v "$target-w64-mingw32-clang")" \
    -DCMAKE_RC_COMPILER="$(command -v "$target-w64-mingw32-windres")" \
    -DCMAKE_PROJECT_kitty-ng_INCLUDE="$plugin/cmake/embed.cmake" \
    -DKITTYNGFS_DIR="$plugin" -DCMAKE_BUILD_TYPE=Release
  cmake --build "build/cross-$arch" --target kittyngfs --parallel
  cp "build/cross-$arch/kitty-ng-fs.$suffix" dist/development/
done
