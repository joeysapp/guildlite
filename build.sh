#!/usr/bin/env bash
# ------------------------------------------------------------------------------
# build.sh -- Guildlite plugin: macOS -> Windows -> macOS build pipeline.
# ------------------------------------------------------------------------------
# Write on macOS -> package the working tree -> build headlessly on Windows
# (no Visual Studio GUI) -> VERIFY the DLL is real & fresh before accepting it
# -> deploy into the GWToolbox plugin dir -> fetch the DLL + log back for audit.
# ------------------------------------------------------------------------------
# The long CMake/MSBuild build runs DETACHED on Windows (a Scheduled Task) and
# writes an atomic exit-code marker; this script POLLS with short, keepalive'd ssh
# connections. A dropped sshd kills at most one poll, never the build -- the fix for
# "its ssh daemon dropped a good number of those waiting builds".
# -------------------------------------------------------------------------------
# Config precedence: CLI flag > env (windows-env.sh) > built-in default.
# Usage:
#   ./build.sh                 full run (package, build, verify, deploy, fetch)
#   ./build.sh --macos [--run] build the LOCAL macOS SDL2+Metal control GUI (gui/ tree) --
#                              no Windows box / SSH pipeline needed; --run launches it,
#                              --selftest verifies it headless. See gui/README.md
#   ./build.sh --guildlite     build + install the STANDALONE injector (injector/ tree) into
#                              Documents/guildlite/: guildlite.dll (Phase-1 monolith),
#                              guildlite-inject.exe (loader), guildlite-core.dll +
#                              guildlite-stub.dll (Phase-2 dev loop), and gwca.dll
#   ./build.sh -n              dry-run: print every ssh/scp/tar/powershell command
#   ./build.sh --debug         build Debug instead of RelWithDebInfo
#   ./build.sh --clean         wipe the remote CMake cache first (keeps vcpkg_installed)
#   ./build.sh --no-deploy     build + verify + fetch, but don't drop into the plugin dir
#   ./build.sh --launcher      build + install our OWN GWToolbox.exe + GWToolboxdll.dll
#                              (updater neutered) into Documents/GWToolboxpp/ -- not the plugin
#   ./build.sh --doctor        just run the toolchain/reachability audit and exit
#   ./build.sh --attach <id>   re-attach to an in-flight build's poll loop
#   ./build.sh -H <user@host> -d <reldir> -p <reldir> -c <cfg>   overrides
# ------------------------------------------------------------------------------

set -Eeuo pipefail
IFS=$'\n\t'

# --- locate self + repo root --------------------------------------------------
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" && pwd -P)"
export GUILDLITE_SRC="$SCRIPT_DIR"

# --- native-on-Windows shortcut ----------------------------------------------
# `bash ./build.sh` under Git-Bash/MSYS on the Windows box itself: skip the network
# entirely and hand off to the PowerShell worker to build in place.
case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*)
        exec powershell -NoProfile -ExecutionPolicy Bypass \
            -File "$SCRIPT_DIR/tools/build_remote.ps1" -Mode Worker \
            -RunId "local-$(date +%Y%m%d-%H%M%S)" \
            -Config "${GUILDLITE_CONFIG:-RelWithDebInfo}" \
            -SrcDir "$SCRIPT_DIR" -PluginDir "${GW_TOOLBOX_PLUGIN_DIR:-Documents/GWToolboxpp/plugins}"
        ;;
esac

# --- native-on-macOS GUI shortcut --------------------------------------------
# `./build.sh --macos` builds the local SDL2+Metal ImGui control console (gui/ tree) with
# ONLY local tools -- no Windows box, no SSH pipeline, no vcpkg. Handled here, BEFORE we
# source the windows-* helpers, so it works on a Mac with none of that cross-build setup.
for _a in "$@"; do
    case "$_a" in
        --macos|--gui) exec "$SCRIPT_DIR/tools/build_macos_gui.sh" "$@" ;;
    esac
done

# --- native-on-macOS datcore shortcut ----------------------------------------
# `./build.sh --datcore --local` builds the datcore CLI (datcli) locally with cmake +
# Apple clang -- no Windows box. Geometry/catalog/labels work; textures need the remote
# (Windows) build because of the x86 ATEX asm. Plain `--datcore` still builds on the box.
_dc=0 _loc=0
for _a in "$@"; do case "$_a" in --datcore) _dc=1 ;; --local|--native) _loc=1 ;; esac; done
[ "$_dc" = 1 ] && [ "$_loc" = 1 ] && exec "$SCRIPT_DIR/tools/build_datcore_local.sh"

# --- source env + helpers (auto-sourced interactively; explicit here for bash) --
[ -f "$HOME/etc/term/windows-env.sh" ] && . "$HOME/etc/term/windows-env.sh"
if ! command -v win-ssh >/dev/null 2>&1; then
    if [ -f "$HOME/etc/term/windows-utils.sh" ]; then
        . "$HOME/etc/term/windows-utils.sh"
    else
        echo "[build.sh] FATAL: cannot find ~/etc/term/windows-utils.sh" >&2
        exit 1
    fi
fi

# build.sh wants fatal exits; win_die (from utils) only returns. Override locally.
die() { win_err "$@"; exit 1; }
trap 'rc=$?; die "failed at line $LINENO (rc=$rc)"' ERR

# --- config: start from env/defaults, then overlay CLI ------------------------
HOST="${GUILDLITE_REMOTE:-${WINDOWS_HOST:-guildlite-win}}"
REMOTE_DIR="${GUILDLITE_REMOTE_DIR:-src/guildlite}"
PLUGIN_DIR="${GW_TOOLBOX_PLUGIN_DIR:-Documents/GWToolboxpp/plugins}"
CONFIG="${GUILDLITE_CONFIG:-RelWithDebInfo}"
KIND="plugin"; INSTALL_DIR="${GW_TOOLBOX_INSTALL_DIR:-}"
VERBOSE=""; DRY_RUN=""; CLEAN=""; NO_DEPLOY=""; DOCTOR_ONLY=""
ATTACH=""; TIMEOUT="3600"
export VERBOSE DRY_RUN   # read by the win-* helpers

while [ $# -gt 0 ]; do
    case "$1" in
        -H|--host)       HOST="$2";       shift 2 ;;
        -d|--dir)        REMOTE_DIR="$2"; shift 2 ;;
        -p|--plugin-dir) PLUGIN_DIR="$2"; shift 2 ;;
        -c|--config)     CONFIG="$2";     shift 2 ;;
        --debug)         CONFIG="Debug";           shift ;;
        --release)       CONFIG="Release";         shift ;;
        --clean)         CLEAN="1";                shift ;;
        --no-deploy)     NO_DEPLOY="1";            shift ;;
        --launcher|--exe) KIND="launcher";         shift ;;
        --guildlite)     KIND="guildlite";         shift ;;
        --datcore)       KIND="datcore";           shift ;;
        --install-dir)   INSTALL_DIR="$2"; shift 2 ;;
        --doctor)        DOCTOR_ONLY="1";          shift ;;
        --attach)        ATTACH="$2";     shift 2 ;;
        --timeout)       TIMEOUT="$2";    shift 2 ;;
        -v|--verbose)    VERBOSE="1";              shift ;;
        -n|--dry-run)    DRY_RUN="1";              shift ;;
        -h|--help)       sed -n '2,32p' "$0"; exit 0 ;;
        *)               die "unknown option: $1 (try --help)" ;;
    esac
done
export VERBOSE DRY_RUN
# Keep the helpers' host resolution in sync with our resolved HOST.
export GUILDLITE_REMOTE="$HOST" GUILDLITE_REMOTE_DIR="$REMOTE_DIR" \
       GW_TOOLBOX_PLUGIN_DIR="$PLUGIN_DIR" GUILDLITE_CONFIG="$CONFIG"

# Fetched DLL/pdb/log land here (audit copies). Kept OUT of the repo tree by default
# so builds don't leave untracked files; override with $GUILDLITE_ARTIFACTS.
ARTIFACTS="${GUILDLITE_ARTIFACTS:-${TMPDIR:-/tmp}/guildlite-artifacts}"

# --- stages -------------------------------------------------------------------
preflight() {
    win_log "preflight: host=$HOST dir=$REMOTE_DIR config=$CONFIG plugin=$PLUGIN_DIR"
    [ -n "$DRY_RUN" ] && { win_warn "dry-run: skipping live doctor"; return 0; }
    win-doctor "$HOST" || die "preflight audit failed -- fix the checklist above, or use --dry-run"
}

launch() {
    win-push-src "$HOST" || die "failed to push source"
    _win_stage_launcher "$HOST" || die "failed to stage worker"
    local clean_flag="" nodeploy_flag=""
    [ -n "$CLEAN" ]     && clean_flag="-Clean"
    [ -n "$NO_DEPLOY" ] && nodeploy_flag="-NoDeploy"
    win_log "launching detached build (run-id $RUNID, kind=$KIND) ..."
    win-ssh "$HOST" "powershell -NoProfile -ExecutionPolicy Bypass -File build_remote.ps1 \
-Mode Launch -RunId $RUNID -Config $CONFIG -SrcDir $REMOTE_DIR -PluginDir $PLUGIN_DIR \
-Kind $KIND -InstallDir \"$INSTALL_DIR\" -Tarball guildlite-src.tar.gz $clean_flag $nodeploy_flag" \
        || die "failed to launch remote build"
    win_ok "build launched; polling (timeout ${TIMEOUT}s) ..."
}

# poll the atomic exit-code marker until DONE or timeout; tolerate ssh drops.
poll() {
    [ -n "$DRY_RUN" ] && { win_warn "dry-run: skipping poll"; return 0; }
    local elapsed=0 backoff=8 out status lastline
    while [ "$elapsed" -lt "$TIMEOUT" ]; do
        if ! out="$(win-ssh "$HOST" \
                "powershell -NoProfile -ExecutionPolicy Bypass -File build_remote.ps1 -Mode Status -RunId $RUNID" 2>/dev/null)"; then
            win_warn "poll blip at ${elapsed}s (ssh drop?) -- resetting master, retrying"
            win-ssh-reset "$HOST"
            sleep "$backoff"; elapsed=$((elapsed + backoff)); continue
        fi
        out="${out//$'\r'/}"                    # Windows PowerShell emits CRLF -- strip CR
        status="$(printf '%s\n' "$out" | sed -n 's/^STATUS=//p')"
        if [ "$status" = "DONE" ]; then
            STATUS_OUT="$out"; return 0
        fi
        if [ -z "$status" ]; then
            # No STATUS line = empty/garbled read (often a ControlMaster wedged under
            # build load); tear the master down so the next poll gets a fresh channel.
            win_warn "poll got no STATUS at ${elapsed}s -- resetting master, retrying"
            win-ssh-reset "$HOST"
            sleep "$backoff"; elapsed=$((elapsed + backoff)); continue
        fi
        lastline="$(printf '%s\n' "$out" | sed -n 's/^LASTLINE=//p')"
        win_log "building... ${elapsed}s${lastline:+ | $lastline}"
        sleep "$backoff"; elapsed=$((elapsed + backoff))
        [ "$backoff" -lt 30 ] && backoff=$((backoff + 4))
    done
    die "build did not finish within ${TIMEOUT}s (re-attach later with: $0 --attach $RUNID)"
}

# gate acceptance on exit-code + remote verify, then fetch + re-hash locally.
verify_and_fetch() {
    [ -n "$DRY_RUN" ] && { win_warn "dry-run: skipping verify/fetch"; return 0; }
    local exitcode verify sha remote_dll deployed kind files
    exitcode="$(printf '%s\n' "$STATUS_OUT" | sed -n 's/^EXITCODE=//p')"
    verify="$(  printf '%s\n' "$STATUS_OUT" | sed -n 's/^VERIFY=//p')"
    sha="$(     printf '%s\n' "$STATUS_OUT" | sed -n 's/^SHA256=//p')"
    remote_dll="$(printf '%s\n' "$STATUS_OUT" | sed -n 's/^DLL=//p')"
    deployed="$(printf '%s\n' "$STATUS_OUT" | sed -n 's/^DEPLOYED=//p')"
    kind="$(    printf '%s\n' "$STATUS_OUT" | sed -n 's/^KIND=//p')"
    files="$(   printf '%s\n' "$STATUS_OUT" | sed -n 's/^FILE=//p')"

    local dst="$ARTIFACTS/$RUNID"
    win-fetch "$HOST" ".guildlite/$RUNID/build.log" "$dst/build.log" 2>/dev/null || true

    if [ "${exitcode:-1}" != "0" ]; then
        win_err "BUILD FAILED (exit $exitcode). Last lines of build.log:"
        [ -f "$dst/build.log" ] && tail -n 40 "$dst/build.log" >&2
        die "remote build reported failure"
    fi
    if [ "$verify" != "PASS" ]; then
        die "artifact verification failed on Windows: ${verify:-<none>}"
    fi

    # Pull each built artifact (+ pdb) back and confirm transfer integrity against the
    # remote per-file hash. The worker emits one `FILE=<name> <sha256> <deploydir>` line
    # per artifact; fall back to the single-DLL layout for pre-manifest / --attach runs.
    [ -n "$files" ] || files="Guildlite.dll $sha $PLUGIN_DIR"
    local any=0 name fsha fdir pdb local_sha
    while IFS=' ' read -r name fsha fdir; do
        [ -n "$name" ] || continue
        any=1
        win-fetch "$HOST" ".guildlite/$RUNID/$name" "$dst/$name" || die "failed to fetch $name"
        pdb="${name%.*}.pdb"
        win-fetch "$HOST" ".guildlite/$RUNID/$pdb" "$dst/$pdb" 2>/dev/null || true
        if [ -n "$fsha" ]; then
            local_sha="$(shasum -a 256 "$dst/$name" | cut -d' ' -f1)"
            [ "$local_sha" = "$fsha" ] || die "sha256 mismatch after fetch of $name (remote $fsha, local $local_sha)"
            win_ok "sha256 verified: $name"
        else
            win_ok "fetched: $name (no remote hash)"
        fi
        if [ -n "$NO_DEPLOY" ]; then
            win_warn "--no-deploy: $name not installed"
        elif [ "$deployed" = "1" ]; then
            win_ok "installed: $name -> $fdir"
        else
            win_warn "$name NOT deployed (worker reported DEPLOYED=$deployed) -- check build.log"
        fi
    done <<EOF
$files
EOF
    [ "$any" = "1" ] || die "no artifacts in manifest to fetch"
    win_ok "artifacts in $dst/"
    [ -n "$remote_dll" ] && win_ok "remote source: $remote_dll"
    if [ -z "$NO_DEPLOY" ] && [ "$deployed" != "1" ]; then
        die "deploy FAILED on $HOST (worker reported DEPLOYED=$deployed) -- target likely locked/loaded (close GW or whatever holds it), then rebuild. See build.log"
    fi
    if [ "$kind" = "launcher" ] || [ "$kind" = "guildlite" ] || [ "$deployed" = "1" ]; then
        win_warn "restart GWToolbox / Guild Wars to load the new binaries (or re-inject, for guildlite)"
    fi
    return 0
}

# --- main ---------------------------------------------------------------------
if [ -n "$DOCTOR_ONLY" ]; then
    win-doctor "$HOST"; exit $?
fi

if [ -n "$ATTACH" ]; then
    RUNID="$ATTACH"
    win_log "re-attaching to run $RUNID on $HOST"
    poll
    verify_and_fetch
    win_ok "done (attached run $RUNID)"
    exit 0
fi

RUNID="$(date +%Y%m%d-%H%M%S)"
preflight
launch
poll
verify_and_fetch
win_ok "build complete: run $RUNID -> $ARTIFACTS/$RUNID/"
