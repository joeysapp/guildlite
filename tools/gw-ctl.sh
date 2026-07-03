#!/usr/bin/env bash
# gw-ctl.sh -- send verbs to the live Guildlite core's control-file command QUEUE.
#
# The injected stub (guildlite-stub.dll) polls Documents\guildlite\control at ~2 Hz,
# reads-then-deletes it, and runs each line in order. It's a simple command queue with
# dual-threaded execution: the stub's poll thread dequeues verbs, the game's render thread
# (inside the EndScene hook) actually performs a capture/screenshot on a later frame. So
# commands are ENQUEUED, not synchronous -- a `capture` returns immediately here and the
# file lands a few frames later. The remote shell is cmd.exe, so we deliver the file by
# scp'ing straight to the watched path (POSIX printf/mv don't work remotely).
#
# Each ARG becomes one control line, so multi-word verbs are a single quoted arg:
#   tools/gw-ctl.sh capture
#   tools/gw-ctl.sh 'profile clean-solo' capture
#   tools/gw-ctl.sh reload 'set drop_effects true' 'set filter_max_extent 120' capture
#
# Verbs (forwarded to the core -- see stub_main.cpp + Exporter::HandleCommand):
#   reload | unload          stub: hot-swap the freshly-built core / unload the stub
#   capture | capture-dry     arm a model export (dry = diagnostics only, writes no files)
#   screenshot                backbuffer -> Documents\guildlite\guildlite-shot.png
#   profile <name>            clean-solo | clean-self | clean-target | raw
#   set <key> <value>         e.g. set drop_effects true ; set filter_max_extent 120
#   target <player|target>    which agent seeds the capture
#   demo                      toggle the ImGui demo window
#
# Host: $GW_HOST (default guildlite-win, the keepalive'd ssh alias; or bob@bobmobile.local).
# To read results back, use tools/gw-loop.sh (full loop) or scp the newest output/ manifest.
set -euo pipefail

HOST="${GW_HOST:-${GUILDLITE_REMOTE:-guildlite-win}}"
CONTROL="Documents/guildlite/control"

[ $# -ge 1 ] || { echo "usage: gw-ctl.sh <verb> [verb...]   (each arg = one control line)" >&2; exit 2; }

cf="$(mktemp)"
for v in "$@"; do printf '%s\n' "$v" >> "$cf"; done
printf '[gw-ctl] queue -> %s:%s\n' "$HOST" "$CONTROL" >&2
sed 's/^/    /' "$cf" >&2
scp -q "$cf" "$HOST:$CONTROL"
rm -f "$cf"
echo "[gw-ctl] queued (dequeued by the stub within ~0.5s; capture lands a few frames later)." >&2
