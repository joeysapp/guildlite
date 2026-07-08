#!/usr/bin/env bash
# gw-confirm.sh -- the READBACK half of the control loop (companion to gw-ctl.sh).
#
# The stub's control file is a write-only command queue; this closes the loop the other
# way. The core's `status` verb writes Documents\guildlite\status.json (player/target/pick/
# last-capture); this fires optional verbs (with a trailing `status`), fetches that file,
# and prints it -- so an SSH/agent loop can CONFIRM the result of target/select/pick/capture
# with no view. It also WIRES THE DAT BRIDGE: each target equipment model_file_id is joined
# to the local datcore catalog+labels (datcli show), turning "who am I looking at" into
# "which DAT models + labels is this", ready for `datcli label`.
#
#   tools/gw-confirm.sh                      # fetch + print current status.json
#   tools/gw-confirm.sh 'select nearest'     # queue verb(s), append status, fetch, print
#   tools/gw-confirm.sh 'target target' 'select 1234'
#
# Env: GW_HOST (default guildlite-win), GW_WAIT (post-send settle secs, default 2),
#      GUILDLITE_DATA (dir with catalog.tsv/labels.json; default repo root) for the bridge.
set -euo pipefail
cd "$(dirname "$0")/.."
HOST="${GW_HOST:-${GUILDLITE_REMOTE:-guildlite-win}}"
DIR="Documents/guildlite"
DATCLI="./datcore/bin/datcli"
export GUILDLITE_DATA="${GUILDLITE_DATA:-$PWD}"

# 1) Queue verbs (each arg = one line), always ending with `status` so the fetched file is
#    fresh -- SelectInGame also self-refreshes status.json on the game thread post-change.
if [ "$#" -gt 0 ]; then
  { for v in "$@"; do printf '%s\n' "$v"; done; printf 'status\n'; } > /tmp/gl-confirm-ctl
  scp -q /tmp/gl-confirm-ctl "$HOST:$DIR/control"
  sleep "${GW_WAIT:-2}"   # 2 Hz poll dequeue + game-thread apply + status.json write
fi

# 2) Fetch status.json
tmp="$(mktemp)"
if ! scp -q "$HOST:$DIR/status.json" "$tmp" 2>/dev/null; then
  echo "gw-confirm: no status.json on $HOST -- send a 'status' verb first, or the core isn't loaded"
  rm -f "$tmp"; exit 1
fi

# 3) Print the confirm summary; emit target `slot=model_file_id` pairs for the bridge.
ids="$(python3 - "$tmp" <<'PY'
import json,sys
d=json.load(open(sys.argv[1]))
def line(name):
    s=d.get(name,{})
    if not s.get('valid'): print(f"  {name:6}: (none)"); return
    eq=" ".join(f"{e['slot']}={e['model_file_id']}" for e in s.get('equipment',[]) if e.get('model_file_id'))
    kind='NPC' if s.get('is_npc') else 'player'
    print(f"  {name:6}: id={s['agent_id']} {kind} {s.get('primary','?')}/{s.get('secondary','?')} "
          f"lvl{s.get('level',0)} npc_id={s.get('transmog_npc_id',0)} pos=({s['pos'][0]:.0f},{s['pos'][1]:.0f})")
    if eq: print(f"          equip: {eq}")
p=d.get('pick',{})
print(f"seq={d.get('seq')} ready={d.get('ready')} map={d.get('map_id')} "
      f"source={d.get('export_source')} has_target={d.get('has_target')}")
line('player'); line('target')
sel=p.get('selected')
print(f"  pick  : active={p.get('active')} list={p.get('list')} marked={p.get('marked')} "
      f"cursor={p.get('cursor')} selected={sel}")
print(f"  last  : {d.get('last_status')!r} -> {d.get('last_output')!r}")
t=d.get('target',{})
pairs=[f"{e['slot']}={e['model_file_id']}" for e in t.get('equipment',[]) if e.get('model_file_id')] if t.get('valid') else []
print("IDS:"+" ".join(pairs))
PY
)"
echo "$ids" | sed '/^IDS:/d'

# 4) DAT bridge: target equipment model_file_id -> composites.tsv file_ids -> datcore label.
#    (The low model_file_id is a COMPOSITE id, not a catalog hash; composites.tsv -- dumped
#    in-game by `edit composites` -- maps it to the real sub-model hashes that carry labels.)
bridge_pairs="$(echo "$ids" | sed -n 's/^IDS://p')"
COMP="$GUILDLITE_DATA/composites.tsv"
if [ -n "${bridge_pairs// }" ] && [ -x "$DATCLI" ] && [ -f "$COMP" ]; then
  echo "  DAT bridge (target equipment -> datcore armor names):"
  for pair in $bridge_pairs; do
    slot="${pair%%=*}"; id="${pair##*=}"
    row="$(awk -F'\t' -v k="$id" '$1==k{print; exit}' "$COMP")"
    if [ -z "$row" ]; then
      printf '    %-6s model %-6s  (not in composites.tsv -- run `edit composites` in-game, scp it back)\n' "$slot" "$id"; continue
    fi
    name=""
    for h in $(printf '%s' "$row" | cut -f2- | tr '\t' ' '); do
      [ "$h" = "0" ] && continue
      lbl="$("$DATCLI" show "hash:$h" --labels 2>/dev/null | sed -n 's/.*label: "\([^"]*\)".*/\1/p' || true)"
      [ -n "$lbl" ] && { name="$lbl (hash:$h)"; break; }
    done
    printf '    %-6s model %-6s -> %s\n' "$slot" "$id" "${name:-(no armor label found)}"
  done
fi
rm -f "$tmp"
