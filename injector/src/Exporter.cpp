#include "Exporter.h"

#include "stl.h" // prelude: <Windows.h> + <functional> the GWCA managers assume, BEFORE any GWCA header

#include "Capture.h"
#include "GameState.h"
#include "GuildliteConfig.h"
#include "ObjWriter.h"
#include "TextureExport.h"

#include "Game.h"
#include "Log.h"
#include "Settings.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/MapMgr.h>

#include <imgui.h>

#include <d3d9.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Guildlite;

namespace {

    // --- exporter state (was GuildlitePlugin's members) --------------------
    Config g_config;

    bool capture_in_flight = false;
    bool pending_dry_run = false;
    Config pending_cfg;
    GameStateSnapshot pending_snapshot;

    std::string status_line = "Ready.";
    CaptureStats last_stats;
    std::string last_output;

    bool has_last_aabb = false;
    float last_aabb_min[3] = {0.f, 0.f, 0.f};
    float last_aabb_max[3] = {0.f, 0.f, 0.f};

    char export_dir_buf[512] = {};

    // --- small helpers (verbatim from the plugin) --------------------------
    std::wstring Widen(const std::string& s)
    {
        std::wstring w;
        w.reserve(s.size());
        for (const char c : s) {
            w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        return w;
    }

    std::string Narrow(const std::wstring& s)
    {
        std::string n;
        n.reserve(s.size());
        for (const wchar_t c : s) {
            n.push_back(static_cast<char>(c & 0xFF));
        }
        return n;
    }

    std::string TimeStamp()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buf[32];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%04u%02u%02u-%02u%02u%02u",
                    static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay),
                    static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond));
        return buf;
    }

    std::filesystem::path ResolveExportDir()
    {
        if (!g_config.export_dir.empty()) {
            return std::filesystem::path(g_config.export_dir);
        }
        return Settings::Dir(); // <Documents>\guildlite
    }

    // --- remote control plane (Increment 2): `set <key> <value>` / `profile <name>` ---
    // Mutates the single live g_config, which every capture snapshots at BeginCapture, so a
    // change takes effect on the next `capture`; persisted immediately so it also survives a
    // core `reload`. Runs on the stub poll thread; g_config is otherwise touched by the render
    // thread (panel + BeginCapture), but scalar writes are atomic on x86 and this is a
    // single-user dev tool -- acceptable without locking.
    bool ParseBool(const std::string& s, bool& out)
    {
        if (s == "1" || s == "true" || s == "on" || s == "yes")  { out = true;  return true; }
        if (s == "0" || s == "false" || s == "off" || s == "no") { out = false; return true; }
        return false;
    }

    void SetConfig(const std::string& key, const std::string& val)
    {
        bool b = false;
        const bool isb = ParseBool(val, b);
        const int   iv = std::atoi(val.c_str());
        const float fv = static_cast<float>(std::atof(val.c_str()));
        if      (key == "isolate_by_bone")        { if (isb) g_config.isolate_by_bone = b; }
        else if (key == "require_skinned")        { if (isb) g_config.require_skinned = b; }
        else if (key == "require_texture")        { if (isb) g_config.require_texture = b; }
        else if (key == "exclude_2d")             { if (isb) g_config.exclude_2d = b; }
        else if (key == "dedupe")                 { if (isb) g_config.dedupe = b; }
        else if (key == "trim_outliers")          { if (isb) g_config.trim_outliers = b; }
        else if (key == "export_normals")         { if (isb) g_config.export_normals = b; }
        else if (key == "export_uvs")             { if (isb) g_config.export_uvs = b; }
        else if (key == "export_textures")        { if (isb) g_config.export_textures = b; }
        else if (key == "log_draws")              { if (isb) g_config.log_draws = b; }
        else if (key == "probe_shader_constants") { if (isb) g_config.probe_shader_constants = b; }
        else if (key == "filter_max_extent")      { g_config.filter_max_extent = fv; }
        else if (key == "filter_min_thickness")   { g_config.filter_min_thickness = fv; }
        else if (key == "filter_center_radius")   { g_config.filter_center_radius = fv; }
        else if (key == "isolate_tolerance")      { g_config.isolate_tolerance = fv; }
        else if (key == "trim_k")                 { g_config.trim_k = fv; }
        else if (key == "filter_min_prims")       { g_config.filter_min_prims = iv; }
        else if (key == "filter_max_prims")       { g_config.filter_max_prims = iv; }
        else if (key == "filter_min_verts")       { g_config.filter_min_verts = iv; }
        else if (key == "up_axis")                { g_config.up_axis = iv; }
        else if (key == "scope")  { g_config.scope  = (val == "filtered" || val == "1") ? CaptureScope::Filtered : CaptureScope::WholeScene; }
        else if (key == "target") { g_config.target = (val == "target"   || val == "1") ? TargetSource::Target  : TargetSource::Player; }
        else if (key == "detail") { g_config.detail = (val == "advanced" || val == "1") ? DetailLevel::Advanced : DetailLevel::Base; }
        else if (key == "format") { g_config.format = (val == "stl"      || val == "1") ? OutputFormat::STL     : OutputFormat::OBJ; }
        else { GL_DLLLOG("Exporter::Set: unknown key '%s'", key.c_str()); return; }
        Settings::Save(g_config);
        GL_DLLLOG("Exporter::Set: %s = %s", key.c_str(), val.c_str());
    }

    void ApplyProfile(const std::string& name)
    {
        if (name == "clean-self" || name == "clean-target") {
            // The reproducible character recipe: NO iso (its bone-match drops the tall body
            // meshes -- proven via draw_log); require_skinned is the deterministic,
            // pose-independent "a character, not scenery" filter used instead.
            g_config.scope = CaptureScope::Filtered;
            g_config.isolate_by_bone = false;
            g_config.require_skinned = true;
            g_config.require_texture = false;
            g_config.filter_max_extent = 150.f;   // drops the 600-1300u structures
            g_config.filter_min_thickness = 1.5f; // drops HUD billboards
            g_config.filter_center_radius = 0.f;
            g_config.trim_outliers = true;
            g_config.exclude_2d = true;
            g_config.dedupe = true;
            g_config.target = (name == "clean-target") ? TargetSource::Target : TargetSource::Player;
        }
        else if (name == "raw") {
            // Whole scene, drop nothing but exact-duplicate redraws -- the diagnostic baseline.
            g_config.scope = CaptureScope::WholeScene;
            g_config.isolate_by_bone = false;
            g_config.require_skinned = false;
            g_config.require_texture = false;
            g_config.filter_max_extent = 0.f;
            g_config.filter_min_thickness = 0.f;
            g_config.trim_outliers = false;
            g_config.exclude_2d = false;
        }
        else { GL_DLLLOG("Exporter::ApplyProfile: unknown profile '%s'", name.c_str()); return; }
        Settings::Save(g_config);
        GL_DLLLOG("Exporter::ApplyProfile: %s applied", name.c_str());
    }

    // --- capture (was BeginCapture/FlushCapture) ---------------------------
    void BeginCapture(bool dry_run)
    {
        if (!Game::Ready()) {
            status_line = "GWCA not ready yet -- cannot read game state.";
            return;
        }
        g_config.export_dir = export_dir_buf;
        pending_dry_run = dry_run;
        pending_cfg = g_config;
        pending_snapshot = GameState::Gather(g_config.target);
        // Seed the render-thread isolation match from the target's GWCA world position
        // (the hook can't safely call GWCA), calibrated 1:1 to the bone-palette translation.
        pending_cfg.has_match_pos = pending_snapshot.valid;
        pending_cfg.match_pos[0] = pending_snapshot.pos_x;
        pending_cfg.match_pos[1] = pending_snapshot.pos_y;
        pending_cfg.match_pos[2] = pending_snapshot.pos_z;
        Capture::Arm(pending_cfg);
        capture_in_flight = true;
        status_line = dry_run ? "Refreshing diagnostics..." : "Capturing next frame...";
    }

    void FlushCapture(IDirect3DDevice9* device)
    {
        // Drop far-placed effect/billboard fliers before anything else so textures,
        // stats and the manifest all describe the trimmed model, not the fliers.
        Capture::TrimOutliers(pending_cfg);
        auto& chunks = Capture::Chunks();
        const CaptureStats stats = Capture::Stats();
        last_stats = stats;

        // Model-space bounds of the final (post-trim) geometry.
        has_last_aabb = false;
        for (const auto& c : chunks) {
            if (c.positions.empty()) {
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                if (!has_last_aabb) {
                    last_aabb_min[k] = c.aabb_min[k];
                    last_aabb_max[k] = c.aabb_max[k];
                }
                else {
                    if (c.aabb_min[k] < last_aabb_min[k]) last_aabb_min[k] = c.aabb_min[k];
                    if (c.aabb_max[k] > last_aabb_max[k]) last_aabb_max[k] = c.aabb_max[k];
                }
            }
            has_last_aabb = true;
        }

        // Dry run ("Refresh diagnostics"): stats + bounds are now populated; write nothing.
        if (pending_dry_run) {
            status_line = chunks.empty()
                              ? "Diagnostics: nothing captured with these settings (no files written)."
                              : "Diagnostics refreshed (no files written).";
            return;
        }

        const auto dir = ResolveExportDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        const std::string ts = TimeStamp();
        std::wstring stem = GameState::SuggestStem(pending_snapshot) + L"_" + Widen(ts);
        const std::string narrow_stem = Narrow(stem);

        int textures_written = 0;
        const bool advanced = pending_cfg.detail == DetailLevel::Advanced;
        if (advanced && pending_cfg.export_textures && pending_cfg.format == OutputFormat::OBJ) {
            std::map<void*, std::string> tex_names;
            int n = 0;
            for (auto& c : chunks) {
                if (!c.texture_ptr) {
                    continue;
                }
                const auto it = tex_names.find(c.texture_ptr);
                if (it != tex_names.end()) {
                    c.texture_file = it->second;
                    continue;
                }
                const std::string base = narrow_stem + "_tex" + std::to_string(n) + ".tga";
                const bool ok = TextureExport::SaveTGA(device, static_cast<IDirect3DTexture9*>(c.texture_ptr), dir / Widen(base));
                const std::string stored = ok ? base : std::string{};
                tex_names.emplace(c.texture_ptr, stored);
                c.texture_file = stored;
                if (ok) {
                    textures_written++;
                }
                n++;
            }
        }

        WriteResult wr;
        if (pending_cfg.format == OutputFormat::OBJ) {
            wr = ObjWriter::WriteObj(chunks, pending_cfg, dir, stem);
        }
        else {
            wr = ObjWriter::WriteStl(chunks, pending_cfg, dir, stem);
        }

        // Manifest is a cheap, vertex-free audit sidecar; write it for Base too.
        if (pending_cfg.write_manifest) {
            const std::string json = GameState::BuildManifest(pending_snapshot, chunks, pending_cfg, stats, ts,
                                                              Capture::ProbeSamples(), Capture::DrawLog());
            std::ofstream mf(dir / (stem + L".json"), std::ios::binary | std::ios::trunc);
            mf << json;
        }

        if (wr.ok) {
            last_output = wr.main_file.string();
            status_line = "Saved: " + last_output;
            if (Game::Ready()) {
                std::wstring msg = L"Guildlite: saved " + wr.main_file.filename().wstring() + L" (" +
                                   std::to_wstring(stats.draws_captured) + L" objects, " +
                                   std::to_wstring(stats.triangles) + L" tris";
                if (advanced) {
                    msg += L", " + std::to_wstring(textures_written) + L" textures";
                }
                msg += L")";
                GW::Chat::WriteChat(GW::Chat::CHANNEL_GWCA1, msg.c_str());
            }
        }
        else {
            status_line = "Export failed: " + wr.error;
            if (Game::Ready()) {
                GW::Chat::WriteChat(GW::Chat::CHANNEL_GWCA1, L"Guildlite: export failed (see control panel).");
            }
        }
    }

    // --- control panel (was DrawControlPanel) ------------------------------
    void DrawControlPanel(IDirect3DDevice9* device)
    {
        const bool gw_ready = Game::Ready();

        ImGui::TextWrapped("Snapshot the live 3D geometry of the player or your target to disk.");
        if (!gw_ready) {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.f), "Waiting for GWCA to initialise...");
        }
        ImGui::Separator();

        // --- Target ------------------------------------------------------------
        {
            const char* items[] = {"Player (self)", "Target (selection)"};
            int v = static_cast<int>(g_config.target);
            if (ImGui::Combo("Source", &v, items, 2)) {
                g_config.target = static_cast<TargetSource>(v);
            }
        }

        // --- Detail + format ---------------------------------------------------
        {
            const char* items[] = {"Base (geometry only)", "Advanced (textures + manifest)"};
            int v = static_cast<int>(g_config.detail);
            if (ImGui::Combo("Detail", &v, items, 2)) {
                g_config.detail = static_cast<DetailLevel>(v);
            }
        }
        {
            const char* items[] = {"OBJ (+mtl/textures)", "STL (geometry solid)"};
            int v = static_cast<int>(g_config.format);
            if (ImGui::Combo("Format", &v, items, 2)) {
                g_config.format = static_cast<OutputFormat>(v);
            }
        }

        // --- Geometry options --------------------------------------------------
        if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Export normals", &g_config.export_normals);
            ImGui::SameLine();
            ImGui::Checkbox("Dedupe redraws", &g_config.dedupe);
            ImGui::Checkbox("Exclude 2D/UI (depth-test)", &g_config.exclude_2d);
            ImGui::TextDisabled("Dedupe collapses shadow/main passes. Exclude-2D drops HUD/UI;\n"
                                "turn it off if 'draws_2d_skipped' is high but nothing captured.");
        }

        // --- Cleanup / orientation --------------------------------------------
        if (ImGui::CollapsingHeader("Cleanup / Orientation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Trim outlier fliers", &g_config.trim_outliers);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.f);
            ImGui::InputFloat("MAD k", &g_config.trim_k);
            ImGui::TextDisabled("Drops effect/billboard quads placed far from the model that\n"
                                "otherwise blow the export's bounds up (model renders as a speck).");
            const char* up_items[] = {"X", "Y (already up)", "Z (GW default)"};
            ImGui::Combo("Model up-axis", &g_config.up_axis, up_items, 3);
            ImGui::TextDisabled("GW is Z-up; OBJ/DCC tools are Y-up. Remaps to +Y so exports\n"
                                "stand upright instead of lying on their side (~90 deg).");
        }

        // --- Appearance (Advanced) --------------------------------------------
        if (g_config.detail == DetailLevel::Advanced) {
            if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("UV coordinates", &g_config.export_uvs);
                ImGui::Checkbox("Extract textures (TGA)", &g_config.export_textures);
                ImGui::Checkbox("Write JSON manifest / audit", &g_config.write_manifest);
                ImGui::Separator();
                ImGui::Checkbox("Record armor slots", &g_config.include_armor);
                ImGui::SameLine();
                ImGui::Checkbox("Record weapon slots", &g_config.include_weapons);
                ImGui::TextDisabled("Logs each slot's model id + dye to the manifest ONLY -- it does not\n"
                                    "change which geometry is captured or rendered. Per-slot geometry\n"
                                    "isolation arrives with the DAT loader (see ROADMAP).");
            }
        }

        // --- Scope / filter ----------------------------------------------------
        if (ImGui::CollapsingHeader("Scope / Filter")) {
            const char* items[] = {"Whole scene", "Filtered (isolate a model)"};
            int v = static_cast<int>(g_config.scope);
            if (ImGui::Combo("Scope", &v, items, 2)) {
                g_config.scope = static_cast<CaptureScope>(v);
            }
            if (g_config.scope == CaptureScope::Filtered) {
                ImGui::TextWrapped("These are per-draw size/count heuristics in MODEL space. They thin a "
                                   "scene (drop sprites, terrain, effects) but cannot separate two agents: "
                                   "GW skins in-shader so every character overlaps at the origin. To pull "
                                   "one agent out of a crowd use Isolation (below), not these.");
                ImGui::InputInt("Min triangles", &g_config.filter_min_prims);
                ImGui::InputInt("Max triangles (0=off)", &g_config.filter_max_prims);
                ImGui::InputInt("Min vertices", &g_config.filter_min_verts);
                ImGui::InputFloat("Max AABB extent (0=off)", &g_config.filter_max_extent);
                ImGui::TextDisabled("Drops a draw whose model-space box is larger than this (culls\n"
                                    "terrain-sized meshes). Does NOT separate people -- all agents are\n"
                                    "the same ~90u tall at the origin.");
                ImGui::InputFloat("Drop flat draws < thickness (0=off)", &g_config.filter_min_thickness);
                ImGui::TextDisabled("Drops near-planar billboards/decals/floating text/nameplates whose\n"
                                    "thinnest side is under this many units. They are exactly flat (0); a\n"
                                    "real mesh has thickness, so ~1-2 clears them. Use this when the HUD\n"
                                    "leaks in because your world renders depth-test off (Exclude-2D can't\n"
                                    "help). Watch it work in Diagnostics: 'filtered=' jumps.");
                ImGui::InputFloat("Center radius (0=off)", &g_config.filter_center_radius);
                ImGui::TextDisabled("Drops a draw whose center is farther than this from the scene's\n"
                                    "robust center (model space; needs >=4 draws). Culls a detached prop\n"
                                    "or terrain skirt -- it is measured from the scene center, not from you.");
                ImGui::Checkbox("Require a bound texture", &g_config.require_texture);
                ImGui::TextDisabled("Drops draws with no stage-0 texture (a few glow/effect quads). Most\n"
                                    "world geometry is textured, so this usually changes little.");
                ImGui::Checkbox("Require skinned mesh (drop static props/terrain)", &g_config.require_skinned);
                ImGui::TextDisabled("Keeps only meshes with bone blend weights -- an animated character,\n"
                                    "not scenery. THE fix for grabbing a nearby building/statue: it drops\n"
                                    "static geometry even when it sits right next to you, which isolation\n"
                                    "cannot. For a solo self-capture this alone gives just your body.\n"
                                    "Trade-off: also drops rigidly-attached items (some weapons).");
            }
        }

        // --- Isolation / calibration ------------------------------------------
        if (ImGui::CollapsingHeader("Isolation (bone-palette)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Isolate to Source agent", &g_config.isolate_by_bone);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputFloat("Match tolerance", &g_config.isolate_tolerance);
            ImGui::TextDisabled("The one filter that separates agents: keeps a skinned draw only if a\n"
                                "bone-palette register holds a world position within tolerance of the\n"
                                "Source's GWCA position. Units are world units, ~250 (< agent spacing).\n"
                                "Too small drops the body to a stray piece (e.g. just a cape); too large\n"
                                "lets neighbours back in. Only touches skinned draws -- a static prop with\n"
                                "a world matrix can slip through, terrain is unaffected (pair with the\n"
                                "filters above). Confirm it via 'skipped: iso=' in Diagnostics: 0 there\n"
                                "means nothing matched the layout -- re-calibrate with Probe.");
            ImGui::Separator();
            ImGui::Checkbox("Probe: dump shader constants to manifest", &g_config.probe_shader_constants);
            ImGui::TextDisabled("Isolation calibration only -- NOT textures/skin colour. Dumps VS\n"
                                "constants c0..c95 of the first skinned draws to manifest probe[]; use it\n"
                                "to find which register tracks subject.pos_* if isolation stops matching.");
        }

        // --- Live pose read-out ------------------------------------------------
        if (ImGui::CollapsingHeader("Animation / pose")) {
            GW::AgentLiving* live = nullptr;
            if (gw_ready) {
                live = (g_config.target == TargetSource::Player)
                           ? GW::Agents::GetControlledCharacter()
                           : GW::Agents::GetTargetAsAgentLiving();
            }
            if (live) {
                ImGui::Text("model_state: %u", live->model_state);
                ImGui::Text("animation_id: %u  code: %u", live->animation_id, live->animation_code);
            }
            else {
                ImGui::TextDisabled(gw_ready ? "No agent selected." : "GWCA not ready.");
            }
            ImGui::TextWrapped("A live grab is the current pose. GW skins in a vertex shader and exposes "
                               "no skeleton, so animation-frame export is future (DAT/memory) work.");
        }

        // --- Output path -------------------------------------------------------
        ImGui::Separator();
        ImGui::InputTextWithHint("Export dir", Narrow(ResolveExportDir().wstring()).c_str(),
                                 export_dir_buf, sizeof(export_dir_buf));
        g_config.export_dir = export_dir_buf;

        // --- Diagnostics (refresh without exporting) --------------------------
        if (ImGui::CollapsingHeader("Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
            GW::AgentLiving* src = nullptr;
            if (gw_ready) {
                src = (g_config.target == TargetSource::Player)
                          ? GW::Agents::GetControlledCharacter()
                          : GW::Agents::GetTargetAsAgentLiving();
            }
            if (src) {
                ImGui::Text("Source world pos: %.0f, %.0f, %.0f", src->pos.x, src->pos.y, src->z);
                ImGui::Text("Source box (GWCA): %.1f wide x %.1f tall", src->width1, src->height1);
            }
            else {
                ImGui::TextDisabled("Source: no %s selected.",
                                    g_config.target == TargetSource::Player ? "player" : "target");
            }

            const bool loading = gw_ready && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading;
            ImGui::BeginDisabled(!gw_ready || loading || capture_in_flight);
            if (ImGui::Button("Refresh diagnostics (writes no file)")) {
                BeginCapture(true);
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("Arms one capture with the current settings and reports the counts and\n"
                                "bounds below -- without writing any files.");

            if (last_stats.hook_calls > 0 || last_stats.draws_seen > 0 || last_stats.draws_captured > 0) {
                ImGui::Separator();
                ImGui::Text("hook_calls=%u  seen=%u  captured=%u",
                            last_stats.hook_calls, last_stats.draws_seen, last_stats.draws_captured);
                ImGui::Text("DIP by type: list=%u  strip=%u  fan=%u  other=%u",
                            last_stats.dip_trianglelist, last_stats.dip_trianglestrip,
                            last_stats.dip_trianglefan, last_stats.dip_other);
                ImGui::Text("siblings [tris]: DP=%u[%u]  DPUP=%u[%u]  DIPUP=%u[%u]",
                            last_stats.dp_calls, last_stats.dp_tris, last_stats.dpup_calls,
                            last_stats.dpup_tris, last_stats.dipup_calls, last_stats.dipup_tris);
                ImGui::Text("skipped: 2d=%u  unreadable=%u  filtered=%u  iso=%u  trimmed=%u",
                            last_stats.draws_2d_skipped, last_stats.draws_skipped_unreadable,
                            last_stats.draws_skipped_filtered, last_stats.draws_skipped_isolation,
                            last_stats.draws_trimmed);
                ImGui::Text("verts=%u  tris=%u  textures=%u",
                            last_stats.vertices, last_stats.triangles, last_stats.unique_textures);
                if (has_last_aabb) {
                    const float bw = last_aabb_max[0] - last_aabb_min[0];
                    const float bh = last_aabb_max[1] - last_aabb_min[1];
                    const float bd = last_aabb_max[2] - last_aabb_min[2];
                    ImGui::Text("model AABB: %.1f x %.1f x %.1f  (volume %.0f)", bw, bh, bd, bw * bh * bd);
                }
                ImGui::TextDisabled("'iso=' is the bone-isolation drop count. If Isolate is on and this\n"
                                    "stays 0, the palette layout did not match -- widen tolerance or Probe.");
            }
            else {
                ImGui::TextDisabled("Export or refresh to populate capture stats.");
            }
        }

        // --- Export ------------------------------------------------------------
        const bool loading = gw_ready && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading;
        const bool busy = capture_in_flight;
        ImGui::BeginDisabled(!gw_ready || loading || busy);
        if (ImGui::Button(busy ? "Capturing..." : "Export Snapshot", ImVec2(-1, 0))) {
            BeginCapture(false);
        }
        ImGui::EndDisabled();
        if (!gw_ready) {
            ImGui::TextDisabled("Waiting for GWCA...");
        }
        else if (loading) {
            ImGui::TextDisabled("Waiting for the map to finish loading...");
        }

        // --- Status ------------------------------------------------------------
        ImGui::Separator();
        if (Capture::IsInstalled()) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.f), "Capture hook: installed");
        }
        else {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.f), "Capture hook: NOT INSTALLED");
        }
        ImGui::TextWrapped("%s", status_line.c_str());
        ImGui::TextDisabled("Full counts/bounds are under Diagnostics above.");
        (void)device;
    }

} // namespace

namespace Exporter {

    void Init()
    {
        Settings::Load(g_config);
        _snprintf_s(export_dir_buf, sizeof(export_dir_buf), _TRUNCATE, "%s", g_config.export_dir.c_str());
        GL_DLLLOG("Exporter::Init: settings loaded (dir='%s')", g_config.export_dir.c_str());
    }

    void Draw(IDirect3DDevice9* device)
    {
        if (!Capture::IsInstalled() && device) {
            Capture::Install(device);
        }

        if (capture_in_flight) {
            const CaptureState st = Capture::Advance();
            if (st == CaptureState::Ready) {
                FlushCapture(device);
                Capture::Reset();
                capture_in_flight = false;
            }
            else if (st == CaptureState::Failed) {
                last_stats = Capture::Stats();
                status_line = last_stats.hook_calls == 0
                                  ? "Hook never fired this frame (see diagnostics below)."
                                  : "Draws seen but none captured -- check the diagnostics/filters below.";
                Capture::Reset();
                capture_in_flight = false;
            }
        }

        if (!g_config.window_visible) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(440, 620), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(420, 40), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Guildlite - Model Exporter", &g_config.window_visible)) {
            DrawControlPanel(device);
        }
        ImGui::End();
    }

    void Shutdown()
    {
        g_config.export_dir = export_dir_buf;
        Settings::Save(g_config);
        Capture::Remove();
        GL_DLLLOG("Exporter::Shutdown: settings saved, capture hook removed");
    }

    void Command(const char* verb)
    {
        if (!verb) {
            return;
        }
        std::vector<std::string> tok;
        {
            std::istringstream is(verb);
            std::string t;
            while (is >> t) tok.push_back(t);
        }
        if (tok.empty()) {
            return;
        }
        const std::string& cmd = tok[0];
        if (cmd == "set" && tok.size() >= 3)     { SetConfig(tok[1], tok[2]); return; }
        if (cmd == "target" && tok.size() >= 2)  { SetConfig("target", tok[1]); return; }
        if (cmd == "profile" && tok.size() >= 2) { ApplyProfile(tok[1]); return; }
        // Capture verbs -- guarded so they no-op during a map load or an in-flight grab.
        // Short-circuit protects GetInstanceType() from being called before GWCA is up.
        const bool loading = !Game::Ready() || GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading;
        if (cmd == "capture") {
            if (!capture_in_flight && !loading) BeginCapture(false);
        }
        else if (cmd == "capture-dry") {
            if (!capture_in_flight && !loading) BeginCapture(true);
        }
        else {
            GL_DLLLOG("Exporter::Command: unhandled verb '%s'", verb);
        }
    }

} // namespace Exporter
