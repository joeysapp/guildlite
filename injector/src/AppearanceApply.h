#pragma once

#include <cstdint>
#include <vector>

#include "EditorConfig.h"
#include "GuildliteConfig.h" // TargetSource (shared "player vs target" selector)

// AppearanceApply -- the GWCA WRITE half of the Editor, the mirror of GameState's read half.
// GameState snapshots an agent OUT of the game; this pushes edits IN. Every mutation that
// touches game/render memory (agent fields, the NPCEquipment vtable, emulated StoC packets)
// is marshalled onto GW's game thread via GW::GameThread::Enqueue, so these are safe to call
// from the overlay's render thread (the panel) or the stub's poll thread (control verbs).
// No-ops before Game::Ready(). All edits are CLIENT-SIDE and reset when the server recreates
// the agent on zone -- callers re-apply as needed.
//
// Mechanisms (all proven in GWToolbox's TransmoModule / ArmoryWindow):
//   - transmog + scale : emulate AgentModel / AgentScale StoC packets (model_id 0 / 100% reset)
//   - equipment + dye  : spoof equip->items[slot] then call the EquipItem/RemoveItem vtable
//   - professions / sex: direct AgentLiving field writes (experimental: not every model re-skins)
namespace Guildlite {
namespace AppearanceApply {

    // Apply one appearance to the resolved Source (player or current target). Captures the
    // agent's pre-edit state the first time it edits it, so Revert restores the real look.
    void ApplyToSource(TargetSource source, const Appearance& ap);
    void RevertSource(TargetSource source);

    // Apply/revert by explicit agent id (used by global states for all-players / all-npcs).
    void ApplyToAgent(uint32_t agent_id, const Appearance& ap);
    void RevertAgent(uint32_t agent_id);

    // Revert every agent this session edited (used on "Revert all" and, optionally, shutdown).
    void RevertAll();

    // Apply a set of global states: only the enabled ones, low-priority first so the highest
    // priority wins a contested field, each onto its own target set. One-shot over the agents
    // present now (new spawns are not auto-styled -- that is future work).
    void ApplyStates(const std::vector<GlobalState>& states);

    // Best-effort read of the Source's current appearance into `out` (for the panel's live
    // readout + "Read from source"). Pure reads, fine from the render thread. Returns false if
    // no such agent. Scale can't be read back from GW, so out.scale_* is left at its default.
    bool ReadSource(TargetSource source, Appearance& out);

    // How many agents currently carry edits we could revert (for the panel status line).
    int EditedCount();

    // One row of GW's client-global NPC table -- the transmog picker source. `id` is the npc id
    // to transmog into (the array index); model/profession are shown so a row is recognisable
    // without decoding the (encoded) name. Names are left for a follow-up (async UI decode).
    struct NpcRow {
        uint32_t id = 0;
        uint32_t model_file_id = 0;
        int primary = 0;
    };
    // Snapshot up to max_rows non-empty NPC definitions into out (cleared first). Reads the GW
    // NPC array, so call it from the render thread (the panel), on demand -- not every frame.
    // Returns the total number of non-empty NPCs seen (may exceed out.size() if capped).
    size_t NpcListSnapshot(std::vector<NpcRow>& out, uint32_t max_rows);

    // One row of GW's client-global composite-model table: model_file_id -> the 11 DAT
    // file-ids that make up an armor/item (sub-models + textures). This is the RUNTIME half
    // of the armor item->texture bridge; the static half is datcore/data/armors.tsv, joined
    // offline on model_file_id. The array index is taken as the model_file_id (that's the key
    // GetCompositeModelInfo() looks up by). Dyeable-armor diffuse textures live here, not in
    // the model's own FFNA texture refs -- see datcore/README.md.
    struct CompositeRow {
        uint32_t model_file_id = 0;
        uint32_t class_flags = 0;
        uint32_t file_ids[11] = {0};
    };
    // Snapshot every non-empty composite into out (cleared first). Reads the GW composite
    // array, so call from the render thread on demand. Returns total non-empty seen.
    size_t CompositeSnapshot(std::vector<CompositeRow>& out, uint32_t max_rows);

} // namespace AppearanceApply
} // namespace Guildlite
