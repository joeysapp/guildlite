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
#   ./build.sh -n              dry-run: print every ssh/scp/tar/powershell command
#   ./build.sh --debug         build Debug instead of RelWithDebInfo
#   ./build.sh --clean         wipe the remote CMake cache first (keeps vcpkg_installed)
#   ./build.sh --no-deploy     build + verify + fetch, but don't drop into the plugin dir
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
        --doctor)        DOCTOR_ONLY="1";          shift ;;
        --attach)        ATTACH="$2";     shift 2 ;;
        --timeout)       TIMEOUT="$2";    shift 2 ;;
        -v|--verbose)    VERBOSE="1";              shift ;;
        -n|--dry-run)    DRY_RUN="1";              shift ;;
        -h|--help)       sed -n '2,25p' "$0"; exit 0 ;;
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
    win_log "launching detached build (run-id $RUNID) ..."
    win-ssh "$HOST" "powershell -NoProfile -ExecutionPolicy Bypass -File build_remote.ps1 \
-Mode Launch -RunId $RUNID -Config $CONFIG -SrcDir $REMOTE_DIR -PluginDir $PLUGIN_DIR \
-Tarball guildlite-src.tar.gz $clean_flag $nodeploy_flag" \
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
    local exitcode verify sha remote_dll deployed
    exitcode="$(printf '%s\n' "$STATUS_OUT" | sed -n 's/^EXITCODE=//p')"
    verify="$(  printf '%s\n' "$STATUS_OUT" | sed -n 's/^VERIFY=//p')"
    sha="$(     printf '%s\n' "$STATUS_OUT" | sed -n 's/^SHA256=//p')"
    remote_dll="$(printf '%s\n' "$STATUS_OUT" | sed -n 's/^DLL=//p')"
    deployed="$(printf '%s\n' "$STATUS_OUT" | sed -n 's/^DEPLOYED=//p')"

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

    # Pull the DLL (+ pdb) back and confirm transfer integrity against the remote hash.
    win-fetch "$HOST" ".guildlite/$RUNID/Guildlite.dll" "$dst/Guildlite.dll" \
        || die "failed to fetch DLL"
    win-fetch "$HOST" ".guildlite/$RUNID/Guildlite.pdb" "$dst/Guildlite.pdb" 2>/dev/null || true
    if [ -n "$sha" ]; then
        local local_sha; local_sha="$(shasum -a 256 "$dst/Guildlite.dll" | cut -d' ' -f1)"
        [ "$local_sha" = "$sha" ] || die "sha256 mismatch after fetch (remote $sha, local $local_sha)"
        win_ok "sha256 verified: $sha"
    fi
    win_ok "DLL accepted: $dst/Guildlite.dll"
    win_ok "remote source: $remote_dll"
    if [ -n "$NO_DEPLOY" ]; then
        win_warn "--no-deploy: not installed into the plugin dir"
    elif [ "$deployed" = "1" ]; then
        win_ok "deployed into $PLUGIN_DIR on $HOST (restart GWToolbox to load)"
    else
        win_warn "not deployed (worker reported DEPLOYED=$deployed) -- check build.log"
    fi
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
