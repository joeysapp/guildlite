#!/usr/bin/env bash
# Seed datcore labels.json with the hand-collected NPC/model labels scattered across
# ROADMAP.md and datcore/README.md, keyed by STABLE file hash (mft: ids were resolved to
# hash: first, since mft changes every dat build). Idempotent: datcli label upserts by id.
#
#   tools/seed_labels.sh [labels.json]     (default: ./labels.json)
#
# Kept separate from the shared labels.json until it is stable across remotes (README note).
set -euo pipefail
cd "$(dirname "$0")/.."
DATCLI="./datcore/bin/datcli"
LABELS="${1:-./labels.json}"
L=(--labels "$LABELS")

# --- ROADMAP.md NPC/model list -------------------------------------------------
"$DATCLI" label hash:153069 "Fire Djinn"        "${L[@]}" --category npc    --tag monster --tag GToB --source roadmap --notes "Floating fire djinn; transmog #285 (giant); Great Temple of Balthazar"
"$DATCLI" label hash:161906 "Xandra"            "${L[@]}" --category npc    --tag hero --tag ritualist --source roadmap --notes "Ritualist hero; NPC #6025-6027 default armor"
"$DATCLI" label hash:350208 "Signpost"          "${L[@]}" --category object --source roadmap --notes "NPC #1197"
"$DATCLI" label hash:350209 "Signpost"          "${L[@]}" --category object --source roadmap --notes "NPC #1198"
"$DATCLI" label hash:350210 "Signpost"          "${L[@]}" --category object --source roadmap --notes "NPC #1199"

# --- datcore/README.md manual labels (mft: resolved to hash:) -------------------
"$DATCLI" label hash:379857 "Spectral Tiger"    "${L[@]}" --category npc    --source readme --notes "with a portal"
"$DATCLI" label hash:283392 "Gwen Doll"         "${L[@]}" --category npc    --source readme --notes "no skin-dye step -> renders blue"
"$DATCLI" label hash:370178 "Ghostly Warrior"   "${L[@]}" --category npc    --source readme --notes "full model (was mft:16509)"
"$DATCLI" label hash:52356  "Underworld Dog?"   "${L[@]}" --category npc    --source readme --notes "(was mft:14657)"
"$DATCLI" label hash:289688 "Cave Entrance"     "${L[@]}" --category object --source readme --notes "icy? area/object (was mft:128101)"
"$DATCLI" label hash:157824 "Atmospheric Bird?" "${L[@]}" --category object --source readme --notes "(was mft:88301)"

echo "seeded 11 labels into $LABELS"
