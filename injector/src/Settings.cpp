#include "Settings.h"
#include "Log.h"

#include <windows.h>

#include <glaze/glaze.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace Guildlite {

// Flat, glaze-reflectable mirror of the persisted subset of Config. enums are stored
// as ints (their on-disk form in the old plugin) so the JSON is stable and we never
// lean on glaze enum-name reflection. Lives at namespace scope, not anonymous:
// glaze reflection keys on an external-linkage type. probe_shader_constants is
// intentionally absent -- like the plugin, it is a one-off diagnostic that resets
// each session so it can't be left on permanently.
struct PersistConfig {
    int format = 0;
    int detail = 1;
    int target = 0;
    int scope = 0;
    std::string export_dir;
    bool export_normals = true;
    bool dedupe = true;
    bool exclude_2d = true;
    bool export_uvs = true;
    bool export_textures = true;
    bool write_manifest = true;
    bool include_armor = true;
    bool include_weapons = true;
    int filter_min_prims = 4;
    int filter_max_prims = 0;
    int filter_min_verts = 0;
    float filter_max_extent = 0.f;
    float filter_min_thickness = 0.f;
    bool require_texture = false;
    bool require_skinned = false;
    bool drop_effects = false;      // MUST persist: clean-* profiles set this on, and the dev
                                    // loop reloads constantly -- omitting it silently reverted
                                    // every profile's aura/effect cull back to off after a reload.
    bool trim_outliers = true;
    float trim_k = 6.f;
    float filter_center_radius = 0.f;
    int up_axis = 2;
    bool isolate_by_bone = false;
    float isolate_tolerance = 250.f;
    bool export_skin_weights = true; // persist so the #vbld substrate toggle survives a reload
    bool pose_to_live = false;       // persist the pose-reconstruction toggle across reloads
    bool window_visible = true;
};

namespace {

    PersistConfig FromConfig(const Config& c)
    {
        PersistConfig p;
        p.format = static_cast<int>(c.format);
        p.detail = static_cast<int>(c.detail);
        p.target = static_cast<int>(c.target);
        p.scope = static_cast<int>(c.scope);
        p.export_dir = c.export_dir;
        p.export_normals = c.export_normals;
        p.dedupe = c.dedupe;
        p.exclude_2d = c.exclude_2d;
        p.export_uvs = c.export_uvs;
        p.export_textures = c.export_textures;
        p.write_manifest = c.write_manifest;
        p.include_armor = c.include_armor;
        p.include_weapons = c.include_weapons;
        p.filter_min_prims = c.filter_min_prims;
        p.filter_max_prims = c.filter_max_prims;
        p.filter_min_verts = c.filter_min_verts;
        p.filter_max_extent = c.filter_max_extent;
        p.filter_min_thickness = c.filter_min_thickness;
        p.require_texture = c.require_texture;
        p.require_skinned = c.require_skinned;
        p.drop_effects = c.drop_effects;
        p.trim_outliers = c.trim_outliers;
        p.trim_k = c.trim_k;
        p.filter_center_radius = c.filter_center_radius;
        p.up_axis = c.up_axis;
        p.isolate_by_bone = c.isolate_by_bone;
        p.isolate_tolerance = c.isolate_tolerance;
        p.export_skin_weights = c.export_skin_weights;
        p.pose_to_live = c.pose_to_live;
        p.window_visible = c.window_visible;
        return p;
    }

    void ApplyTo(const PersistConfig& p, Config& c)
    {
        c.format = static_cast<OutputFormat>(p.format);
        c.detail = static_cast<DetailLevel>(p.detail);
        c.target = static_cast<TargetSource>(p.target);
        c.scope = static_cast<CaptureScope>(p.scope);
        c.export_dir = p.export_dir;
        c.export_normals = p.export_normals;
        c.dedupe = p.dedupe;
        c.exclude_2d = p.exclude_2d;
        c.export_uvs = p.export_uvs;
        c.export_textures = p.export_textures;
        c.write_manifest = p.write_manifest;
        c.include_armor = p.include_armor;
        c.include_weapons = p.include_weapons;
        c.filter_min_prims = p.filter_min_prims;
        c.filter_max_prims = p.filter_max_prims;
        c.filter_min_verts = p.filter_min_verts;
        c.filter_max_extent = p.filter_max_extent;
        c.filter_min_thickness = p.filter_min_thickness;
        c.require_texture = p.require_texture;
        c.require_skinned = p.require_skinned;
        c.drop_effects = p.drop_effects;
        c.trim_outliers = p.trim_outliers;
        c.trim_k = p.trim_k;
        c.filter_center_radius = p.filter_center_radius;
        c.up_axis = p.up_axis;
        c.isolate_by_bone = p.isolate_by_bone;
        c.isolate_tolerance = p.isolate_tolerance;
        c.export_skin_weights = p.export_skin_weights;
        c.pose_to_live = p.pose_to_live;
        c.window_visible = p.window_visible;
    }

} // namespace
} // namespace Guildlite

namespace Settings {

    std::filesystem::path Dir()
    {
        wchar_t home[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        auto dir = std::filesystem::path(home) / L"Documents" / L"guildlite";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    std::filesystem::path File()
    {
        return Dir() / L"settings.json";
    }

    void Load(Guildlite::Config& cfg)
    {
        std::ifstream in(File(), std::ios::binary);
        if (!in) {
            return; // no settings yet -> keep the defaults already in cfg
        }
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string buffer = ss.str();
        if (buffer.empty()) {
            return;
        }
        // Seed the DTO from the live defaults so keys absent from the file survive.
        Guildlite::PersistConfig p = Guildlite::FromConfig(cfg);
        if (const auto err = glz::read_json(p, buffer)) {
            GL_DLLLOG("Settings::Load: parse error -- keeping defaults");
            return;
        }
        Guildlite::ApplyTo(p, cfg);
    }

    void Save(const Guildlite::Config& cfg)
    {
        const Guildlite::PersistConfig p = Guildlite::FromConfig(cfg);
        const std::string json = glz::write<glz::opts{.prettify = true}>(p).value_or(std::string{});
        if (json.empty()) {
            GL_DLLLOG("Settings::Save: serialise failed");
            return;
        }
        std::ofstream out(File(), std::ios::binary | std::ios::trunc);
        out << json;
    }

} // namespace Settings
