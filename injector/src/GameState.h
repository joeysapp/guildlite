#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Capture.h"
#include "GuildliteConfig.h"

// ==============================================================================
// GameState -- the GWCA half of the Advanced snapshot. GWCA exposes model
// IDENTITY (professions, per-slot equipment model_file_ids + dyes, animation
// state ids) but NO mesh/skeleton geometry, so this never touches vertices; it
// records who/what was captured into a JSON manifest beside the model, and
// suggests a filename. Animation is only opaque state ids in GWCA, so the
// manifest is honest that a live grab is the current pose, not skeletal data.
// ==============================================================================

namespace Guildlite {

    struct EquipSlot {
        std::string slot;
        uint32_t model_file_id = 0;
        int item_type = -1;
        uint32_t dye = 0; // raw DyeInfo bytes
    };

    // Plain, glaze-serialisable description of the captured agent.
    struct GameStateSnapshot {
        bool valid = false;
        std::string source;            // "player" | "target"
        uint32_t agent_id = 0;
        uint32_t player_number = 0;
        uint32_t agent_model_type = 0; // 0x3000 player, 0x2000 npc
        uint32_t transmog_npc_id = 0;
        int primary = 0;
        int secondary = 0;
        std::string primary_name;
        std::string secondary_name;
        int level = 0;
        bool is_npc = false;
        bool is_female = false;
        int weapon_type = 0;
        int weapon_item_type = 0;
        int offhand_item_type = 0;
        int weapon_item_id = 0;
        int offhand_item_id = 0;
        uint32_t model_state = 0;
        uint32_t animation_id = 0;
        uint32_t animation_code = 0;
        float animation_type = 0.f;
        float animation_speed = 0.f;
        float pos_x = 0.f, pos_y = 0.f, pos_z = 0.f, rotation = 0.f;
        float box_width = 0.f, box_height = 0.f;
        int map_id = 0;
        int instance_type = -1;
        std::string instance_name;
        std::vector<EquipSlot> equipment;
    };

    namespace GameState {
        GameStateSnapshot Gather(TargetSource source);
        std::wstring SuggestStem(const GameStateSnapshot& snap);
        std::string BuildManifest(const GameStateSnapshot& snap, const std::vector<MeshChunk>& chunks,
                                  const Config& cfg, const CaptureStats& stats, const std::string& timestamp,
                                  const std::vector<ProbeSample>& probes,
                                  const std::vector<DrawLogEntry>& draw_log);
    }

} // namespace Guildlite
