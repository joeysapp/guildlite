#!/usr/bin/env bash
# gw-loop.sh -- one-command Guildlite dev loop over SSH (the "Dev Loop control layer"
# from BUILD.md). Does: [reload] -> profile/set/target -> screenshot + capture -> poll
# for the new manifest -> fetch export + in-game shot -> Blender render -> summarize + log.
#
# The Windows remote shell is cmd.exe, so the control file is written by scp'ing straight
# to the watched path (POSIX printf/mv do NOT work remotely). Requires the STUB injected
# (guildlite-inject guildlite-stub.dll), not the monolith.
#
# Usage:
#   tools/gw-loop.sh [--reload] [--profile NAME] [--target player|target]
#                    [--set 'key value']... [--dest DIR] [--no-render]
# Examples:
#   tools/gw-loop.sh --reload --profile clean-self
#   tools/gw-loop.sh --profile clean-target --set 'filter_max_extent 120'
set -uo pipefail

HOST="${GW_HOST:-guildlite-win}"
REMOTE_OUT_WIN='Documents\guildlite\output'
REMOTE_OUT_POSIX='Documents/guildlite/output'
BLENDER="${BLENDER:-/opt/homebrew/bin/blender}"
RENDER_PY="$(cd "$(dirname "$0")" && pwd)/blender_render.py"
DEST="${GW_DEST:-$(pwd)/gl-loop}"
DO_RELOAD=0; PROFILE=""; TARGET=""; RENDER=1
SETS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --reload)    DO_RELOAD=1; shift;;
    --profile)   PROFILE="$2"; shift 2;;
    --target)    TARGET="$2"; shift 2;;
    --set)       SETS+=("$2"); shift 2;;
    --dest)      DEST="$2"; shift 2;;
    --no-render) RENDER=0; shift;;
    *) echo "gw-loop: unknown arg '$1'" >&2; exit 2;;
  esac
done
mkdir -p "$DEST"

rnewest() { ssh "$HOST" "cmd /c dir /O-D /B \"$REMOTE_OUT_WIN\\*.json\"" 2>/dev/null | tr -d '\r' | head -1; }

before="$(rnewest)"

# --- build + deliver the control file (one atomic scp to the watched path) ---
cf="$(mktemp)"; : > "$cf"
[ "$DO_RELOAD" = 1 ] && echo reload            >> "$cf"
[ -n "$PROFILE" ]    && echo "profile $PROFILE" >> "$cf"
[ -n "$TARGET" ]     && echo "target $TARGET"   >> "$cf"
for s in "${SETS[@]:-}"; do [ -n "$s" ] && echo "set $s" >> "$cf"; done
echo screenshot >> "$cf"
echo capture    >> "$cf"
echo "[gw-loop] control ->"; sed 's/^/    /' "$cf"
scp -q "$cf" "$HOST:Documents/guildlite/control"; rm -f "$cf"

# --- poll for a fresh manifest (capture is silently ignored while loading/in-flight) ---
printf '[gw-loop] waiting for capture'
newest=""
for _ in $(seq 1 40); do
  sleep 1; printf .
  n="$(rnewest)"
  if [ -n "$n" ] && [ "$n" != "$before" ]; then newest="$n"; break; fi
done
printf '\n'
[ -z "$newest" ] && { echo "[gw-loop] TIMEOUT: no new manifest -- stub injected? game rendering? not on a loading screen?"; exit 1; }
stem="${newest%.json}"
echo "[gw-loop] captured: $stem"

# --- fetch the whole export set (obj/mtl/json/_texN.tga) + the in-game screenshot ---
scp -q "$HOST:$REMOTE_OUT_POSIX/$stem*" "$DEST/" 2>/dev/null || true
scp -q "$HOST:Documents/guildlite/guildlite-shot.png" "$DEST/$stem.ingame.png" 2>/dev/null \
  && echo "[gw-loop] in-game: $DEST/$stem.ingame.png" || echo "[gw-loop] (no in-game screenshot)"

# --- render on the mac ---
if [ "$RENDER" = 1 ] && [ -x "$BLENDER" ] && [ -f "$DEST/$stem.obj" ]; then
  if "$BLENDER" --background --python "$RENDER_PY" -- "$DEST/$stem.obj" "$DEST/$stem.render.png" >/dev/null 2>&1; then
    echo "[gw-loop] render:  $DEST/$stem.render.png"
  else
    echo "[gw-loop] render FAILED (see: $BLENDER --background --python $RENDER_PY -- $DEST/$stem.obj out.png)"
  fi
fi

# --- summarize + append to the run log ---
python3 - "$DEST/$stem.json" "$DEST" "profile=$PROFILE target=$TARGET ${SETS[*]:-}" <<'PY'
import json, sys, os
d = json.load(open(sys.argv[1])); dest = sys.argv[2]; cfg = sys.argv[3]
st = d.get('settings', {})
line = ("captured=%s effect=%s iso=%s filtered=%s 2d=%s trimmed=%s chunks=%s | "
        "drop_effects=%s req_skinned=%s max_ext=%s | %s") % (
    d.get('draws_captured'), d.get('draws_skipped_effect'), d.get('draws_skipped_isolation'),
    d.get('draws_skipped_filtered'), d.get('draws_2d_skipped'), d.get('draws_trimmed'),
    len(d.get('chunks', [])),
    st.get('drop_effects'), st.get('require_skinned'), st.get('filter_max_extent'), cfg.strip())
print("[gw-loop] " + line)
with open(os.path.join(dest, "analysis.log"), "a") as f:
    f.write("%s  %s\n" % (d.get('timestamp', ''), line))
PY
echo "[gw-loop] done -> $DEST"
