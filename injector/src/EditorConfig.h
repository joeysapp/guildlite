#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ==============================================================================
// EditorConfig -- the data model for the Model Editor (ROADMAP "Model Editor").
// The Editor's analog of GuildliteConfig: a plain, glaze-serialisable description
// of the appearance edits the user configures, the named looks they save, and the
// higher-level global states they toggle. Kept to scalar leaf fields + structs in
// vectors so it persists through glaze exactly like the exporter's manifest/config.
//
// Every axis carries a "use_*" gate: an edit only touches what it declares, so
// loading a "black gloves" look never silently resets your profession or transmog,
// and stacking two states composes instead of clobbering.
// ==============================================================================

namespace Guildlite {

    // One equipment slot's edit. Maps 1:1 to NPCEquipment.items[slot]. Slot order
    // (GWCA): 0 weapon, 1 offhand, 2 chest, 3 legs, 4 head, 5 feet, 6 hands,
    // 7 costume_body, 8 costume_head. In memory the Editor keeps all 9 (index==slot);
    // `enabled` decides whether a slot participates when the appearance is applied.
    struct SlotEdit {
        int slot = 0;                 // 0..8, redundant with the index but self-describing on disk
        bool enabled = false;         // apply this slot at all
        bool set_model = false;       // overwrite items[slot].model_file_id
        uint32_t model_file_id = 0;
        bool set_dye = false;         // overwrite the dye channels + tint
        int dye0 = 0;                 // DyeColor 0..13 per channel (0 = None)
        int dye1 = 0;
        int dye2 = 0;
        int dye3 = 0;
        int dye_tint = 0;             // DyeInfo.dye_tint byte
    };

    // The full set of appearance edits for one agent.
    struct Appearance {
        // Transmog: replace the whole rendered model with an NPC's, via an emulated AgentModel
        // StoC packet (model_id = npc id). use_transmog with transmog_npc_id 0 clears it.
        bool use_transmog = false;
        uint32_t transmog_npc_id = 0;

        // Scale: rescale the agent via an emulated AgentScale packet. Percent of normal size
        // (100 = normal); GW packs it as (percent & 0xFF) << 24. Reverts to 100.
        bool use_scale = false;
        int scale_percent = 100;      // 1..255

        // Profession bytes (drive body build / animations for some model choices).
        bool use_professions = false;
        int primary = 0;              // ProfessionByte 0..10 (None,W,R,Mo,N,Me,E,A,Rt,P,D)
        int secondary = 0;

        // Sex bit (type_map 0x200). Experimental: not every model re-skins on a live flip.
        bool use_sex = false;
        bool female = false;

        // Per-slot equipment models + dyes. Always length 9 in memory (index==slot).
        bool use_equipment = false;
        std::vector<SlotEdit> slots;
    };

    // A saved, named look the user can recall onto the Source with one click.
    struct CharacterConfig {
        std::string name;
        std::string note;
        Appearance appearance;
    };

    // Which agents a global state hits when it's activated.
    enum class StateTarget : int {
        Self = 0,        // the controlled character
        Target = 1,      // the current selection
        AllPlayers = 2,  // every living player in the instance (one-shot over those present)
        AllNpcs = 3,     // every living NPC in the instance (one-shot over those present)
    };

    // A higher-level, toggleable state: one appearance recipe bound to a target set,
    // with a priority so overlapping states resolve deterministically -- states are
    // applied low priority first, so the highest-priority state wins a contested field.
    // Several can be enabled at once and applied together ("Apply enabled states").
    struct GlobalState {
        std::string name;
        bool enabled = false;
        int priority = 0;
        int target = 0;               // StateTarget as int
        Appearance appearance;
    };

    // The persisted Editor document (Documents\guildlite\editor.json).
    struct EditorSave {
        std::vector<CharacterConfig> characters;
        std::vector<GlobalState> states;
    };

} // namespace Guildlite
