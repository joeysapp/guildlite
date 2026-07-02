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
        uint32_t draws_trimmed = 0;            // dropped post-capture as locality outliers
        uint32_t vertices = 0;
        uint32_t triangles = 0;
        uint32_t unique_textures = 0;
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
        const CaptureStats& Stats();
    }

} // namespace Guildlite
