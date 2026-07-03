#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GuildliteConfig.h"

struct IDirect3DDevice9;

// ==============================================================================
// Capture -- the D3D9 geometry grabber. Hooks DrawIndexedPrimitive and, for one
// armed frame, decodes every triangle-list draw into a portable MeshChunk. This
// is the shared core of Component 1 (Base geometry) and Component 2 (adds the
// per-chunk texture identity + AABB the Advanced writers/manifest consume).
//
// GW draws its world through vertex shaders and skins in-shader, so the vertex
// buffers hold BIND-POSE, MODEL-LOCAL positions -- exactly what a model export
// wants. We never see a per-agent "world matrix", so scoping to one character is
// done after the fact from the recorded signals (prim/vertex counts, AABB size,
// whether a texture is bound); see CaptureScope::Filtered.
// ==============================================================================

namespace Guildlite {

    // One accepted draw call, decoded to a self-contained mesh (0-based indices).
    struct MeshChunk {
        std::vector<float> positions; // 3 per vertex (model-local space)
        std::vector<float> normals;   // 3 per vertex, or empty if the format had none
        std::vector<float> uvs;       // 2 per vertex, or empty if the format had none
        std::vector<uint32_t> indices; // triangle list, 0-based within this chunk

        uint32_t draw_index = 0; // Nth accepted draw this frame (stable-ish object id)
        uint32_t stride = 0;
        uint32_t fvf = 0;
        bool has_vertex_shader = false;
        bool is_skinned = false; // vertex format carries bone blend weights/indices ->
                                 // an animated character mesh, not static world geometry
        bool alpha_blend = false; // D3DRS_ALPHABLENDENABLE was on -> GW draws this as a
                                  // cutout/translucent piece (hair, cape, feathers), so its
                                  // texture alpha IS opacity. Opaque draws (body/armor) put a
                                  // GLOSS mask in alpha, so map_d must NOT be emitted for them.
        int src_blend = 0;   // D3DRS_SRCBLEND at draw time (D3DBLEND_*); 0 = not recorded
        int dest_blend = 0;  // D3DRS_DESTBLEND at draw time
        bool is_effect = false; // alpha-blended with an ADDITIVE/SCREEN dest factor (DEST=ONE or
                                // INVSRCCOLOR) -> an effect plane (aura/glow/enchant/trail). Its
                                // black texture background is invisible in-game but exports as a
                                // solid black panel. Legit cutouts use DEST=INVSRCALPHA and are
                                // NOT flagged. drop_effects culls these.

        // Stage-0 texture identity. texture_file is filled in later by TextureExport.
        void* texture_ptr = nullptr;
        uint32_t texture_format = 0;
        int texture_w = 0;
        int texture_h = 0;
        std::string texture_file;

        float aabb_min[3] = {0.f, 0.f, 0.f};
        float aabb_max[3] = {0.f, 0.f, 0.f};
    };

    struct CaptureStats {
        uint32_t hook_calls = 0;           // ANY DrawIndexedPrimitive seen while armed (proves the hook fires)
        uint32_t draws_seen = 0;           // triangle-list, depth-tested draws we tried to read
        uint32_t draws_2d_skipped = 0;     // triangle-list draws skipped as 2D/UI (Z-test off)
        uint32_t draws_captured = 0;
        uint32_t draws_skipped_unreadable = 0; // WRITEONLY / unlockable buffers
        uint32_t draws_skipped_filtered = 0;   // rejected by the Filtered heuristics
        uint32_t draws_skipped_isolation = 0;  // skinned, but bone-palette matched a different agent
        uint32_t draws_skipped_effect = 0;     // additive/screen effect planes dropped by drop_effects
        uint32_t draws_trimmed = 0;            // dropped post-capture as locality outliers
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        uint32_t unique_textures = 0;

        // --- Increment 0: draw-path census (which entry point/primitive type do
        // draws actually use?). The bare-skin body is never captured; if GW draws
        // it as a strip/fan, or via a sibling *UP entry point, the TRIANGLELIST-only
        // DrawIndexedPrimitive capture drops it with no trace. These counters (armed
        // frame only) make that visible so the skin-body path can be found in ONE
        // capture. Counters only -- the sibling entry points are not decoded yet.
        uint32_t dip_trianglelist = 0;  // DrawIndexedPrimitive Type==TRIANGLELIST (the path we decode)
        uint32_t dip_trianglestrip = 0; // Type==TRIANGLESTRIP (candidate skin-body path)
        uint32_t dip_trianglefan = 0;   // Type==TRIANGLEFAN   (candidate skin-body path)
        uint32_t dip_other = 0;         // points/lines
        uint32_t dp_calls = 0,    dp_tris = 0;    // DrawPrimitive (vtbl 81): total, triangle-typed
        uint32_t dpup_calls = 0,  dpup_tris = 0;  // DrawPrimitiveUP (vtbl 83)
        uint32_t dipup_calls = 0, dipup_tris = 0; // DrawIndexedPrimitiveUP (vtbl 84)
    };

    // Registers c0..c(kProbeRegCount-1) captured for a skinned draw when
    // Config::probe_shader_constants is on. c0-c3 hold view, c4-c7 projection; the
    // per-agent world/bone transform is somewhere beyond, and this window is dumped
    // to the manifest so the isolation register + world->render scale can be solved
    // offline against the draw's model-local center and the agent's GWCA position.
    constexpr int kProbeRegCount = 96;
    struct ProbeSample {
        uint32_t draw_index = 0;
        float center[3] = {0.f, 0.f, 0.f}; // model-local AABB center of the probed draw
        std::vector<float> regs;           // kProbeRegCount * 4 floats (row-major c0..cN)
    };

    // One triangle-list draw's disposition in the armed frame (Config::log_draws).
    // reason = captured | skip_2d | dedup | filtered | iso | unreadable. ext is the
    // model-space AABB size, populated only when the draw was actually read (0 for
    // draws dropped before ReadChunk). Used to find which stage kills the skin body.
    struct DrawLogEntry {
        uint32_t seq = 0;   // hook_calls index -- draw order within the frame
        uint32_t prims = 0;
        uint32_t verts = 0;
        bool is_skinned = false;  // vertex format carries bone blend weights
        bool has_texture = false; // a stage-0 texture is bound
        bool z_enabled = false;   // depth test on (false => a 2D/UI-classed draw)
        float ext[3] = {0.f, 0.f, 0.f};
        std::string reason;
    };

    enum class CaptureState {
        Idle,    // nothing armed, no data pending
        Waiting, // armed, waiting for the render thread to record a full frame
        Ready,   // a full armed frame is recorded and can be flushed
        Failed,  // armed too long without seeing any triangles
    };

    namespace Capture {
        // Hook / unhook DrawIndexedPrimitive (device vtable[82]). Safe to call once.
        bool Install(IDirect3DDevice9* device);
        void Remove();
        bool IsInstalled();

        // Arm a one-frame capture with a private copy of cfg. The next rendered
        // frame is recorded into Chunks(); poll Advance() to learn when it's done.
        void Arm(const Config& cfg);

        // Call once per plugin Draw (after the world pass). Advances the little
        // state machine and returns the current state.
        CaptureState Advance();

        // Discard any recorded data and return to Idle (call after flushing).
        void Reset();

        // Drop locality-outlier chunks (effect/billboard fliers placed far from the
        // model) using the robust MAD test in cfg. Call once after a frame is Ready
        // and before writing; mutates Chunks() and updates stats.draws_trimmed.
        void TrimOutliers(const Config& cfg);

        std::vector<MeshChunk>& Chunks();
        const std::vector<ProbeSample>& ProbeSamples();
        const std::vector<DrawLogEntry>& DrawLog();
        const CaptureStats& Stats();
    }

} // namespace Guildlite
