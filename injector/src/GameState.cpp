#include "GameState.h"

#include "stl.h" // project prelude: pulls <functional>/<Windows.h> that GWCA managers assume

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Item.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/MapMgr.h>

#include <glaze/glaze.hpp>

#include <cstring>

namespace Guildlite {

// glaze reflection keys on an external-linkage type, so these manifest structs
// live at namespace scope rather than in an anonymous namespace.
struct ManifestChunk {
    uint32_t draw_index = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t stride = 0;
    uint32_t fvf = 0;
    bool has_vertex_shader = false;
    bool is_skinned = false;
    bool alpha_blend = false;
    bool has_uv = false;
    bool has_normal = false;
    std::string texture_file;
    uint32_t texture_format = 0;
    int texture_w = 0;
    int texture_h = 0;
    std::vector<float> aabb_min;
    std::vector<float> aabb_max;
    std::vector<float> center; // AABB center, model-local -- the key signal for locality tuning
};

// The full effective capture options, echoed into the manifest so an export is
// self-describing (the "JSON of settings + output" idea) and filters can be
// re-tuned offline without guessing what produced a given file.
struct ManifestSettings {
    std::string format;
    std::string detail;
    std::string target;
    std::string scope;
    bool export_normals = false;
    bool dedupe = false;
    bool exclude_2d = false;
    bool export_uvs = false;
    bool export_textures = false;
    bool trim_outliers = false;
    float trim_k = 0.f;
    float filter_center_radius = 0.f;
    int up_axis = 0;
    int filter_min_prims = 0;
    int filter_max_prims = 0;
    int filter_min_verts = 0;
    float filter_max_extent = 0.f;
    float filter_min_thickness = 0.f;
    bool require_texture = false;
    bool require_skinned = false;
    bool probe_shader_constants = false;
    bool isolate_by_bone = false;
    float isolate_tolerance = 0.f;
};

// One skinned draw's vertex-shader constant window (see Config::probe_shader_constants).
// note documents the register layout so the dump is readable without the code.
struct ManifestProbe {
    uint32_t draw_index = 0;
    std::vector<float> center; // model-local AABB center of the probed draw
    int reg_count = 0;         // number of 4-float registers in `regs` (c0..c[reg_count-1])
    std::vector<float> regs;   // row-major float4 per register
};

// One triangle-list draw's disposition in the armed frame (Config::log_draws).
struct ManifestDrawLog {
    uint32_t seq = 0;
    uint32_t prims = 0;
    uint32_t verts = 0;
    bool is_skinned = false;
    bool has_texture = false;
    bool z_enabled = false;
    std::vector<float> ext; // model-space AABB size; empty/0 if dropped before ReadChunk
    std::string reason;     // captured|skip_2d|dedup|filtered|iso|unreadable
};

struct Manifest {
    std::string tool = "guildlite";
    std::string version = "0.3.0";
    std::string timestamp;
    std::string format;
    std::string detail;
    std::string scope;
    uint32_t draws_seen = 0;
    uint32_t draws_captured = 0;
    uint32_t draws_skipped_unreadable = 0;
    uint32_t draws_skipped_filtered = 0;
    uint32_t draws_skipped_isolation = 0;
    uint32_t draws_trimmed = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t unique_textures = 0;
    // Increment 0 draw-path census -- see diag_note. Makes visible where the never-
    // captured bare-skin body actually goes (strip/fan, or a sibling *UP entry point).
    uint32_t hook_calls = 0;
    uint32_t draws_2d_skipped = 0;
    uint32_t dip_trianglelist = 0;
    uint32_t dip_trianglestrip = 0;
    uint32_t dip_trianglefan = 0;
    uint32_t dip_other = 0;
    uint32_t dp_calls = 0,    dp_tris = 0;
    uint32_t dpup_calls = 0,  dpup_tris = 0;
    uint32_t dipup_calls = 0, dipup_tris = 0;
    std::string diag_note;
    std::string pose_note;
    std::string probe_note;
    ManifestSettings settings;
    GameStateSnapshot subject;
    std::vector<ManifestChunk> chunks;
    std::vector<ManifestProbe> probe;
    std::string draw_log_note;
    std::vector<ManifestDrawLog> draw_log;
};

namespace {

    const char* ProfName(int p)
    {
        static const char* names[] = {"None", "Warrior", "Ranger", "Monk", "Necromancer", "Mesmer",
                                      "Elementalist", "Assassin", "Ritualist", "Paragon", "Dervish"};
        return (p >= 0 && p < 11) ? names[p] : "Unknown";
    }

    const char* InstanceName(int t)
    {
        switch (t) {
            case 0: return "Outpost";
            case 1: return "Explorable";
            case 2: return "Loading";
            default: return "Unknown";
        }
    }

    void ReadEquipment(GW::AgentLiving* living, std::vector<EquipSlot>& out)
    {
        if (!living || !living->equip || !*living->equip) {
            return;
        }
        GW::NPCEquipment* eq = *living->equip;
        static const char* slot_names[9] = {"weapon", "offhand", "chest", "legs", "head",
                                            "feet", "hands", "costume_body", "costume_head"};
        for (int i = 0; i < 9; ++i) {
            const GW::ItemData& d = eq->items[i];
            if (d.model_file_id == 0) {
                continue;
            }
            EquipSlot s;
            s.slot = slot_names[i];
            s.model_file_id = d.model_file_id;
            s.item_type = static_cast<int>(d.type);
            std::memcpy(&s.dye, &d.dye, sizeof(d.dye));
            out.push_back(std::move(s));
        }
    }

} // namespace

namespace GameState {

    GameStateSnapshot Gather(TargetSource source)
    {
        GameStateSnapshot s;
        s.source = (source == TargetSource::Player) ? "player" : "target";
        GW::AgentLiving* living = (source == TargetSource::Player)
                                      ? GW::Agents::GetControlledCharacter()
                                      : GW::Agents::GetTargetAsAgentLiving();

        s.map_id = static_cast<int>(GW::Map::GetMapID());
        s.instance_type = static_cast<int>(GW::Map::GetInstanceType());
        s.instance_name = InstanceName(s.instance_type);

        if (!living) {
            s.valid = false;
            return s;
        }
        s.valid = true;
        s.agent_id = living->agent_id;
        s.player_number = living->player_number;
        s.agent_model_type = living->agent_model_type;
        s.transmog_npc_id = living->transmog_npc_id;
        s.primary = static_cast<int>(living->primary);
        s.secondary = static_cast<int>(living->secondary);
        s.primary_name = ProfName(s.primary);
        s.secondary_name = ProfName(s.secondary);
        s.level = living->level;
        s.is_npc = living->IsNPC();
        s.is_female = living->GetIsFemale();
        s.weapon_type = living->weapon_type;
        s.weapon_item_type = living->weapon_item_type;
        s.offhand_item_type = living->offhand_item_type;
        s.weapon_item_id = living->weapon_item_id;
        s.offhand_item_id = living->offhand_item_id;
        s.model_state = living->model_state;
        s.animation_id = living->animation_id;
        s.animation_code = living->animation_code;
        s.animation_type = living->animation_type;
        s.animation_speed = living->animation_speed;
        s.pos_x = living->pos.x;
        s.pos_y = living->pos.y;
        s.pos_z = living->z;
        s.rotation = living->rotation_angle;
        s.box_width = living->width1;
        s.box_height = living->height1;
        ReadEquipment(living, s.equipment);
        return s;
    }

    std::wstring SuggestStem(const GameStateSnapshot& snap)
    {
        std::string stem = snap.source;
        stem += "_pn" + std::to_string(snap.player_number);
        stem += "_map" + std::to_string(snap.map_id);
        std::wstring w;
        w.reserve(stem.size());
        for (const char c : stem) {
            w.push_back(static_cast<wchar_t>(c));
        }
        return w;
    }

    std::string BuildManifest(const GameStateSnapshot& snap, const std::vector<MeshChunk>& chunks,
                              const Config& cfg, const CaptureStats& stats, const std::string& timestamp,
                              const std::vector<ProbeSample>& probes,
                              const std::vector<DrawLogEntry>& draw_log)
    {
        Manifest m;
        m.timestamp = timestamp;
        m.format = (cfg.format == OutputFormat::OBJ) ? "obj" : "stl";
        m.detail = (cfg.detail == DetailLevel::Advanced) ? "advanced" : "base";
        m.scope = (cfg.scope == CaptureScope::Filtered) ? "filtered" : "whole_scene";
        m.draws_seen = stats.draws_seen;
        m.draws_captured = stats.draws_captured;
        m.draws_skipped_unreadable = stats.draws_skipped_unreadable;
        m.draws_skipped_filtered = stats.draws_skipped_filtered;
        m.draws_skipped_isolation = stats.draws_skipped_isolation;
        m.draws_trimmed = stats.draws_trimmed;
        m.vertices = stats.vertices;
        m.triangles = stats.triangles;
        m.unique_textures = stats.unique_textures;
        m.hook_calls = stats.hook_calls;
        m.draws_2d_skipped = stats.draws_2d_skipped;
        m.dip_trianglelist = stats.dip_trianglelist;
        m.dip_trianglestrip = stats.dip_trianglestrip;
        m.dip_trianglefan = stats.dip_trianglefan;
        m.dip_other = stats.dip_other;
        m.dp_calls = stats.dp_calls;       m.dp_tris = stats.dp_tris;
        m.dpup_calls = stats.dpup_calls;   m.dpup_tris = stats.dpup_tris;
        m.dipup_calls = stats.dipup_calls; m.dipup_tris = stats.dipup_tris;
        m.diag_note = "Draw-path census for the armed frame. hook_calls = every DrawIndexedPrimitive "
                      "(vtbl 82) call; dip_* split those by primitive type (only dip_trianglelist is "
                      "decoded/captured today). dp_/dpup_/dipup_* count the sibling entry points "
                      "DrawPrimitive(81)/DrawPrimitiveUP(83)/DrawIndexedPrimitiveUP(84), with _tris = the "
                      "triangle-typed subset -- these are NOT decoded yet. A large dip_trianglestrip/fan or "
                      "*_tris count is where the never-captured bare-skin body is going.";
        m.pose_note = "Live grab is the current bind/animation pose only; GW skins in a vertex shader "
                      "and GWCA exposes no skeleton. model_state/animation_id record the pose for provenance.";
        m.probe_note = "probe[].regs lists the first 96 vertex-shader constant registers c0..c95 (see each "
                       "probe's reg_count), one float4 per register. GW view is c0-c3, projection c4-c7; find "
                       "the register whose translation row tracks subject.pos_* (x,y = pos_x,pos_y; height = "
                       "pos_z) to isolate this agent and solve world->render scale.";
        m.subject = snap;

        m.settings.format = m.format;
        m.settings.detail = m.detail;
        m.settings.target = (cfg.target == TargetSource::Player) ? "player" : "target";
        m.settings.scope = m.scope;
        m.settings.export_normals = cfg.export_normals;
        m.settings.dedupe = cfg.dedupe;
        m.settings.exclude_2d = cfg.exclude_2d;
        m.settings.export_uvs = cfg.export_uvs;
        m.settings.export_textures = cfg.export_textures;
        m.settings.trim_outliers = cfg.trim_outliers;
        m.settings.trim_k = cfg.trim_k;
        m.settings.filter_center_radius = cfg.filter_center_radius;
        m.settings.up_axis = cfg.up_axis;
        m.settings.filter_min_prims = cfg.filter_min_prims;
        m.settings.filter_max_prims = cfg.filter_max_prims;
        m.settings.filter_min_verts = cfg.filter_min_verts;
        m.settings.filter_max_extent = cfg.filter_max_extent;
        m.settings.filter_min_thickness = cfg.filter_min_thickness;
        m.settings.require_texture = cfg.require_texture;
        m.settings.require_skinned = cfg.require_skinned;
        m.settings.probe_shader_constants = cfg.probe_shader_constants;
        m.settings.isolate_by_bone = cfg.isolate_by_bone;
        m.settings.isolate_tolerance = cfg.isolate_tolerance;

        m.chunks.reserve(chunks.size());
        for (const auto& c : chunks) {
            ManifestChunk mc;
            mc.draw_index = c.draw_index;
            mc.vertices = static_cast<uint32_t>(c.positions.size() / 3);
            mc.triangles = static_cast<uint32_t>(c.indices.size() / 3);
            mc.stride = c.stride;
            mc.fvf = c.fvf;
            mc.has_vertex_shader = c.has_vertex_shader;
            mc.is_skinned = c.is_skinned;
            mc.alpha_blend = c.alpha_blend;
            mc.has_uv = !c.uvs.empty();
            mc.has_normal = !c.normals.empty();
            mc.texture_file = c.texture_file;
            mc.texture_format = c.texture_format;
            mc.texture_w = c.texture_w;
            mc.texture_h = c.texture_h;
            mc.aabb_min = {c.aabb_min[0], c.aabb_min[1], c.aabb_min[2]};
            mc.aabb_max = {c.aabb_max[0], c.aabb_max[1], c.aabb_max[2]};
            mc.center = {(c.aabb_min[0] + c.aabb_max[0]) * 0.5f,
                         (c.aabb_min[1] + c.aabb_max[1]) * 0.5f,
                         (c.aabb_min[2] + c.aabb_max[2]) * 0.5f};
            m.chunks.push_back(std::move(mc));
        }

        m.probe.reserve(probes.size());
        for (const auto& p : probes) {
            ManifestProbe mp;
            mp.draw_index = p.draw_index;
            mp.center = {p.center[0], p.center[1], p.center[2]};
            mp.reg_count = static_cast<int>(p.regs.size() / 4);
            mp.regs = p.regs;
            m.probe.push_back(std::move(mp));
        }

        m.draw_log_note = "Per-draw disposition of every triangle-list DrawIndexedPrimitive in the armed "
                          "frame (Config.log_draws). reason: captured | skip_2d (depth-test off, dropped by "
                          "exclude_2d) | dedup | filtered (size/extent/texture/skinned heuristic) | iso "
                          "(bone-palette didn't match the isolate target) | unreadable. is_skinned/has_texture "
                          "are the character-vs-scenery signals; ext is model-space AABB size (present only "
                          "when the draw was read). The bare-skin body is a skinned, textured, tall-ext draw -- "
                          "find it here and read its reason to see which stage drops it.";
        m.draw_log.reserve(draw_log.size());
        for (const auto& e : draw_log) {
            ManifestDrawLog md;
            md.seq = e.seq;
            md.prims = e.prims;
            md.verts = e.verts;
            md.is_skinned = e.is_skinned;
            md.has_texture = e.has_texture;
            md.z_enabled = e.z_enabled;
            md.ext = {e.ext[0], e.ext[1], e.ext[2]};
            md.reason = e.reason;
            m.draw_log.push_back(std::move(md));
        }

        return glz::write<glz::opts{.prettify = true}>(m).value_or(std::string{});
    }

} // namespace GameState
} // namespace Guildlite
