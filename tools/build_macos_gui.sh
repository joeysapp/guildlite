#!/usr/bin/env bash
# ------------------------------------------------------------------------------
# build_macos_gui.sh -- build the local macOS SDL2+Metal ImGui control console (gui/).
# ------------------------------------------------------------------------------
# Invoked by `./build.sh --macos`. Uses ONLY local tools (cmake + pkg-config + clang +
# the vendored Dear ImGui submodule) -- no Windows box, no SSH pipeline, no vcpkg. Builds
# Guildlite.app, which drives injected Windows clients over SSH and doubles as an ImGui
# sandbox. Runs fully on a Mac with none of the cross-build machinery set up.
#
# Flags (forwarded from build.sh):
#   --run        launch Guildlite.app after building
#   --selftest   after building, run it headless (--selftest) and check the OK marker
#   --debug      build Debug instead of Release
#   --clean      wipe gui/build first
# ------------------------------------------------------------------------------
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" && pwd -P)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
GUI_DIR="$ROOT/gui"
BUILD_DIR="$GUI_DIR/build"

CONFIG="Release"; DO_RUN=""; DO_CLEAN=""; DO_SELFTEST=""
for a in "$@"; do
    case "$a" in
        --macos|--gui) ;;                    # the dispatch flag from build.sh -- consumed here
        --debug)    CONFIG="Debug" ;;
        --release)  CONFIG="Release" ;;
        --run)      DO_RUN=1 ;;
        --clean)    DO_CLEAN=1 ;;
        --selftest) DO_SELFTEST=1 ;;
        -h|--help)  sed -n '2,22p' "$0"; exit 0 ;;
        *)          printf '[macos-gui] ignoring unknown arg: %s\n' "$a" >&2 ;;
    esac
done

log() { printf '\033[36m[macos-gui]\033[0m %s\n' "$*"; }
die() { printf '\033[31m[macos-gui] FATAL:\033[0m %s\n' "$*" >&2; exit 1; }

# --- preflight: everything the local build needs -----------------------------
command -v cmake      >/dev/null 2>&1 || die "cmake not found (brew install cmake)"
command -v pkg-config >/dev/null 2>&1 || die "pkg-config not found (brew install pkg-config)"
pkg-config --exists sdl2 2>/dev/null  || die "SDL2 not found (brew install sdl2)"
[ -f "$ROOT/third_party/imgui/imgui.h" ] || \
    die "Dear ImGui submodule missing -- run: git submodule update --init third_party/imgui"

# Pin Apple's clang (Command Line Tools / Xcode), NOT Homebrew LLVM. Homebrew clang++ ships
# its own libc++ that can't find mbstate_t/pthread against the macOS SDK -- and we need
# Apple's Objective-C++ + Metal toolchain regardless. xcrun resolves whichever is active.
command -v xcrun >/dev/null 2>&1 || die "xcrun not found (install the Xcode Command Line Tools)"
CC="$(xcrun -f clang)"; CXX="$(xcrun -f clang++)"; OBJCXX="$CXX"
SDKROOT="$(xcrun --show-sdk-path)"
export CC CXX OBJCXX SDKROOT

[ -n "$DO_CLEAN" ] && { log "clean: rm -rf $BUILD_DIR"; rm -rf "$BUILD_DIR"; }

# A cache configured with a different compiler (e.g. a stray Homebrew clang) is poison --
# CMake pins the compiler at first configure. Reconfigure clean if it drifted.
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cached="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$BUILD_DIR/CMakeCache.txt")"
    if [ -n "$cached" ] && [ "$cached" != "$CXX" ]; then
        log "compiler changed ($cached -> $CXX); reconfiguring clean"
        rm -rf "$BUILD_DIR"
    fi
fi

# Prefer Ninja if present; otherwise CMake's default (Unix Makefiles) is fine.
# Plain string (not an array) so this stays safe under `set -u` on macOS bash 3.2.
GEN_ARGS=""; GEN_NOTE=""
if command -v ninja >/dev/null 2>&1; then GEN_ARGS="-G Ninja"; GEN_NOTE=" [ninja]"; fi

log "configure ($CONFIG)$GEN_NOTE"
# shellcheck disable=SC2086  # $GEN_ARGS must word-split into two argv tokens
cmake -S "$GUI_DIR" -B "$BUILD_DIR" $GEN_ARGS -DCMAKE_BUILD_TYPE="$CONFIG" >/dev/null

log "build"
cmake --build "$BUILD_DIR" --config "$CONFIG" -j

APP="$BUILD_DIR/Guildlite.app"
BIN="$APP/Contents/MacOS/Guildlite"
[ -x "$BIN" ] || BIN="$BUILD_DIR/Guildlite"          # non-bundle fallback
[ -x "$BIN" ] || die "build produced no runnable binary under $BUILD_DIR"
log "built: $APP"

if [ -n "$DO_SELFTEST" ]; then
    log "selftest (headless: hidden window, render, exit)"
    if out="$("$BIN" --selftest 2>&1)"; then
        if printf '%s\n' "$out" | grep -q "SELFTEST OK"; then
            log "selftest PASS -- $(printf '%s' "$out" | grep 'SELFTEST OK')"
        else
            die "selftest ran but printed no OK marker:
$out"
        fi
    else
        die "selftest exited non-zero:
$out"
    fi
fi

[ -n "$DO_RUN" ] && { log "launching"; open "$APP"; }

echo
log "done ($CONFIG)"
echo "  Run:      open \"$APP\""
echo "  Headless: \"$BIN\" --selftest"
echo "  Rebuild:  ./build.sh --macos           (add --run to launch, --selftest to verify)"
