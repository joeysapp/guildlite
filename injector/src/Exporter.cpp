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

#include <cmath>
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
    bool pending_is_pick = false; // this in-flight capture is a pick Snap (render-thread only)
    int  pending_pick_n = 0;      // how many draws the Snap was asked to grab (for "matched X of N")
    Config pending_cfg;
    GameStateSnapshot pending_snapshot;

    // Pick-mode control-file requests: set by verbs on the stub poll thread, applied on the
    // render thread in Draw() (the pick list is render-thread-owned). Panel buttons call
    // Capture directly since the panel is already on the render thread.
    volatile bool pick_on_req = false, pick_off_req = false, pick_toggle_req = false;
    volatile int  pick_cycle_req = 0;
    volatile int  pick_skinned_req = 0; // 0 none, 1 on, 2 off
    volatile int  pick_2d_req = 0;      // 0 none, 1 on, 2 off
    volatile bool pick_mark_req = false, pick_clear_req = false;
    volatile bool pick_markall_req = false;
    volatile bool pick_clearlist_req = false;
    volatile bool pick_marktarget_req = false;
    volatile bool pick_snap_req = false;

    std::string status_line = "Ready.";
    CaptureStats last_stats;
    std::string last_output;

    bool has_last_aabb = false;
    float last_aabb_min[3] = {0.f, 0.f, 0.f};
    float last_aabb_max[3] = {0.f, 0.f, 0.f};

    char export_dir_buf[512] = {};
    char exclude_buf[256] = {};   // UI text mirror of g_config.exclude_list ("TxV,TxV" excludes)

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
        else if (key == "drop_effects")           { if (isb) g_config.drop_effects = b; }
        else if (key == "require_texture")        { if (isb) g_config.require_texture = b; }
        else if (key == "exclude_2d")             { if (isb) g_config.exclude_2d = b; }
        else if (key == "dedupe")                 { if (isb) g_config.dedupe = b; }
        else if (key == "trim_outliers")          { if (isb) g_config.trim_outliers = b; }
        else if (key == "export_normals")         { if (isb) g_config.export_normals = b; }
        else if (key == "export_uvs")             { if (isb) g_config.export_uvs = b; }
        else if (key == "export_textures")        { if (isb) g_config.export_textures = b; }
        else if (key == "log_draws")              { if (isb) g_config.log_draws = b; }
        else if (key == "export_skin_weights")    { if (isb) g_config.export_skin_weights = b; }
        else if (key == "pose_to_live")           { if (isb) g_config.pose_to_live = b; }
        else if (key == "exclude_list")           { g_config.exclude_list = val; }
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

    // A profile is a ONE-CLICK, WHOLE-STATE recipe. It first resets every capture field to its
    // default, then applies only the recipe's deltas -- so applying a profile can never leave a
    // stale filter/toggle from a previous profile silently influencing the next capture, and the
    // panel afterwards reflects exactly what will be captured. Only the environment (export dir +
    // window visibility) is preserved. Same names drive the SSH path (gw-ctl 'profile <name>').
    void ApplyProfile(const std::string& name)
    {
        // Validate the name BEFORE mutating anything, so an unknown profile is a no-op.
        static const char* kKnown[] = {"clean-full", "clean-full-target", "clean-self",
                                       "clean-target", "clean-solo", "clean-solo-target", "raw"};
        bool known = false;
        for (const char* k : kKnown) { if (name == k) { known = true; break; } }
        if (!known) { GL_DLLLOG("Exporter::ApplyProfile: unknown profile '%s'", name.c_str()); return; }

        const std::string keep_dir = g_config.export_dir;
        const bool keep_vis = g_config.window_visible;
        const bool keep_pose = g_config.pose_to_live;      // pose is orthogonal to scene filtering
        const std::string keep_excl = g_config.exclude_list; // the manual junk-list is global,
        const std::map<std::string, std::string> keep_lbl = g_config.mesh_labels; // as are labels
        g_config = Config{};               // reset EVERY capture setting to its default
        g_config.export_dir = keep_dir;    // ... but keep the user's environment + pose + excl/labels
        g_config.window_visible = keep_vis;
        g_config.pose_to_live = keep_pose;
        g_config.exclude_list = keep_excl;
        g_config.mesh_labels = keep_lbl;

        if (name == "clean-full" || name == "clean-full-target") {
            // The most COMPLETE solo character in one Export Snapshot. A/B testing (2026-07-06)
            // proved require_skinned was WRONG here: GW draws the dress/skirt (and some armor) as a
            // normal DIP mesh it does NOT flag as skinned, so require_skinned silently dropped it --
            // the "missing armor/robe" symptom. So keep require_skinned OFF and separate character
            // from scenery by SIZE instead: filter_max_extent drops terrain/structures,
            // filter_min_thickness drops HUD billboards (needed because exclude_2d is OFF to reach
            // depth-test-off pieces), drop_effects kills additive auras. Best in a SOLO instance --
            // with require_skinned off, nearby agents' meshes come in too, so isolate a
            // target-in-a-crowd with the pick path instead.
            g_config.scope = CaptureScope::Filtered;
            g_config.require_skinned = false;     // A/B: require_skinned dropped the non-skinned dress
            g_config.drop_effects = true;
            g_config.exclude_2d = false;          // include depth-test-off draws (reach z-off armor)
            g_config.filter_max_extent = 150.f;   // drop terrain/structure-sized meshes (scenery gate)
            g_config.filter_min_thickness = 1.5f; // drop HUD billboards let in by exclude_2d off
            g_config.trim_outliers = true;        // drop far-scattered foliage/props (stray flowers)
            g_config.target = (name == "clean-full-target") ? TargetSource::Target : TargetSource::Player;
        }
        else if (name == "clean-self" || name == "clean-target") {
            // Conservative character recipe: same as clean-full but keeps exclude_2d ON (drop the
            // HUD the cheap way). Misses z-off armor but is the safest "just my body" grab.
            g_config.scope = CaptureScope::Filtered;
            g_config.require_skinned = true;
            g_config.drop_effects = true;
            g_config.filter_max_extent = 150.f;
            g_config.filter_min_thickness = 1.5f;
            g_config.trim_outliers = true;        // drop far-scattered foliage/props
            g_config.target = (name == "clean-target") ? TargetSource::Target : TargetSource::Player;
        }
        else if (name == "clean-solo" || name == "clean-solo-target") {
            // Solo self-capture -- you are ALONE in the instance (private district, empty
            // explorable, guild hall). No crowd to isolate and no need to skin-gate (which also
            // blocks static props/attachments), so drop terrain by extent, HUD billboards by
            // thickness, and additive effect planes by blend mode. What's left is one clean body.
            g_config.scope = CaptureScope::Filtered;
            g_config.require_skinned = false;     // solo => no other characters to exclude
            g_config.drop_effects = true;
            g_config.filter_max_extent = 150.f;
            g_config.filter_min_thickness = 1.5f;
            g_config.trim_outliers = true;        // drop far-scattered foliage/props
            g_config.target = (name == "clean-solo-target") ? TargetSource::Target : TargetSource::Player;
        }
        else if (name == "raw") {
            // Whole scene, drop only exact-duplicate redraws (dedupe stays on by default) -- the
            // diagnostic baseline. exclude_2d + trim off so nothing is hidden.
            g_config.scope = CaptureScope::WholeScene;
            g_config.exclude_2d = false;
            g_config.trim_outliers = false;
        }
        Settings::Save(g_config);
        GL_DLLLOG("Exporter::ApplyProfile: '%s' applied (whole capture state reset + recipe)", name.c_str());
    }

    // Log the whole live capture state to the DLL log -- the SSH "verify settings" surface
    // (gw-ctl 'settings'; readable in the guildlite log, or fetch a capture-dry manifest which
    // also carries settings[]). Log-only: this runs on the stub poll thread, which must NOT call
    // into GWCA/D3D, so it deliberately touches nothing but g_config + the log.
    void DumpSettings()
    {
        const Config& c = g_config;
        GL_DLLLOG("Exporter settings: scope=%s target=%s detail=%s format=%s",
                  c.scope == CaptureScope::Filtered ? "filtered" : "whole",
                  c.target == TargetSource::Target ? "target" : "player",
                  c.detail == DetailLevel::Advanced ? "advanced" : "base",
                  c.format == OutputFormat::STL ? "stl" : "obj");
        GL_DLLLOG("  require_skinned=%d drop_effects=%d exclude_2d=%d require_texture=%d dedupe=%d",
                  c.require_skinned, c.drop_effects, c.exclude_2d, c.require_texture, c.dedupe);
        GL_DLLLOG("  isolate_by_bone=%d tol=%.0f max_extent=%.1f min_thickness=%.2f center_radius=%.1f",
                  c.isolate_by_bone, c.isolate_tolerance, c.filter_max_extent, c.filter_min_thickness,
                  c.filter_center_radius);
        GL_DLLLOG("  min_prims=%d max_prims=%d min_verts=%d trim=%d trim_k=%.1f up_axis=%d skin_weights=%d",
                  c.filter_min_prims, c.filter_max_prims, c.filter_min_verts, c.trim_outliers, c.trim_k,
                  c.up_axis, c.export_skin_weights);
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
        pending_is_pick = false; // the filter-stack path, not a pick Snap
        pending_cfg = g_config;
        if (pending_cfg.pose_to_live) pending_cfg.probe_shader_constants = true; // pose needs palettes
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

    // Snap exactly the draw currently selected in pick mode -- no filter stack, no
    // isolation; the pick IS the selection. Flushes through the normal capture path.
    void PickSnap()
    {
        if (!Game::Ready()) { status_line = "GWCA not ready -- cannot snap."; return; }
        if (capture_in_flight) return;
        // Snap the MARKED set; fall back to the cursor only when nothing is marked. Guarding on
        // the cursor alone was the "nothing selected to snap" bug: with a stable content key the
        // cursor can still be absent from a given frame while marks are perfectly valid.
        const int marks = Capture::PickMarkedCount();
        if (marks == 0 && !Capture::HasSelection()) {
            status_line = "Pick: nothing marked or selected to snap.";
            return;
        }
        g_config.export_dir = export_dir_buf;
        pending_dry_run = false;
        pending_is_pick = true;
        pending_pick_n = (marks > 0) ? marks : 1;
        pending_cfg = g_config;
        if (pending_cfg.pose_to_live) pending_cfg.probe_shader_constants = true; // pose needs palettes
        pending_snapshot = GameState::Gather(g_config.target);
        pending_cfg.has_match_pos = false;   // the pick is the filter; isolation not needed
        pending_cfg.trim_outliers = false;   // export exactly the marked draws, trim nothing
        pending_cfg.filter_center_radius = 0.f;
        pending_cfg.exclude_2d = false;      // a marked draw may be depth-test-off (e.g. armor)
        Capture::ArmSelected(pending_cfg);
        capture_in_flight = true;
        status_line = "Snapping selected draw...";
    }

    // Mark every draw skinned to the Source agent's skeleton in one action (a whole
    // character, regardless of draw order), via the bone-palette isolation signal. Uses the
    // same calibration isolate_by_bone does; if it marks nothing on a given map the palette
    // layout differs there -- fall back to manual marking. Then Snap exports the set.
    void PickMarkTarget()
    {
        if (!Game::Ready()) { status_line = "GWCA not ready."; return; }
        if (!Capture::PickActive()) { status_line = "Enable Pick mode first."; return; }
        const GameStateSnapshot s = GameState::Gather(g_config.target);
        if (!s.valid) { status_line = "No source agent to group by (choose Player, or target one)."; return; }
        const float pos[3] = {s.pos_x, s.pos_y, s.pos_z};
        Capture::PickMarkMatching(pos, g_config.isolate_tolerance);
        status_line = "Marking all of the source's pieces (bone-palette match)...";
    }

    // GW hands NON-skinned character meshes (dress/skirt/some armor) already baked into WORLD
    // space, while skinned meshes arrive bind-pose-local at the origin (the bone palette poses
    // them in-shader). Left alone they sit thousands of units apart -- the export renders as a
    // speck. Re-seat each world-space piece into the agent's local frame so it sits ON the body:
    //     local = Rz(-facing) * (world - agent_pos)      (facing rotates the horizontal x/y; z=up)
    // Verified 2026-07-06 against a targeted capture. NOTE: the skinned body is BIND pose while
    // these pieces are the LIVE pose, so the root/facing align but limb-level pose does not --
    // "close, not exact". Exact assembly needs posing the body from the captured bone weights
    // (see ROADMAP "rig/pose"). Needs a VALID snapshot (real Source agent -> pos + facing);
    // skinned chunks are already local and left untouched.
    void AlignWorldSpaceChunks(const GameStateSnapshot& snap, std::vector<MeshChunk>& chunks)
    {
        if (!snap.valid) return;
        const float px = snap.pos_x, py = snap.pos_y, pz = snap.pos_z;
        const float c = std::cos(snap.rotation), s = std::sin(snap.rotation);
        for (auto& ch : chunks) {
            if (ch.is_skinned || ch.positions.size() < 3) continue; // skinned = already local
            const float cx = (ch.aabb_min[0] + ch.aabb_max[0]) * 0.5f;
            const float cy = (ch.aabb_min[1] + ch.aabb_max[1]) * 0.5f;
            const float cz = (ch.aabb_min[2] + ch.aabb_max[2]) * 0.5f;
            if (cx * cx + cy * cy + cz * cz < 500.f * 500.f) continue; // already near local origin
            float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
            for (size_t i = 0; i + 2 < ch.positions.size(); i += 3) {
                const float dx = ch.positions[i]     - px;
                const float dy = ch.positions[i + 1] - py;
                const float dz = ch.positions[i + 2] - pz;
                const float lx =  dx * c + dy * s; // Rz(-facing) on the horizontal plane
                const float ly = -dx * s + dy * c;
                ch.positions[i]     = lx;
                ch.positions[i + 1] = ly;
                ch.positions[i + 2] = dz;          // z (up) is unchanged by a facing rotation
                const float p[3] = {lx, ly, dz};
                for (int k = 0; k < 3; ++k) {
                    if (i == 0) { mn[k] = mx[k] = p[k]; }
                    else { if (p[k] < mn[k]) mn[k] = p[k]; if (p[k] > mx[k]) mx[k] = p[k]; }
                }
            }
            for (int k = 0; k < 3; ++k) { ch.aabb_min[k] = mn[k]; ch.aabb_max[k] = mx[k]; }
            for (size_t i = 0; i + 2 < ch.normals.size(); i += 3) { // un-rotate normal directions
                const float nx = ch.normals[i], ny = ch.normals[i + 1];
                ch.normals[i]     =  nx * c + ny * s;
                ch.normals[i + 1] = -nx * s + ny * c;
            }
        }
    }

    // Pose reconstruction (experimental, pose_to_live): pose the captured BIND-pose skinned body
    // forward into the LIVE frame using the per-draw bone palette, so it matches GW's live-pose
    // non-skinned pieces (armor/dress). Each skinned chunk uses its OWN palette (matched by
    // draw_index -- GW remaps bones per draw); the palette base register is self-calibrated per
    // draw by scanning for the bone whose translation is the agent's world position. Then
    // world_v = BoneMatrix[vertex_bone] * bind_v, re-seated to local like the non-skinned pieces.
    // GW is rigid single-bone, so the vertex's bone = blend index byte 0. Verified 2026-07-06
    // (base c53 on map 248): reconstructs a coherent live-pose body that the armor lines up with.
    void PoseChunks(const GameStateSnapshot& snap, std::vector<MeshChunk>& chunks,
                    const std::vector<ProbeSample>& probes)
    {
        if (!snap.valid) return;
        std::map<uint32_t, const ProbeSample*> pal;
        for (const auto& p : probes) pal[p.draw_index] = &p;
        const float px = snap.pos_x, py = snap.pos_y, pz = snap.pos_z;
        const float cc = std::cos(snap.rotation), ss = std::sin(snap.rotation);
        for (auto& ch : chunks) {
            if (!ch.is_skinned) continue;               // non-skinned -> AlignWorldSpaceChunks
            const auto it = pal.find(ch.draw_index);
            if (it == pal.end()) continue;              // no palette captured -> leave bind pose
            const std::vector<float>& regs = it->second->regs;
            const int nregs = static_cast<int>(regs.size() / 4);
            int base = -1;                              // self-calibrate: bone whose .w == agent pos
            for (int r = 0; r + 2 < nregs; ++r) {
                if (std::fabs(regs[r * 4 + 3] - px) < 250.f &&
                    std::fabs(regs[(r + 1) * 4 + 3] - py) < 250.f &&
                    std::fabs(regs[(r + 2) * 4 + 3] - pz) < 250.f) { base = r; break; }
            }
            if (base < 0) continue;                     // palette not in this draw's captured window
            const size_t nv = ch.positions.size() / 3;
            float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0}; bool any = false;
            for (size_t v = 0; v < nv; ++v) {
                const float bx = ch.positions[v * 3], by = ch.positions[v * 3 + 1], bz = ch.positions[v * 3 + 2];
                const int bone = (v * 4 < ch.blend_indices.size()) ? static_cast<int>(ch.blend_indices[v * 4]) : 0;
                const int b = base + 3 * bone;
                if (b + 2 >= nregs) continue;           // bone beyond captured palette -> leave
                const float* r0 = &regs[b * 4]; const float* r1 = &regs[(b + 1) * 4]; const float* r2 = &regs[(b + 2) * 4];
                const float wx = r0[0] * bx + r0[1] * by + r0[2] * bz + r0[3];
                const float wy = r1[0] * bx + r1[1] * by + r1[2] * bz + r1[3];
                const float wz = r2[0] * bx + r2[1] * by + r2[2] * bz + r2[3];
                const float dx = wx - px, dy = wy - py, dz = wz - pz;
                ch.positions[v * 3]     =  cc * dx + ss * dy;   // re-seat: Rz(-facing)*(world-pos)
                ch.positions[v * 3 + 1] = -ss * dx + cc * dy;
                ch.positions[v * 3 + 2] = dz;
                if (v * 3 + 2 < ch.normals.size()) {            // rotate normal by R_bone then -facing
                    const float nx = ch.normals[v * 3], ny = ch.normals[v * 3 + 1], nz = ch.normals[v * 3 + 2];
                    const float wnx = r0[0] * nx + r0[1] * ny + r0[2] * nz;
                    const float wny = r1[0] * nx + r1[1] * ny + r1[2] * nz;
                    const float wnz = r2[0] * nx + r2[1] * ny + r2[2] * nz;
                    ch.normals[v * 3]     =  cc * wnx + ss * wny;
                    ch.normals[v * 3 + 1] = -ss * wnx + cc * wny;
                    ch.normals[v * 3 + 2] = wnz;
                }
                const float p[3] = {ch.positions[v * 3], ch.positions[v * 3 + 1], ch.positions[v * 3 + 2]};
                for (int k = 0; k < 3; ++k) { if (!any) { mn[k] = mx[k] = p[k]; } else { if (p[k] < mn[k]) mn[k] = p[k]; if (p[k] > mx[k]) mx[k] = p[k]; } }
                any = true;
            }
            if (any) for (int k = 0; k < 3; ++k) { ch.aabb_min[k] = mn[k]; ch.aabb_max[k] = mx[k]; }
        }
    }

    void FlushCapture(IDirect3DDevice9* device)
    {
        // Assemble ONE model in a single local frame: (pose_to_live) pose the skinned body forward
        // into the live pose so it matches GW's live-pose non-skinned pieces, then re-seat those
        // world-space pieces onto it. Only for single-character captures (pick snap or Filtered)
        // with a valid agent.
        if ((pending_is_pick || pending_cfg.scope == CaptureScope::Filtered) && pending_snapshot.valid) {
            if (pending_cfg.pose_to_live) {
                PoseChunks(pending_snapshot, Capture::Chunks(), Capture::ProbeSamples());
            }
            AlignWorldSpaceChunks(pending_snapshot, Capture::Chunks());
        }
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
            if (pending_is_pick) {
                char b[96];
                _snprintf_s(b, sizeof(b), _TRUNCATE, "  (Snap: %u of %d marked draws matched)",
                            stats.draws_captured, pending_pick_n);
                status_line += b;
            }
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

    // --- pick-list exclude helpers: toggle a "TRISxVERTS" token in g_config.exclude_list -------
    bool ExclContains(const std::string& list, uint32_t t, uint32_t v)
    {
        size_t pos = 0;
        while (pos < list.size()) {
            const size_t comma = list.find(',', pos);
            const std::string e = list.substr(pos, (comma == std::string::npos ? list.size() : comma) - pos);
            const size_t x = e.find_first_of("xX*");
            if (x != std::string::npos &&
                static_cast<uint32_t>(std::atoi(e.c_str())) == t &&
                static_cast<uint32_t>(std::atoi(e.c_str() + x + 1)) == v) {
                return true;
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return false;
    }

    void ExclToggle(std::string& list, uint32_t t, uint32_t v)
    {
        if (ExclContains(list, t, v)) {                  // rebuild the list without the matched token
            std::string out;
            size_t pos = 0;
            while (pos <= list.size()) {
                const size_t comma = list.find(',', pos);
                const std::string e = list.substr(pos, (comma == std::string::npos ? list.size() : comma) - pos);
                const size_t x = e.find_first_of("xX*");
                const bool match = x != std::string::npos &&
                                   static_cast<uint32_t>(std::atoi(e.c_str())) == t &&
                                   static_cast<uint32_t>(std::atoi(e.c_str() + x + 1)) == v;
                if (!match && !e.empty()) { if (!out.empty()) out += ","; out += e; }
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            list.swap(out);
        }
        else {                                           // append "TxV"
            char tok[32];
            _snprintf_s(tok, sizeof(tok), _TRUNCATE, "%ux%u", t, v);
            if (!list.empty()) list += ",";
            list += tok;
        }
    }

    // --- control panel (was DrawControlPanel) ------------------------------
    void DrawControlPanel(IDirect3DDevice9* device)
    {
        const bool gw_ready = Game::Ready();
        const bool pick_on = Capture::PickActive();
        // Shown under the filter/isolation/cleanup headers while Pick mode is on, because a
        // Snap bypasses all of them -- without this the panel silently implies they apply.
        const auto snapNote = [&]() {
            if (pick_on) {
                ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f),
                                   "Pick mode on: this section does NOT affect a Snap (only 'Export Snapshot').");
            }
        };

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

        // --- Profiles (one-click, whole-state recipes) + save/load -------------
        if (ImGui::CollapsingHeader("Profiles (one-click setup)", ImGuiTreeNodeFlags_DefaultOpen)) {
            static int prof_idx = 0;
            const char* profiles[] = {"clean-full", "clean-full-target", "clean-self", "clean-target",
                                      "clean-solo", "clean-solo-target", "raw"};
            ImGui::SetNextItemWidth(220.f);
            ImGui::Combo("##profile", &prof_idx, profiles, IM_ARRAYSIZE(profiles));
            ImGui::SameLine();
            if (ImGui::Button("Apply profile")) {
                ApplyProfile(profiles[prof_idx]);
                _snprintf_s(export_dir_buf, sizeof(export_dir_buf), _TRUNCATE, "%s", g_config.export_dir.c_str());
                status_line = std::string("Applied profile '") + profiles[prof_idx] +
                              "' -- whole capture state set; the panel below now reflects it.";
            }
            ImGui::TextDisabled("A profile RESETS every capture setting, then applies its recipe. clean-full =\n"
                                "drop-effects + Exclude-2D OFF + size-gated (NO require-skinned, so non-skinned\n"
                                "pieces like dresses/skirts survive): the most complete solo character in one\n"
                                "Export Snapshot. Same names over SSH:  gw-ctl 'profile clean-full'.");
            if (ImGui::Button("Save settings now")) {
                Settings::Save(g_config);
                status_line = "Settings saved to settings.json.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload from disk")) {
                Settings::Load(g_config);
                g_config.window_visible = true; // never let a reloaded 'closed' strand the panel
                _snprintf_s(export_dir_buf, sizeof(export_dir_buf), _TRUNCATE, "%s", g_config.export_dir.c_str());
                _snprintf_s(exclude_buf, sizeof(exclude_buf), _TRUNCATE, "%s", g_config.exclude_list.c_str());
                status_line = "Settings reloaded from settings.json.";
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(also auto-saved on every change + on close)");
        }

        // --- Pick a draw (interactive select-one) ------------------------------
        if (ImGui::CollapsingHeader("Pick a draw (point-and-shoot)", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool active = Capture::PickActive();
            if (ImGui::Checkbox("Pick mode", &active)) {
                Capture::PickSetActive(active);
            }
            ImGui::TextDisabled("Lists the frame's pickable draws below and tints marked ones GREEN in-game\n"
                                "(the cursor is AMBER). Click rows (or Prev/Next + Mark) to MARK several --\n"
                                "body + each armor piece + weapon -- then Snap exports them together as one\n"
                                "model. No filters, no isolation: you pick exactly what you want.");
            if (active) {
                bool skinned_only = Capture::PickSkinnedOnly();
                if (ImGui::Checkbox("Skinned only (characters)", &skinned_only)) {
                    Capture::PickSetSkinnedOnly(skinned_only);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("cuts the list to skinned characters. WARNING: GW draws some pieces\n"
                                    "(dresses/skirts, some armor) as NON-skinned meshes -- this HIDES them.\n"
                                    "If a piece is missing from the list, turn this OFF.");
                bool include_2d = Capture::PickInclude2D();
                if (ImGui::Checkbox("Include depth-test-off draws", &include_2d)) {
                    Capture::PickSetInclude2D(include_2d);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("lists draws GW renders z-off (some armor; also the HUD -- pair with Skinned only)");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.f, 1.f),
                    "Snap exports EXACTLY the marked draws. The Scope/Filter and Isolation sections\n"
                    "below (plus Trim) do NOT affect a Snap -- the pick IS the filter. They apply\n"
                    "only to 'Export Snapshot'. (The up-axis/orientation remap still applies.)");
                ImGui::TextDisabled("Green in-game = marked (will export).  Amber = cursor (Prev/Next focus).\n"
                    "Click any row to mark/unmark it -- clicking a green row removes it from the set.");

                // One-click whole-character grouping: mark all of the Source's skinned pieces
                // at once (bone-palette match), sidestepping the scattered draw order.
                const bool wc_loading = gw_ready && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading;
                ImGui::BeginDisabled(!gw_ready || wc_loading || capture_in_flight);
                if (ImGui::Button("Capture whole character (clean-full)")) {
                    // 1:1 with `gw-ctl 'profile clean-full[-target]' capture`: apply the size-gated
                    // clean-full recipe for the current Source (keeps GW's non-skinned dress/armor +
                    // all textures automatically -- no hunting for a cape) and Export-Snapshot it.
                    // ApplyProfile preserves pose_to_live, so a posed capture stays posed.
                    ApplyProfile(g_config.target == TargetSource::Target ? "clean-full-target" : "clean-full");
                    _snprintf_s(export_dir_buf, sizeof(export_dir_buf), _TRUNCATE, "%s", g_config.export_dir.c_str());
                    BeginCapture(false);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("One click EXPORTS the whole character straight to a file -- it does\n"
                                    "NOT green-pick anything (the size filter grabs body + dress/armor +\n"
                                    "textures directly; pick-selection can't tell a character from scenery).\n"
                                    "Watch the status line / in-game chat for 'saved'. Keeps your pose toggle.");

                const int n = Capture::PickCount();
                const int sel = Capture::PickIndex();
                const int marked = Capture::PickMarkedCount();

                ImGui::BeginDisabled(n == 0);
                if (ImGui::Button("< Prev")) Capture::PickCycle(-1);
                ImGui::SameLine();
                if (ImGui::Button("Next >")) Capture::PickCycle(1);
                ImGui::SameLine();
                if (ImGui::Button(Capture::PickRowMarked(sel) ? "Unmark" : "Mark")) Capture::PickToggleMark();
                ImGui::SameLine();
                ImGui::Text("cursor %d/%d  |  marked %d", (sel < 0) ? 0 : sel + 1, n, marked);
                ImGui::EndDisabled();

                const bool loading = gw_ready && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading;
                const int to_snap = (marked > 0) ? marked : (Capture::HasSelection() ? 1 : 0);
                ImGui::BeginDisabled(n == 0);
                if (ImGui::Button("Mark all in view")) Capture::PickMarkAllFiltered();
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(marked == 0);
                if (ImGui::Button("Clear marks")) Capture::PickClearMarks();
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(n == 0);
                if (ImGui::Button("Clear list")) Capture::PickClearList();
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("The list is a running window of pieces drawn in the last few seconds\n"
                                      "(some render only intermittently). Clear it when you switch targets.");
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!gw_ready || loading || capture_in_flight || to_snap == 0);
                char snaplabel[64];
                _snprintf_s(snaplabel, sizeof(snaplabel), _TRUNCATE, "Snap %d draw%s", to_snap, to_snap == 1 ? "" : "s");
                if (ImGui::Button(snaplabel, ImVec2(-1, 0))) PickSnap();
                ImGui::EndDisabled();

                ImGui::TextDisabled("Per row:  [x] never-export toggle | label (click to name) | the draw.");
                ImGui::BeginChild("pick_list", ImVec2(0, 175), true);
                static uint32_t label_edit = 0;   // r.id whose label is being edited (0 = none)
                static char label_buf[48] = {};
                static bool label_focus = false;
                for (int i = 0; i < n; ++i) {
                    const PickInfo r = Capture::PickRow(i);
                    const bool mk = Capture::PickRowMarked(i);
                    const bool cur = (i == sel);
                    char key[32];
                    _snprintf_s(key, sizeof(key), _TRUNCATE, "%ux%u", r.tris, r.verts);
                    const bool excl = ExclContains(g_config.exclude_list, r.tris, r.verts);
                    ImGui::PushID(static_cast<int>(r.id));
                    // [x] never-export toggle -> the global, persisted exclude list.
                    bool ex = excl;
                    if (ImGui::Checkbox("##excl", &ex)) {
                        ExclToggle(g_config.exclude_list, r.tris, r.verts);
                        _snprintf_s(exclude_buf, sizeof(exclude_buf), _TRUNCATE, "%s", g_config.exclude_list.c_str());
                        Settings::Save(g_config);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Never export this tris x verts (global, persisted)");
                    ImGui::SameLine();
                    // Label: click to edit; Enter or click-away commits. Keyed by tris x verts, so
                    // it sticks to that mesh across captures and is saved to settings.json.
                    const auto lit = g_config.mesh_labels.find(key);
                    const std::string cur_label = (lit != g_config.mesh_labels.end()) ? lit->second : std::string();
                    ImGui::SetNextItemWidth(80.f);
                    if (label_edit == r.id) {
                        if (label_focus) { ImGui::SetKeyboardFocusHere(); label_focus = false; }
                        const bool enter = ImGui::InputText("##lbl", label_buf, sizeof(label_buf),
                                                            ImGuiInputTextFlags_EnterReturnsTrue);
                        if (enter || ImGui::IsItemDeactivated()) {
                            if (label_buf[0]) g_config.mesh_labels[key] = label_buf;
                            else g_config.mesh_labels.erase(key);
                            Settings::Save(g_config);
                            label_edit = 0;
                        }
                    }
                    else {
                        char btn[64];
                        _snprintf_s(btn, sizeof(btn), _TRUNCATE, "%-9s##lb", cur_label.empty() ? "label..." : cur_label.c_str());
                        if (ImGui::Button(btn)) {
                            label_edit = r.id; label_focus = true;
                            _snprintf_s(label_buf, sizeof(label_buf), _TRUNCATE, "%s", cur_label.c_str());
                        }
                    }
                    ImGui::SameLine();
                    // The draw row (click = mark green / deselect). Greyed when excluded. Stable
                    // #id + hidden ##id keep the row's identity as the list prunes/reorders.
                    char label[176];
                    _snprintf_s(label, sizeof(label), _TRUNCATE, "%s %s #%u  %u tris %u verts %dx%d%s##pk%u",
                                mk ? "[x]" : "[ ]", cur ? ">" : " ", r.id, r.tris, r.verts,
                                r.tex_w, r.tex_h, r.skinned ? " skin" : "", r.id);
                    if (excl) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.45f, 0.45f, 1.f));
                    if (ImGui::Selectable(label, mk || cur)) { Capture::PickSelect(i); Capture::PickToggleMarkRow(i); }
                    if (excl) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::EndChild();
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
            snapNote();
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
                ImGui::Checkbox("Drop effect planes (auras/glows -> black panels)", &g_config.drop_effects);
                ImGui::TextDisabled("Drops additive/screen 'effect' draws -- enchant auras, glows, weapon\n"
                                    "trails. Their black texture background is invisible in-game but exports\n"
                                    "as a solid black panel stuck to the model. Detected from the blend mode,\n"
                                    "so legit cutouts (hair/cape/feather) are kept. Turn ON with require-\n"
                                    "skinned OFF for a clean solo body -- this is the 'clean-solo' profile.");
                ImGui::Separator();
                ImGui::InputTextWithHint("Exclude tris x verts", "e.g.  20x40, 154x135", exclude_buf, sizeof(exclude_buf));
                g_config.exclude_list = exclude_buf;
                ImGui::TextDisabled("Drops draws with these EXACT tris x verts. Read a stray mesh's counts off\n"
                                    "the pick list (or a log_draws capture) and add it here to nuke recurring\n"
                                    "junk -- random flowers/props the size filters let through. Comma-separated.");
            }
        }

        // --- Isolation / calibration ------------------------------------------
        if (ImGui::CollapsingHeader("Isolation (bone-palette)", ImGuiTreeNodeFlags_DefaultOpen)) {
            snapNote();
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
            ImGui::Checkbox("Export skin weights (#vbld)", &g_config.export_skin_weights);
            ImGui::TextDisabled("Writes each vertex's 4 bone-palette indices + weights into the OBJ as\n"
                                "'#vbld' comment lines -- the mesh->bone binding needed to re-rig/pose the\n"
                                "export later. GW packs the indices in a D3DCOLOR; enable Probe to also dump\n"
                                "the bone transforms (VS constants) to the manifest.");
            ImGui::Separator();
            ImGui::Checkbox("Pose to live frame (experimental)", &g_config.pose_to_live);
            ImGui::SameLine();
            ImGui::TextDisabled("(forces Probe on)");
            ImGui::TextDisabled("Reconstructs the body's LIVE pose from the bone palette (world_v =\n"
                                "BoneMatrix[bone] * bind_v) so GW's live-pose non-skinned armor lines up\n"
                                "with the body instead of a bind-pose mismatch. Needs a VALID Source\n"
                                "(pos + facing). Rigid single-bone; not every piece is perfect yet.");
            ImGui::TextWrapped("Off = bind pose (skinned body as authored). GW skins in a vertex shader; "
                               "we read the per-draw bone palette from its VS constants to pose it.");
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
                ImGui::Text("skipped: 2d=%u  unreadable=%u  filtered=%u  effect=%u  iso=%u  trimmed=%u",
                            last_stats.draws_2d_skipped, last_stats.draws_skipped_unreadable,
                            last_stats.draws_skipped_filtered, last_stats.draws_skipped_effect,
                            last_stats.draws_skipped_isolation, last_stats.draws_trimmed);
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
        // Reopen the exporter window on every load/reload: the window's close box persists
        // its hidden state, and Gw.exe has no menu to reopen it, so a saved "closed" would
        // strand the panel. The overlay's bottom-left panel bar can hide it again at will.
        g_config.window_visible = true;
        _snprintf_s(export_dir_buf, sizeof(export_dir_buf), _TRUNCATE, "%s", g_config.export_dir.c_str());
        _snprintf_s(exclude_buf, sizeof(exclude_buf), _TRUNCATE, "%s", g_config.exclude_list.c_str());
        GL_DLLLOG("Exporter::Init: settings loaded (dir='%s')", g_config.export_dir.c_str());
    }

    bool& WindowVisible() { return g_config.window_visible; }

    void Draw(IDirect3DDevice9* device)
    {
        if (!Capture::IsInstalled() && device) {
            Capture::Install(device);
        }

        // Apply queued pick-mode requests from control-file verbs, then age the pick list.
        // Before the window-visibility check so pick works even with the panel hidden.
        if (pick_on_req)     { pick_on_req = false;     Capture::PickSetActive(true); }
        if (pick_off_req)    { pick_off_req = false;    Capture::PickSetActive(false); }
        if (pick_toggle_req) { pick_toggle_req = false; Capture::PickSetActive(!Capture::PickActive()); }
        if (pick_cycle_req)  { const int d = pick_cycle_req; pick_cycle_req = 0; Capture::PickCycle(d); }
        if (pick_skinned_req) { Capture::PickSetSkinnedOnly(pick_skinned_req == 1); pick_skinned_req = 0; }
        if (pick_2d_req)     { Capture::PickSetInclude2D(pick_2d_req == 1); pick_2d_req = 0; }
        if (pick_mark_req)   { pick_mark_req = false;  Capture::PickToggleMark(); }
        if (pick_markall_req) { pick_markall_req = false; Capture::PickMarkAllFiltered(); }
        if (pick_clearlist_req) { pick_clearlist_req = false; Capture::PickClearList(); }
        if (pick_clear_req)  { pick_clear_req = false; Capture::PickClearMarks(); }
        if (pick_marktarget_req) { pick_marktarget_req = false; PickMarkTarget(); }
        if (pick_snap_req)   { pick_snap_req = false; PickSnap(); }
        Capture::PickCommit();

        if (capture_in_flight) {
            const CaptureState st = Capture::Advance();
            if (st == CaptureState::Ready) {
                FlushCapture(device);
                Capture::Reset();
                capture_in_flight = false;
                pending_is_pick = false;
            }
            else if (st == CaptureState::Failed) {
                last_stats = Capture::Stats();
                if (last_stats.hook_calls == 0) {
                    status_line = "Hook never fired this frame (see diagnostics below).";
                }
                else if (pending_is_pick) {
                    // A Snap saw draws but matched none of the marked signatures. The two usual
                    // causes are named right here so the panel is self-diagnosing: the target
                    // isn't issued as a TRIANGLELIST this frame (census shows strip/DIPUP high,
                    // list low), or its buffer identity shifted between marking and snapping.
                    char b[224];
                    _snprintf_s(b, sizeof(b), _TRUNCATE,
                        "Snap matched 0 of %d marked (seen=%u, list=%u). If 'DIP by type' shows "
                        "strip/DIPUP high with list low, the target isn't a TRIANGLELIST -- not "
                        "pickable yet; else its draw identity changed between mark and snap.",
                        pending_pick_n, last_stats.draws_seen, last_stats.dip_trianglelist);
                    status_line = b;
                }
                else {
                    status_line = "Draws seen but none captured -- check the diagnostics/filters below.";
                }
                Capture::Reset();
                capture_in_flight = false;
                pending_is_pick = false;
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
        // Pick mode: verbs set request flags the render thread applies (Draw), since the
        // pick list is render-thread-owned. "snap" grabs the currently selected draw.
        if (cmd == "pick") {
            const std::string a = (tok.size() >= 2) ? tok[1] : "toggle";
            if      (a == "on")     pick_on_req = true;
            else if (a == "off")    pick_off_req = true;
            else if (a == "toggle") pick_toggle_req = true;
            else if (a == "next")   pick_cycle_req += 1;
            else if (a == "prev")   pick_cycle_req -= 1;
            else if (a == "skinned") { pick_skinned_req = (tok.size() >= 3 && (tok[2] == "off" || tok[2] == "0")) ? 2 : 1; }
            else if (a == "2d")      { pick_2d_req = (tok.size() >= 3 && (tok[2] == "off" || tok[2] == "0")) ? 2 : 1; }
            else if (a == "mark")    pick_mark_req = true;
            else if (a == "markall") pick_markall_req = true;
            else if (a == "clearlist") pick_clearlist_req = true;
            else if (a == "clear" || a == "unmark") pick_clear_req = true;
            else if (a == "target" || a == "character") pick_marktarget_req = true;
            else GL_DLLLOG("Exporter::Command: unknown pick arg '%s'", a.c_str());
            return;
        }
        if (cmd == "mark")  { pick_mark_req = true; return; }
        if (cmd == "markall") { pick_markall_req = true; return; }
        if (cmd == "clear") { pick_clear_req = true; return; }
        if (cmd == "snap")  { pick_snap_req = true; return; }
        if (cmd == "settings" || cmd == "dump") { DumpSettings(); return; }
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
