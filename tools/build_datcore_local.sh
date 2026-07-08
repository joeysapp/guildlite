#!/usr/bin/env bash
# Build the datcore CLI (datcli) locally -- no Windows box, no SSH, no vcpkg. Geometry,
# catalog, search, labels all work here; TEXTURES need the remote Windows build (the x86
# ATEX asm), so this is for the parse/index/label side of the tool. Mirrors build_macos_gui.sh.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DC="$SCRIPT_DIR/datcore"

# Apple clang (NOT Homebrew LLVM -- it can't find the macOS SDK: mbstate_t/pthread errors).
if [ "$(uname -s)" = "Darwin" ]; then CC=/usr/bin/clang; CXX=/usr/bin/clang++; else CC=cc; CXX=c++; fi
command -v cmake >/dev/null 2>&1 || { echo "error: cmake not found (brew install cmake)"; exit 1; }

echo "building datcore (local) -> $DC/bin/datcli"
cmake -S "$DC" -B "$DC/build" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" >/dev/null
cmake --build "$DC/build" -j
echo "done. next:  $DC/bin/datcli setup"
