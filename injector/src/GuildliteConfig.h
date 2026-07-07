#pragma once

#include <cstdint>
#include <map>
#include <string>

// ==============================================================================
// GuildliteConfig -- the single source of truth for user-facing capture options.
// Shared verbatim between the control panel (writes it), the capture engine
// (reads it in the D3D hook) and the writers. Kept POD-ish so a snapshot can be
// copied cheaply and read from the render thread without locking.
// ==============================================================================

namespace Guildlite {

    enum class OutputFormat : int {
        OBJ = 0, // Wavefront OBJ (+ .mtl + textures when Advanced)
        STL = 1, // geometry-only solid, no materials -- Base-tier convenience
    };

    // Base = plain uncoloured geometry. Advanced = UVs + textures + JSON manifest.
    enum class DetailLevel : int {
        Base = 0,
        Advanced = 1,
    };

    // How we decide which draw calls to keep. GW renders the whole frame (terrain,
    // sky, every agent, the HUD) so an unfiltered grab is the entire scene. The
    // Filtered mode applies the cheap, coordinate-free heuristics below so a single
    // character/prop can be isolated without knowing GW's world->render mapping.
    enum class CaptureScope : int {
        WholeScene = 0,
        Filtered = 1,
    };

    // Which agent's game-state metadata seeds the manifest + suggested filename.
    enum class TargetSource : int {
        Player = 0, // GW::Agents::GetControlledCharacter()
        Target = 1, // current selection (GW::Agents::GetTargetAsAgentLiving())
    };

    struct Config {
        // --- window / lifecycle ------------------------------------------------
        bool window_visible = true;

        // --- output ------------------------------------------------------------
        OutputFormat format = OutputFormat::OBJ;
        DetailLevel detail = DetailLevel::Advanced;
        TargetSource target = TargetSource::Player;
        // Empty => resolved at runtime to <Documents>\guildlite\.
        std::string export_dir;
        // Manual per-draw exclude list: "TRISxVERTS" sigs to drop, comma-separated (e.g.
        // "20x40,154x135"). Read a stray mesh's tris/verts off the pick list or a draw_log and
        // add it here to nuke recurring junk (random flowers/props) the size filters let through.
        std::string exclude_list;
        // Freeform per-mesh labels, keyed by "TRISxVERTS" (e.g. {"20x40":"flower"}). GLOBAL and
        // persisted (settings.json) -- a mesh you name/exclude stays named/excluded across every
        // capture and profile. Paired with exclude_list: label = memo, exclude_list = the cull.
        std::map<std::string, std::string> mesh_labels;

        // --- geometry (Base) ---------------------------------------------------
        bool export_normals = true;
        bool dedupe = true; // collapse identical (vbuf,ibuf,range) re-draws (shadow/main pass)
        bool exclude_2d = true; // skip Z-test-off draws (HUD/UI/ImGui). Turn off if the 3D
                                // world is being wrongly excluded (diagnose via draws_2d_skipped).

        // --- appearance (Advanced) --------------------------------------------
        bool export_uvs = true;
        bool export_textures = true;
        bool write_manifest = true; // JSON audit sidecar with GWCA game-state

        // Documentational toggles for the roadmap's armor/weapon axes. A live grab
        // is whole-body; these are recorded into the manifest and reserved for the
        // DAT-driven per-slot export planned later. They do not gate geometry yet.
        bool include_armor = true;
        bool include_weapons = true;

        // --- filter heuristics (only used when scope == Filtered) --------------
        CaptureScope scope = CaptureScope::WholeScene;
        int filter_min_prims = 4;    // drop trivial 1-2 triangle sprites/quads
        int filter_max_prims = 0;    // 0 = no upper bound (else drop terrain-sized meshes)
        int filter_min_verts = 0;
        float filter_max_extent = 0; // 0 = no bound; else drop chunks whose AABB is larger (world units)
        // Drop near-planar draws -- billboards, ground decals, floating combat text,
        // nameplates. These are exactly flat (thinnest AABB extent ~0) 2-tri quads and
        // are vertex-shaded + textured, so extent/texture/VS filters miss them; a real
        // mesh always has some thickness. Essential when the world renders depth-test
        // off, so Exclude-2D can't separate the HUD. 0 = off; ~1-2 removes billboards.
        float filter_min_thickness = 0.f;
        bool require_texture = false; // keep only textured draws (skins/props, not effects)
        // Keep only skinned meshes: GW characters carry per-vertex bone blend weights
        // (D3DFVF_XYZB* / BLENDWEIGHT), static world geometry and props do not. This is
        // the definitive "a character, not the scenery" test -- it drops a nearby
        // building/statue that sits within isolate_tolerance of the agent. Trade-off:
        // also drops rigidly-attached items (some weapons); they stay in the manifest.
        bool require_skinned = false;

        // Drop additive/"screen" effect draws -- enchant auras, glows, weapon trails, the
        // sparkle a buff puts on a character. GW blends these onto the framebuffer with an
        // additive dest factor (DEST=ONE or INVSRCCOLOR), so their black texture background
        // is invisible in-game; a mesh export has no such blending, so they render as solid
        // black panels stuck to the model. Detected from the live D3D blend state (is_effect),
        // so it is pose- and skin-independent. The definitive fix for the "black panels on my
        // capture" class, and the piece that lets require_skinned be turned OFF (so static
        // props/attachments survive) without the effects coming back. 0 = keep them.
        bool drop_effects = false;

        // --- post-capture cleanup (all scopes) --------------------------------
        // GW draws a few effect/billboard quads at large world coordinates while
        // every character sits at the origin; per-chunk extent filtering can't
        // catch them (they are small, just far away) so they blow the export's
        // bounds up and the model renders as a speck. Trim drops chunks whose
        // center is a statistical outlier (>trim_k * MAD on any axis) from the
        // scene's robust center -- adaptive, so a spread-out terrain grab is barely
        // touched but a character-at-origin grab loses only the fliers.
        bool trim_outliers = true;
        float trim_k = 6.0f; // MAD multiplier; larger keeps more, smaller trims harder
        // Filtered scope only: also drop chunks whose center is farther than this
        // from the robust center (world units; 0 = rely on the MAD trim alone).
        float filter_center_radius = 0.f;

        // --- orientation -------------------------------------------------------
        // Which GW model-local axis points "up". GW authors characters Z-up; OBJ/
        // DCC tools and OS 3D previews expect Y-up, so a raw grab looks rotated 90
        // degrees (lying down). The writer remaps this axis to +Y (right-handed)
        // on export. 0 = X, 1 = Y (no-op), 2 = Z (GW default).
        int up_axis = 2;

        // --- probe / calibration ----------------------------------------------
        // Dump the vertex-shader constant registers of the first few skinned draws
        // into the manifest. GW keeps view in c0-c3 and projection in c4-c7 (per
        // GWToolbox's GameWorldCompositor); the per-agent world/bone transform is
        // beyond c7. One probed capture lets us find that register -- its root
        // translation tracks the agent's GWCA world position -- and the
        // world->render scale, the data needed for true per-agent isolation.
        bool probe_shader_constants = false;

        // Diagnostic (Increment 1): log EVERY triangle-list draw's disposition -- kept, or
        // which stage dropped it (2d/dedup/filtered/iso/unreadable) -- with its skinned/
        // textured flags and model-space extents, into the manifest draw_log[]. Lets a
        // never-captured mesh (the bare-skin body) be identified and its killer named from
        // ONE capture. Session-only (not persisted). Off by default (bloats the manifest +
        // costs a per-draw peek); turn on for a diagnostic grab with `set log_draws true`.
        bool log_draws = false;

        // --- per-agent isolation (the real filter) ----------------------------
        // Calibrated 2026-07-01: GW's skinned bone palette lives around c62..c94 as
        // row-major [3x3 | translation.w] triples; the translation equals the
        // agent's GWCA (pos_x,pos_y,pos_z) at scale 1.0 (root c92..c94 matched the
        // target to 1.6 units). So: for a skinned draw, if any bone-triple's
        // translation is within isolate_tolerance of the target's world position,
        // the draw belongs to that agent -- keep it, drop everything else. This
        // separates any agent from any crowd, which extent/count filters cannot.
        // Scanning a register range (not a fixed offset) survives shader changes.
        bool isolate_by_bone = false;
        float isolate_tolerance = 250.f; // world units; < inter-agent spacing, > one agent's bone spread
        // Seeded at capture time from the chosen target's GWCA position (render
        // thread can't safely call GWCA), so the hook can match without locking.
        bool has_match_pos = false;
        float match_pos[3] = {0.f, 0.f, 0.f};

        // --- animation ---------------------------------------------------------
        // GW skins in a vertex shader, so a live grab is the bind/current pose only.
        // Recorded in the manifest for provenance; frame-series export is future work.
        bool capture_pose_note = true;
        // Emit per-vertex bone indices + weights as "#vbld" comment lines in the OBJ (the
        // skinning substrate ModelMod preserves in .mmobj). GW packs 4 bone-palette indices
        // per vertex into a D3DCOLOR; capturing them is the mesh->bone binding needed to
        // re-rig/pose the export later. The bone transforms themselves are in the VS
        // constant palette (enable Probe to dump it). Off => a plain, unskinned OBJ.
        bool export_skin_weights = true;

        // --- pose reconstruction (experimental) --------------------------------
        // Pose the captured BIND-pose skinned body forward into the LIVE frame using the
        // per-draw bone palette (VS constants): world_v = BoneMatrix[vertex_bone] * bind_v, then
        // re-seat to local like the non-skinned pieces. Closes the "close, not exact" gap where a
        // bind-pose body doesn't match GW's live-pose non-skinned armor. Forces probe capture on
        // (it needs the palette). Self-calibrates the palette base register per draw by scanning
        // for the bone triple whose translation matches the agent's GWCA position.
        bool pose_to_live = false;
    };

} // namespace Guildlite
