#include "Capture.h"

#include <d3d9.h>
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Guildlite {
namespace {

    // ------------------------------------------------------------------ layout
    // Byte offsets (into one vertex) of the attributes we export. -1 = absent.
    struct VertexLayout {
        int pos_offset = -1;
        int normal_offset = -1;
        int uv_offset = -1;
        int uv_type = 0;      // D3DDECLTYPE_* for the uv element (0 => FLOAT2 assumed)
        bool transformed = false; // XYZRHW -> screen space, not a model
        bool has_blend = false;   // bone blend weights/indices present -> skinned character
        // Skinning source, if any (offsets into one vertex; -1 = absent). GW carries bone
        // indices in a D3DCOLOR beta and NO explicit weights; other games use a
        // BLENDINDICES + BLENDWEIGHT element pair. Both are decoded into 4 idx + 4 weights.
        // Indices are read in raw byte order so index slot k pairs with weight slot k; GW's
        // single rigid bone sits in byte 0, which lands in slot 0 with the implicit weight 1
        // (verified against a live capture: byte 0 held the varying bone, bytes 1-3 were 0).
        int blend_idx_offset = -1;    // 4 bone indices (one byte each, raw order)
        int blend_wt_offset = -1;     // explicit per-vertex weights, or -1 if implicit
        int blend_wt_type = 0;        // D3DDECLTYPE_* of the weight element
    };

    float HalfToFloat(const uint16_t h)
    {
        const uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
        uint32_t exp = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x3FF;
        uint32_t f;
        if (exp == 0) {
            if (mant == 0) {
                f = sign;
            }
            else {
                exp = 127 - 15 + 1;
                while ((mant & 0x400) == 0) { mant <<= 1; exp--; }
                mant &= 0x3FF;
                f = sign | (exp << 23) | (mant << 13);
            }
        }
        else if (exp == 0x1F) {
            f = sign | 0x7F800000u | (mant << 13);
        }
        else {
            f = sign | ((exp - 15 + 127) << 23) | (mant << 13);
        }
        float out;
        std::memcpy(&out, &f, sizeof(out));
        return out;
    }

    bool DecodeFromDeclaration(IDirect3DDevice9* device, VertexLayout& out)
    {
        IDirect3DVertexDeclaration9* decl = nullptr;
        if (FAILED(device->GetVertexDeclaration(&decl)) || !decl) {
            return false;
        }
        D3DVERTEXELEMENT9 elems[MAXD3DDECLLENGTH + 1] = {};
        UINT count = 0;
        const bool ok = SUCCEEDED(decl->GetDeclaration(elems, &count));
        decl->Release();
        if (!ok) {
            return false;
        }
        for (UINT i = 0; i < count; ++i) {
            const D3DVERTEXELEMENT9& e = elems[i];
            if (e.Stream == 0xFF) {
                break; // D3DDECL_END
            }
            if (e.Stream != 0) {
                continue; // we only read stream 0
            }
            if (e.Usage == D3DDECLUSAGE_BLENDWEIGHT || e.Usage == D3DDECLUSAGE_BLENDINDICES) {
                out.has_blend = true; // skinned mesh (bones), not static world geometry
            }
            if (e.Usage == D3DDECLUSAGE_BLENDINDICES && e.UsageIndex == 0) {
                out.blend_idx_offset = e.Offset;
            }
            else if (e.Usage == D3DDECLUSAGE_BLENDWEIGHT && e.UsageIndex == 0) {
                out.blend_wt_offset = e.Offset;
                out.blend_wt_type = e.Type;
            }
            if (e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0) {
                if (e.Type == D3DDECLTYPE_FLOAT3 || e.Type == D3DDECLTYPE_FLOAT4) {
                    out.pos_offset = e.Offset;
                }
            }
            else if (e.Usage == D3DDECLUSAGE_NORMAL && e.UsageIndex == 0) {
                if (e.Type == D3DDECLTYPE_FLOAT3) {
                    out.normal_offset = e.Offset;
                }
            }
            else if (e.Usage == D3DDECLUSAGE_TEXCOORD && e.UsageIndex == 0) {
                if (e.Type == D3DDECLTYPE_FLOAT2 || e.Type == D3DDECLTYPE_FLOAT3 ||
                    e.Type == D3DDECLTYPE_FLOAT4 || e.Type == D3DDECLTYPE_FLOAT16_2 ||
                    e.Type == D3DDECLTYPE_FLOAT16_4) {
                    out.uv_offset = e.Offset;
                    out.uv_type = e.Type;
                }
            }
        }
        return out.pos_offset >= 0;
    }

    bool DecodeFromFVF(const DWORD fvf, VertexLayout& out)
    {
        const DWORD pos = fvf & D3DFVF_POSITION_MASK;
        int offset = 0;
        int beta = 0; // XYZB beta DWORD count (0 = unskinned)
        switch (pos) {
            case D3DFVF_XYZ:    offset = 12; break;
            case D3DFVF_XYZRHW: offset = 16; out.transformed = true; break;
            case D3DFVF_XYZW:   offset = 16; break;
            // XYZB* carry per-vertex bone blend weights/indices => a skinned character.
            case D3DFVF_XYZB1:  offset = 12 + 4;  beta = 1; out.has_blend = true; break;
            case D3DFVF_XYZB2:  offset = 12 + 8;  beta = 2; out.has_blend = true; break;
            case D3DFVF_XYZB3:  offset = 12 + 12; beta = 3; out.has_blend = true; break;
            case D3DFVF_XYZB4:  offset = 12 + 16; beta = 4; out.has_blend = true; break;
            case D3DFVF_XYZB5:  offset = 12 + 20; beta = 5; out.has_blend = true; break;
            default: return false;
        }
        out.pos_offset = 0; // xyz is always the first three floats
        if (out.transformed) {
            return true; // screen-space; caller will drop it
        }
        // The beta DWORDs sit at [12, 12+4*beta). If a LASTBETA flag is set the final one
        // is 4 packed bone indices (D3DCOLOR or UBYTE4) and the earlier betas are float
        // weights; otherwise all betas are float weights. GW is XYZB1 + LASTBETA_D3DCOLOR:
        // one beta = 4 D3DCOLOR bone indices, no explicit weights (rigid, shader-applied).
        if (beta > 0) {
            const bool last_color = (fvf & D3DFVF_LASTBETA_D3DCOLOR) != 0;
            const bool last_ubyte = (fvf & D3DFVF_LASTBETA_UBYTE4) != 0;
            const int wt_floats = (last_color || last_ubyte) ? (beta - 1) : beta;
            if (wt_floats > 0) {
                out.blend_wt_offset = 12;
                out.blend_wt_type = D3DDECLTYPE_FLOAT1 + (std::min)(wt_floats, 4) - 1;
            }
            if (last_color || last_ubyte) {
                out.blend_idx_offset = 12 + (beta - 1) * 4; // 4 packed bone indices (raw byte order)
            }
        }
        if (fvf & D3DFVF_NORMAL) { out.normal_offset = offset; offset += 12; }
        if (fvf & D3DFVF_PSIZE) { offset += 4; }
        if (fvf & D3DFVF_DIFFUSE) { offset += 4; }
        if (fvf & D3DFVF_SPECULAR) { offset += 4; }
        const DWORD texcount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
        if (texcount > 0) {
            out.uv_offset = offset;
            out.uv_type = D3DDECLTYPE_FLOAT2;
        }
        return true;
    }

    // --------------------------------------------------------------- dedup key
    struct DrawKey {
        uintptr_t vb;
        uintptr_t ib;
        uint32_t base;
        uint32_t start;
        uint32_t prim;
        uint32_t minv;
        bool operator<(const DrawKey& o) const
        {
            if (vb != o.vb) return vb < o.vb;
            if (ib != o.ib) return ib < o.ib;
            if (base != o.base) return base < o.base;
            if (start != o.start) return start < o.start;
            if (prim != o.prim) return prim < o.prim;
            return minv < o.minv;
        }
        bool operator==(const DrawKey& o) const
        {
            return vb == o.vb && ib == o.ib && base == o.base &&
                   start == o.start && prim == o.prim && minv == o.minv;
        }
    };

    // A CONTENT signature for a draw -- stable across frames, unlike DrawKey. GW recycles the
    // vertex/index BUFFER for a skinned mesh every frame, so DrawKey (which embeds the buffer
    // pointers) mints a fresh identity each frame: the pick list bloated with duplicates, marks
    // and the cursor pointed at last frame's key, and a snap matched nothing. (prim,vert,stride)
    // is fixed by the authored mesh + its vertex format, so the same piece keeps one identity --
    // the same reason ModelMod matches a draw by primitive/vertex count. Collisions (two meshes
    // identical in all three) are possible but rare and, for a manual pick, harmless (both mark
    // together). Used for the pick list, marks, cursor and the snap match set; DrawKey still does
    // the per-frame dedupe where pointer identity is exactly what we want.
    struct PickSig {
        uint32_t prims = 0;
        uint32_t verts = 0;
        uint32_t stride = 0;
        bool operator<(const PickSig& o) const
        {
            if (prims != o.prims) return prims < o.prims;
            if (verts != o.verts) return verts < o.verts;
            return stride < o.stride;
        }
        bool operator==(const PickSig& o) const
        {
            return prims == o.prims && verts == o.verts && stride == o.stride;
        }
    };

    // One entry in the live pick list: a stable content signature plus the metadata the UI shows
    // and a last-seen frame for ageing out despawned/culled draws.
    struct PickEntry {
        PickSig key{};
        uint32_t id = 0;   // stable first-seen ordinal (survives prune/reorder; the UI's #id)
        uint32_t verts = 0;
        uint32_t tris = 0;
        int tex_w = 0;
        int tex_h = 0;
        bool skinned = false;
        unsigned last_seen = 0;
    };

    // ---------------------------------------------------------------- engine
    struct Engine {
        bool installed = false;
        bool armed = false;
        bool recorded = false;
        int frames_since_arm = 0;

        Config cfg;
        std::vector<MeshChunk> chunks;
        std::set<DrawKey> seen;
        std::set<std::pair<uint32_t, uint32_t>> exclude_sigs; // parsed cfg.exclude_list: (tris,verts)
        std::vector<ProbeSample> probes;
        std::vector<DrawLogEntry> draw_log; // per-draw disposition when cfg.log_draws
        CaptureStats stats;

        // --- pick mode (persists across captures; independent of `armed`) ------
        bool pick_active = false;
        bool pick_skinned_only = false;       // list/cycle only skinned (character) draws
        bool pick_include_2d = false;         // also list depth-test-off draws (armor, HUD, ...)
        std::vector<PickEntry> pick_list;     // all pickable draws this session
        std::map<PickSig, size_t> pick_index; // content signature -> index into pick_list
        std::map<PickSig, uint32_t> pick_id_of; // signature -> stable ordinal; persists across a
                                                // prune so a re-seen draw reclaims its old #id
        uint32_t pick_next_id = 0;             // next ordinal to hand out
        std::vector<size_t> pick_filtered;    // indices into pick_list passing the filter (UI view)
        PickSig pick_sel{};                    // navigation cursor (amber preview)
        bool pick_has_sel = false;
        std::set<PickSig> pick_marked;         // draws marked for export (multi-select; green)
        unsigned pick_frame = 0;
        IDirect3DTexture9* pick_highlight = nullptr;        // green tint = marked, lazily created
        IDirect3DTexture9* pick_cursor_highlight = nullptr; // amber tint = cursor, lazily created

        // "Mark all of target": one-shot pass that marks every skinned draw whose bone
        // palette matches a seeded world position (the isolation signal), collecting a whole
        // agent at once regardless of list order. pending -> armed -> consumed over 2 frames
        // (armed in PickCommit, matched by next frame's hook), so it's stable across threads.
        bool pick_mark_pending = false;
        bool pick_mark_armed = false;
        float pick_mark_pos[3] = {0.f, 0.f, 0.f};
        float pick_mark_tol = 250.f;

        // selected-draw capture: set by ArmSelected, honoured in the armed hook path.
        bool capture_selected = false;
        std::set<PickSig> capture_set;         // the content signatures a pick-snap keeps
        std::set<PickSig> capture_done;        // signatures already grabbed this snap -- lets the
                                               // snap span several frames (catch the world pass a
                                               // single EndScene often misses) yet keep each piece
                                               // exactly once (no ghosting across frames)

        void ResetData()
        {
            for (auto& c : chunks) {
                if (c.texture_ptr) {
                    static_cast<IDirect3DTexture9*>(c.texture_ptr)->Release();
                    c.texture_ptr = nullptr;
                }
            }
            chunks.clear();
            seen.clear();
            capture_done.clear();
            probes.clear();
            draw_log.clear();
            stats = CaptureStats{};
        }
    };

    constexpr size_t kDrawLogMax = 1500; // safety cap so a pathological frame can't OOM the log

    // Number of VS constant registers actually captured per probe. Clamped at Install()
    // to the device's MaxVertexShaderConst so GetVertexShaderConstantF never over-reads
    // (a vs_1_1 device caps at 96 and the call would FAIL, losing the palette entirely).
    // Defaults to the historically-proven 96 until Install() has run.
    static int g_probe_reg_count = 96;

    constexpr size_t kProbeMaxSamples = 64; // per-DRAW bone palettes (pose_to_live needs one per
                                            // skinned chunk, not just a few samples); a character
                                            // is ~8-14 skinned draws, 64 covers it with headroom

    constexpr size_t kPickListMax = 1024;  // cap on the live pick list (unique draw signatures)
    // The pick list is a RUNNING WINDOW: an entry survives this many frames after it was last
    // drawn. GW renders some character pieces only intermittently (LOD / alternating passes), so
    // a short window meant a piece flickered out of the list before you could mark it -- and it
    // then never made the snap. A wide window keeps every recently-seen piece markable; "Clear
    // list" resets it when you switch targets. (Content-keyed, so this cannot bloat per frame.)
    constexpr unsigned kPickAgeFrames = 600; // ~10s @60fps / ~20s @30fps
    // A pick snap stays armed up to this many frames, accumulating marked signatures across
    // EndScene passes until it has them all -- one EndScene often lands on a minor render pass
    // (portrait/reflection/UI) and misses the world draws, so a single armed frame captures
    // nothing. Roomy enough to also catch a marked piece that only renders every few frames.
    // Bounded so a stale/off-screen mark can't arm forever.
    constexpr int kPickSnapMaxFrames = 150;
    // A Filtered (single-character) capture accumulates over this many frames so its armed window
    // spans a real WORLD pass, not just a minor EndScene pass (portrait/reflection/UI) -- the
    // "seen=7, captured 0" miss. Content-sig dedupe (capture_done) keeps each piece once across the
    // window, so accumulating frames can't ghost. WholeScene stays single-frame (diagnostic).
    constexpr int kFilteredAccumFrames = 6;

    Engine g_engine;

    typedef HRESULT(__stdcall* DIP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
    DIP_t g_original_dip = nullptr;
    void* g_dip_target = nullptr; // vtable[82]; the address MinHook enable/disable/remove operate on

    // Sibling draw entry points, hooked as COUNTERS ONLY in Increment 0 (no geometry
    // decode). If the missing skin body is issued through one of these instead of
    // DrawIndexedPrimitive, it is invisible to today's capture; counting them (and the
    // triangle-typed subset) reveals the path so it can be decoded in Increment 1.
    typedef HRESULT(__stdcall* DP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
    typedef HRESULT(__stdcall* DPUP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
    typedef HRESULT(__stdcall* DIPUP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT,
                                        const void*, D3DFORMAT, const void*, UINT);
    DP_t g_original_dp = nullptr;
    DPUP_t g_original_dpup = nullptr;
    DIPUP_t g_original_dipup = nullptr;
    void* g_dp_target = nullptr;    // vtable[81] DrawPrimitive
    void* g_dpup_target = nullptr;  // vtable[83] DrawPrimitiveUP
    void* g_dipup_target = nullptr; // vtable[84] DrawIndexedPrimitiveUP

    inline bool IsTriangleType(D3DPRIMITIVETYPE t)
    {
        return t == D3DPT_TRIANGLELIST || t == D3DPT_TRIANGLESTRIP || t == D3DPT_TRIANGLEFAN;
    }

    // Cheaply (no vertex-buffer lock) determine whether the current draw is skinned and
    // whether a stage-0 texture is bound -- for logging draws we drop before ReadChunk
    // (the 2D/dedup paths), so their diagnostic entry still carries these signals.
    bool PeekSkinnedTextured(IDirect3DDevice9* device, bool& has_tex)
    {
        has_tex = false;
        IDirect3DBaseTexture9* t = nullptr;
        if (SUCCEEDED(device->GetTexture(0, &t)) && t) { has_tex = true; t->Release(); }
        DWORD fvf = 0;
        device->GetFVF(&fvf);
        VertexLayout layout;
        if (fvf != 0) { DecodeFromFVF(fvf, layout); }
        else { DecodeFromDeclaration(device, layout); }
        return layout.has_blend;
    }

    // Read the stage-0 texture into `chunk`, taking one ref we release in ResetData.
    void GrabTexture(IDirect3DDevice9* device, MeshChunk& chunk)
    {
        IDirect3DBaseTexture9* base = nullptr;
        if (FAILED(device->GetTexture(0, &base)) || !base) {
            return;
        }
        IDirect3DTexture9* tex2d = nullptr;
        // __uuidof avoids a link dependency on dxguid.lib (IID_IDirect3DTexture9).
        if (SUCCEEDED(base->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&tex2d))) && tex2d) {
            D3DSURFACE_DESC desc = {};
            if (SUCCEEDED(tex2d->GetLevelDesc(0, &desc))) {
                chunk.texture_ptr = tex2d; // keep this ref
                chunk.texture_format = static_cast<uint32_t>(desc.Format);
                chunk.texture_w = static_cast<int>(desc.Width);
                chunk.texture_h = static_cast<int>(desc.Height);
            }
            else {
                tex2d->Release();
            }
        }
        base->Release();
    }

    // Decode one triangle-list draw into a MeshChunk. Returns false if unreadable.
    bool ReadChunk(IDirect3DDevice9* device, INT BaseVertexIndex, UINT MinVertexIndex,
                   UINT NumVertices, UINT startIndex, UINT primCount, MeshChunk& chunk)
    {
        if (NumVertices == 0 || primCount == 0) {
            return false;
        }

        DWORD fvf = 0;
        device->GetFVF(&fvf);
        VertexLayout layout;
        const bool have_layout = (fvf != 0) ? DecodeFromFVF(fvf, layout)
                                            : DecodeFromDeclaration(device, layout);
        if (!have_layout || layout.pos_offset < 0 || layout.transformed) {
            return false; // no usable positions, or screen-space UI geometry
        }

        IDirect3DVertexBuffer9* vb = nullptr;
        UINT stream_offset = 0, stride = 0;
        if (FAILED(device->GetStreamSource(0, &vb, &stream_offset, &stride)) || !vb) {
            return false;
        }
        IDirect3DIndexBuffer9* ib = nullptr;
        if (FAILED(device->GetIndices(&ib)) || !ib) {
            vb->Release();
            return false;
        }

        D3DVERTEXBUFFER_DESC vdesc = {};
        D3DINDEXBUFFER_DESC idesc = {};
        vb->GetDesc(&vdesc);
        ib->GetDesc(&idesc);
        const bool idx32 = (idesc.Format == D3DFMT_INDEX32);
        const size_t idx_size = idx32 ? 4u : 2u;

        const long long first_abs = static_cast<long long>(BaseVertexIndex) + static_cast<long long>(MinVertexIndex);
        bool ok = (stride > 0) && (first_abs >= 0);

        // Bounds-check both buffers before locking so a bad draw can't read OOB.
        int attr_end = layout.pos_offset + 12;
        if (layout.normal_offset >= 0) attr_end = (std::max)(attr_end, layout.normal_offset + 12);
        if (layout.uv_offset >= 0) attr_end = (std::max)(attr_end, layout.uv_offset + 8);
        if (layout.blend_idx_offset >= 0) attr_end = (std::max)(attr_end, layout.blend_idx_offset + 4);
        if (layout.blend_wt_offset >= 0) attr_end = (std::max)(attr_end, layout.blend_wt_offset + 16);
        if (ok) {
            const size_t last_vertex_byte = stream_offset +
                static_cast<size_t>(first_abs + static_cast<long long>(NumVertices) - 1) * stride +
                static_cast<size_t>(attr_end);
            const size_t last_index_byte = (static_cast<size_t>(startIndex) + static_cast<size_t>(primCount) * 3) * idx_size;
            if (last_vertex_byte > vdesc.Size || last_index_byte > idesc.Size) {
                ok = false;
            }
        }

        void* vdata = nullptr;
        void* idata = nullptr;
        if (ok && SUCCEEDED(vb->Lock(0, 0, &vdata, D3DLOCK_READONLY))) {
            if (SUCCEEDED(ib->Lock(0, 0, &idata, D3DLOCK_READONLY))) {
                const uint8_t* vbytes = static_cast<const uint8_t*>(vdata) + stream_offset +
                                        static_cast<size_t>(first_abs) * stride;

                chunk.positions.reserve(NumVertices * 3);
                const bool want_normal = layout.normal_offset >= 0;
                const bool want_uv = layout.uv_offset >= 0;
                const bool want_blend = (layout.blend_idx_offset >= 0 || layout.blend_wt_offset >= 0);
                if (want_blend) {
                    chunk.blend_indices.reserve(static_cast<size_t>(NumVertices) * 4);
                    chunk.blend_weights.reserve(static_cast<size_t>(NumVertices) * 4);
                }
                float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
                for (UINT v = 0; v < NumVertices; ++v) {
                    const uint8_t* vp = vbytes + static_cast<size_t>(v) * stride;
                    const float* p = reinterpret_cast<const float*>(vp + layout.pos_offset);
                    for (int k = 0; k < 3; ++k) {
                        const float val = p[k];
                        chunk.positions.push_back(val);
                        if (v == 0) { mn[k] = mx[k] = val; }
                        else { if (val < mn[k]) mn[k] = val; if (val > mx[k]) mx[k] = val; }
                    }
                    if (want_normal) {
                        const float* n = reinterpret_cast<const float*>(vp + layout.normal_offset);
                        chunk.normals.push_back(n[0]);
                        chunk.normals.push_back(n[1]);
                        chunk.normals.push_back(n[2]);
                    }
                    if (want_uv) {
                        if (layout.uv_type == D3DDECLTYPE_FLOAT16_2 || layout.uv_type == D3DDECLTYPE_FLOAT16_4) {
                            const uint16_t* h = reinterpret_cast<const uint16_t*>(vp + layout.uv_offset);
                            chunk.uvs.push_back(HalfToFloat(h[0]));
                            chunk.uvs.push_back(HalfToFloat(h[1]));
                        }
                        else {
                            const float* t = reinterpret_cast<const float*>(vp + layout.uv_offset);
                            chunk.uvs.push_back(t[0]);
                            chunk.uvs.push_back(t[1]);
                        }
                    }
                    if (want_blend) {
                        uint8_t bi[4] = {0, 0, 0, 0};
                        float bw[4] = {0.f, 0.f, 0.f, 0.f};
                        if (layout.blend_idx_offset >= 0) {
                            const uint8_t* q = vp + layout.blend_idx_offset;
                            bi[0] = q[0]; bi[1] = q[1]; bi[2] = q[2]; bi[3] = q[3];
                        }
                        if (layout.blend_wt_offset >= 0) {
                            const uint8_t* q = vp + layout.blend_wt_offset;
                            const float* f = reinterpret_cast<const float*>(q);
                            switch (layout.blend_wt_type) {
                                case D3DDECLTYPE_FLOAT1: bw[0] = f[0]; break;
                                case D3DDECLTYPE_FLOAT2: bw[0] = f[0]; bw[1] = f[1]; break;
                                case D3DDECLTYPE_FLOAT3:
                                    bw[0] = f[0]; bw[1] = f[1]; bw[2] = f[2];
                                    bw[3] = (std::max)(0.f, 1.f - (bw[0] + bw[1] + bw[2])); // implicit 4th
                                    break;
                                case D3DDECLTYPE_FLOAT4:
                                    bw[0] = f[0]; bw[1] = f[1]; bw[2] = f[2]; bw[3] = f[3]; break;
                                // Weight bytes are read in the same raw order as the indices so
                                // weight slot k stays paired with bone slot k.
                                case D3DDECLTYPE_UBYTE4N:
                                case D3DDECLTYPE_D3DCOLOR:
                                    for (int k = 0; k < 4; ++k) bw[k] = q[k] / 255.f; break;
                                default: bw[0] = 1.f; break;
                            }
                        }
                        else {
                            bw[0] = 1.f; // indices only (GW rigid): full weight on the first bone
                        }
                        for (int k = 0; k < 4; ++k) { chunk.blend_indices.push_back(bi[k]); chunk.blend_weights.push_back(bw[k]); }
                    }
                }
                for (int k = 0; k < 3; ++k) { chunk.aabb_min[k] = mn[k]; chunk.aabb_max[k] = mx[k]; }

                const UINT idx_count = primCount * 3;
                chunk.indices.reserve(idx_count);
                const uint16_t* i16 = reinterpret_cast<const uint16_t*>(idata) + startIndex;
                const uint32_t* i32 = reinterpret_cast<const uint32_t*>(idata) + startIndex;
                for (UINT t = 0; t < primCount; ++t) {
                    uint32_t tri[3];
                    bool valid = true;
                    for (int c = 0; c < 3; ++c) {
                        const uint32_t raw = idx32 ? i32[t * 3 + c] : static_cast<uint32_t>(i16[t * 3 + c]);
                        if (raw < MinVertexIndex || raw >= MinVertexIndex + NumVertices) {
                            valid = false;
                            break;
                        }
                        tri[c] = raw - MinVertexIndex; // rebase to chunk-local
                    }
                    if (valid) {
                        chunk.indices.push_back(tri[0]);
                        chunk.indices.push_back(tri[1]);
                        chunk.indices.push_back(tri[2]);
                    }
                }
                ib->Unlock();
            }
            else {
                ok = false;
            }
            vb->Unlock();
        }
        else {
            ok = false;
        }

        vb->Release();
        ib->Release();

        if (!ok || chunk.positions.empty() || chunk.indices.empty()) {
            return false;
        }
        chunk.stride = stride;
        chunk.fvf = fvf;
        chunk.is_skinned = layout.has_blend;
        return true;
    }

    // Parse a "TRISxVERTS,TRISxVERTS" exclude string into (prims, verts) pairs (e.g. "20x40,154x135").
    void ParseExcludes(const std::string& s, std::set<std::pair<uint32_t, uint32_t>>& out)
    {
        out.clear();
        size_t pos = 0;
        while (pos < s.size()) {
            const size_t comma = s.find(',', pos);
            const std::string tok = s.substr(pos, (comma == std::string::npos ? s.size() : comma) - pos);
            const size_t x = tok.find_first_of("xX*");
            if (x != std::string::npos) {
                const int t = std::atoi(tok.c_str());
                const int v = std::atoi(tok.c_str() + x + 1);
                if (t > 0 && v > 0) out.insert({static_cast<uint32_t>(t), static_cast<uint32_t>(v)});
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }

    bool PassesFilter(const Config& cfg, const MeshChunk& chunk, UINT numVertices, UINT primCount)
    {
        if (cfg.scope != CaptureScope::Filtered) {
            return true;
        }
        // (The manual exclude list is enforced earlier in the hook, before this, so it applies to
        // pick snaps too -- see the exclude_sigs check in DIP_Hook.)
        if (primCount < static_cast<UINT>((std::max)(0, cfg.filter_min_prims))) return false;
        if (cfg.filter_max_prims > 0 && primCount > static_cast<UINT>(cfg.filter_max_prims)) return false;
        if (numVertices < static_cast<UINT>((std::max)(0, cfg.filter_min_verts))) return false;
        if (cfg.require_texture && !chunk.texture_ptr) return false;
        if (cfg.require_skinned && !chunk.is_skinned) return false; // drop static world/props
        if (cfg.filter_max_extent > 0.f || cfg.filter_min_thickness > 0.f) {
            const float ex = chunk.aabb_max[0] - chunk.aabb_min[0];
            const float ey = chunk.aabb_max[1] - chunk.aabb_min[1];
            const float ez = chunk.aabb_max[2] - chunk.aabb_min[2];
            if (cfg.filter_max_extent > 0.f) {
                const float extent = (std::max)(ex, (std::max)(ey, ez));
                if (extent > cfg.filter_max_extent) return false;
            }
            // Reject near-planar draws (billboards/decals/text): thinnest side ~0.
            if (cfg.filter_min_thickness > 0.f) {
                const float thinnest = (std::min)(ex, (std::min)(ey, ez));
                if (thinnest < cfg.filter_min_thickness) return false;
            }
        }
        return true;
    }

    // Per-agent isolation: true if any bone-palette triple's world translation
    // (.w of 3 consecutive VS registers) is within tolerance of the target's GWCA
    // position. Scanning a range (not a fixed offset) survives shader differences;
    // requiring all three .w near the target in 3D makes a spurious match unlikely.
    bool BoneMatchesPos(IDirect3DDevice9* device, const float pos[3], float tol)
    {
        float regs[kProbeRegCount * 4];
        const int nregs = g_probe_reg_count;
        if (FAILED(device->GetVertexShaderConstantF(0, regs, static_cast<UINT>(nregs)))) {
            return false;
        }
        const float tol2 = tol * tol;
        for (int r = 0; r + 2 < nregs; ++r) {
            const float dx = regs[r * 4 + 3] - pos[0];
            const float dy = regs[(r + 1) * 4 + 3] - pos[1];
            const float dz = regs[(r + 2) * 4 + 3] - pos[2];
            if (dx * dx + dy * dy + dz * dz <= tol2) {
                return true;
            }
        }
        return false;
    }

    bool BoneMatchesTarget(IDirect3DDevice9* device, const Config& cfg)
    {
        return BoneMatchesPos(device, cfg.match_pos, cfg.isolate_tolerance);
    }

    // A solid 2x2 tint we swap onto a picked draw's stage-0 texture to highlight it in-game.
    // MANAGED pool so it survives a device Reset. argb is opaque ARGB.
    IDirect3DTexture9* MakeTint(IDirect3DDevice9* device, uint32_t argb)
    {
        IDirect3DTexture9* tex = nullptr;
        if (FAILED(device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex) {
            return nullptr;
        }
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
            for (int y = 0; y < 2; ++y) {
                uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
                for (int x = 0; x < 2; ++x) row[x] = argb;
            }
            tex->UnlockRect(0);
        }
        return tex;
    }

    // Lazily create both pick tints: GREEN = marked (in the export set), AMBER = cursor
    // (Prev/Next focus). Two colours so the states are visually distinct in-game.
    void EnsurePickHighlight(IDirect3DDevice9* device)
    {
        if (!device) return;
        if (!g_engine.pick_highlight)        g_engine.pick_highlight = MakeTint(device, 0xFF00FF00u);        // green
        if (!g_engine.pick_cursor_highlight) g_engine.pick_cursor_highlight = MakeTint(device, 0xFFFFC800u); // amber
    }

    // Upsert one pickable draw into the stable pick list (render thread only). A given
    // signature keeps its slot/index across frames, so the UI selection stays put; only
    // the last-seen frame is refreshed. New signatures append (metadata read once).
    void PickRecord(IDirect3DDevice9* device, const PickSig& key, UINT numVerts, UINT primCount)
    {
        const auto it = g_engine.pick_index.find(key);
        if (it != g_engine.pick_index.end()) {
            g_engine.pick_list[it->second].last_seen = g_engine.pick_frame;
            return;
        }
        if (g_engine.pick_list.size() >= kPickListMax) {
            return;
        }
        PickEntry e;
        e.key = key;
        e.verts = numVerts;
        e.tris = primCount;
        e.last_seen = g_engine.pick_frame;
        bool has_tex = false;
        e.skinned = PeekSkinnedTextured(device, has_tex); // cheap: no VB lock
        if (has_tex) {
            IDirect3DBaseTexture9* base = nullptr;
            if (SUCCEEDED(device->GetTexture(0, &base)) && base) {
                IDirect3DTexture9* t2 = nullptr;
                if (SUCCEEDED(base->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&t2))) && t2) {
                    D3DSURFACE_DESC d{};
                    if (SUCCEEDED(t2->GetLevelDesc(0, &d))) { e.tex_w = static_cast<int>(d.Width); e.tex_h = static_cast<int>(d.Height); }
                    t2->Release();
                }
                base->Release();
            }
        }
        // Stable ordinal: reuse this signature's prior id if it was ever seen (survives an
        // age-out then reappear), else mint the next one -- so #id never renumbers under the user.
        const auto idit = g_engine.pick_id_of.find(key);
        if (idit != g_engine.pick_id_of.end()) { e.id = idit->second; }
        else { e.id = g_engine.pick_next_id++; g_engine.pick_id_of.emplace(key, e.id); }
        g_engine.pick_index.emplace(key, g_engine.pick_list.size());
        g_engine.pick_list.push_back(e);
    }

    HRESULT __stdcall DIP_Hook(IDirect3DDevice9* device, D3DPRIMITIVETYPE Type, INT BaseVertexIndex,
                               UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount)
    {
        if (g_engine.armed) {
            g_engine.stats.hook_calls++; // any primitive: proves MinHook installed our hook
            switch (Type) {              // draw-path census (Increment 0)
                case D3DPT_TRIANGLELIST:  g_engine.stats.dip_trianglelist++;  break;
                case D3DPT_TRIANGLESTRIP: g_engine.stats.dip_trianglestrip++; break;
                case D3DPT_TRIANGLEFAN:   g_engine.stats.dip_trianglefan++;   break;
                default:                  g_engine.stats.dip_other++;         break;
            }
        }
        if (g_engine.armed && Type == D3DPT_TRIANGLELIST && NumVertices > 0 && primCount > 0) {
            // Capture only depth-tested (3D world) draws by default. GW's HUD, the in-game
            // UI and ImGui (our own panel, which renders right after this plugin's Draw in
            // the same frame) all draw with Z-test off -- skipping them keeps the UI out of
            // the model and marks 'recorded' only once the real world pass runs. The toggle
            // exists in case some GW world geometry is (unexpectedly) not depth-tested.
            DWORD zenable = D3DZB_TRUE;
            device->GetRenderState(D3DRS_ZENABLE, &zenable);
            DWORD ablend = FALSE; // alpha-blend on => cutout/translucent piece (texture alpha = opacity)
            device->GetRenderState(D3DRS_ALPHABLENDENABLE, &ablend);
            // Blend factors distinguish an additive/screen EFFECT plane (DEST=ONE/INVSRCCOLOR,
            // black bg invisible in-game -> black panel on export) from a legit alpha cutout
            // (DEST=INVSRCALPHA). Recorded per draw and used by drop_effects.
            DWORD sblend = D3DBLEND_ONE, dblend = D3DBLEND_ZERO;
            device->GetRenderState(D3DRS_SRCBLEND, &sblend);
            device->GetRenderState(D3DRS_DESTBLEND, &dblend);

            // Per-draw diagnostic log (Increment 1): record the disposition of EVERY
            // triangle-list draw so a never-captured mesh's killer is nameable in one grab.
            const bool logging = g_engine.cfg.log_draws && g_engine.draw_log.size() < kDrawLogMax;
            const bool lz = (zenable != D3DZB_FALSE);
            bool lskin = false, lhastex = false;
            if (logging) { lskin = PeekSkinnedTextured(device, lhastex); }
            const auto pushLog = [&](const char* reason, const float* ext) {
                if (!logging) return;
                DrawLogEntry e;
                e.seq = g_engine.stats.hook_calls;
                e.prims = primCount;
                e.verts = NumVertices;
                e.is_skinned = lskin;
                e.has_texture = lhastex;
                e.z_enabled = lz;
                if (ext) { e.ext[0] = ext[0]; e.ext[1] = ext[1]; e.ext[2] = ext[2]; }
                e.reason = reason;
                g_engine.draw_log.push_back(std::move(e));
            };

            if (g_engine.cfg.exclude_2d && zenable == D3DZB_FALSE) {
                g_engine.stats.draws_2d_skipped++;
                pushLog("skip_2d", nullptr);
                return g_original_dip(device, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
            }

            g_engine.recorded = true;
            g_engine.stats.draws_seen++;

            IDirect3DVertexBuffer9* vb = nullptr;
            IDirect3DIndexBuffer9* ib = nullptr;
            UINT so = 0, st = 0;
            device->GetStreamSource(0, &vb, &so, &st);
            device->GetIndices(&ib);
            // DrawKey (pointer-based) is the per-FRAME dedupe identity -- correct here, since a
            // shadow/main re-draw reuses the same buffers within a frame. PickSig (content-based)
            // is the cross-FRAME identity a pick snap matches against, because GW recycles the
            // buffers between frames (see PickSig).
            const DrawKey key{
                reinterpret_cast<uintptr_t>(vb), reinterpret_cast<uintptr_t>(ib),
                static_cast<uint32_t>(BaseVertexIndex), startIndex, primCount, MinVertexIndex};
            const PickSig sig{primCount, NumVertices, static_cast<uint32_t>(st)};
            if (vb) vb->Release();
            if (ib) ib->Release();

            // Global "never-export" list wins over EVERYTHING -- the size filter and a pick snap's
            // marks alike. So you can [x] a plant globally, "mark all in view", snap, and still get
            // only the non-excluded pieces.
            if (!g_engine.exclude_sigs.empty() &&
                g_engine.exclude_sigs.count({primCount, NumVertices}) != 0) {
                return g_original_dip(device, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
            }
            // Selected-draw capture (pick snap): grab ONLY the marked signatures; every other
            // draw is passed straight through, so no filter stack is needed. capture_done skips a
            // marked piece already grabbed on an earlier frame of this (multi-frame) snap.
            if (g_engine.capture_selected && !g_engine.capture_set.count(sig)) {
                return g_original_dip(device, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
            }
            // Content-sig dedupe for pick snaps AND Filtered captures -- both accumulate across
            // frames, so the same piece re-drawn next frame (with a recycled buffer -> new DrawKey)
            // must not be captured twice. WholeScene keeps DrawKey-only dedupe (distinct meshes can
            // share a prim/vert/stride sig, and there we want them all).
            const bool sig_dedupe = g_engine.capture_selected || g_engine.cfg.scope == CaptureScope::Filtered;
            const bool sig_done = sig_dedupe && g_engine.capture_done.count(sig) != 0;
            const bool duplicate = sig_done || (g_engine.cfg.dedupe && !g_engine.seen.insert(key).second);
            if (!duplicate) {
                MeshChunk chunk;
                chunk.draw_index = g_engine.stats.draws_captured;
                IDirect3DVertexShader9* vs = nullptr;
                if (SUCCEEDED(device->GetVertexShader(&vs))) {
                    chunk.has_vertex_shader = (vs != nullptr);
                    if (vs) vs->Release();
                }
                const bool want_tex = (g_engine.cfg.detail == DetailLevel::Advanced && g_engine.cfg.export_textures) ||
                                      (g_engine.cfg.scope == CaptureScope::Filtered && g_engine.cfg.require_texture);
                if (want_tex) {
                    GrabTexture(device, chunk);
                }
                if (ReadChunk(device, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount, chunk)) {
                    const float ext[3] = {chunk.aabb_max[0] - chunk.aabb_min[0],
                                          chunk.aabb_max[1] - chunk.aabb_min[1],
                                          chunk.aabb_max[2] - chunk.aabb_min[2]};
                    chunk.alpha_blend = (ablend != FALSE);
                    chunk.src_blend = static_cast<int>(sblend);
                    chunk.dest_blend = static_cast<int>(dblend);
                    // Additive (DEST=ONE) or screen (DEST=INVSRCCOLOR) blending => an effect
                    // plane (aura/glow/enchant/trail), not model geometry. Legit cutouts
                    // (hair/cape/feather) use DEST=INVSRCALPHA and are left alone.
                    chunk.is_effect = (ablend != FALSE) &&
                        (dblend == D3DBLEND_ONE || dblend == D3DBLEND_INVSRCCOLOR);
                    // A selected-draw capture keeps exactly the picked draw -- the pick was
                    // the filter, so PassesFilter/drop_effects/isolation are all bypassed.
                    bool keep = g_engine.capture_selected ? true
                                    : PassesFilter(g_engine.cfg, chunk, NumVertices, primCount);
                    bool effect_drop = false;
                    if (!g_engine.capture_selected && keep && g_engine.cfg.drop_effects && chunk.is_effect) {
                        keep = false; // additive/screen effect plane -- renders as a black panel
                        effect_drop = true;
                    }
                    bool isolation_drop = false;
                    if (!g_engine.capture_selected && keep && g_engine.cfg.isolate_by_bone && g_engine.cfg.has_match_pos &&
                        chunk.has_vertex_shader && !BoneMatchesTarget(device, g_engine.cfg)) {
                        keep = false; // skinned, but belongs to a different agent
                        isolation_drop = true;
                    }
                    if (keep) {
                        if (sig_dedupe) g_engine.capture_done.insert(sig); // grabbed this window
                        g_engine.stats.draws_captured++;
                        g_engine.stats.vertices += static_cast<uint32_t>(chunk.positions.size() / 3);
                        g_engine.stats.triangles += static_cast<uint32_t>(chunk.indices.size() / 3);
                        // Probe: snapshot the VS constant window of the first few skinned
                        // draws (agents) so the bone-palette register can be solved offline.
                        if (g_engine.cfg.probe_shader_constants && chunk.has_vertex_shader &&
                            g_engine.probes.size() < kProbeMaxSamples) {
                            ProbeSample ps;
                            ps.draw_index = chunk.draw_index;
                            for (int k = 0; k < 3; ++k) {
                                ps.center[k] = (chunk.aabb_min[k] + chunk.aabb_max[k]) * 0.5f;
                            }
                            ps.regs.resize(static_cast<size_t>(g_probe_reg_count) * 4, 0.f);
                            if (SUCCEEDED(device->GetVertexShaderConstantF(0, ps.regs.data(),
                                                                           static_cast<UINT>(g_probe_reg_count)))) {
                                g_engine.probes.push_back(std::move(ps));
                            }
                        }
                        pushLog("captured", ext);
                        g_engine.chunks.push_back(std::move(chunk));
                    }
                    else {
                        if (effect_drop) g_engine.stats.draws_skipped_effect++;
                        else if (isolation_drop) g_engine.stats.draws_skipped_isolation++;
                        else g_engine.stats.draws_skipped_filtered++;
                        pushLog(effect_drop ? "effect" : (isolation_drop ? "iso" : "filtered"), ext);
                        if (chunk.texture_ptr) static_cast<IDirect3DTexture9*>(chunk.texture_ptr)->Release();
                    }
                }
                else {
                    g_engine.stats.draws_skipped_unreadable++;
                    pushLog("unreadable", nullptr);
                    if (chunk.texture_ptr) static_cast<IDirect3DTexture9*>(chunk.texture_ptr)->Release();
                }
            }
            else {
                pushLog("dedup", nullptr);
            }
        }

        // --- Pick mode: enumerate pickable draws + tint the selected one --------
        // Runs every frame, independent of `armed`. Depth-tested triangle lists only
        // (the "world geometry" gate), but WITHOUT the capture filter stack, so props
        // and small objects are pickable too. Selected draw is drawn with a green
        // stage-0 texture (restored immediately) so it stands out in-game.
        if (g_engine.pick_active && Type == D3DPT_TRIANGLELIST && NumVertices > 0 && primCount > 0) {
            DWORD zenable = D3DZB_TRUE;
            device->GetRenderState(D3DRS_ZENABLE, &zenable);
            // Depth-tested world draws by default; opt in to z-off draws to reach geometry GW
            // renders with the depth test off (e.g. some armor) at the cost of HUD/UI noise.
            if (g_engine.pick_include_2d || zenable != D3DZB_FALSE) {
                IDirect3DVertexBuffer9* pvb = nullptr;
                UINT pso = 0, pst = 0;
                device->GetStreamSource(0, &pvb, &pso, &pst);
                const PickSig pk{primCount, NumVertices, static_cast<uint32_t>(pst)};
                if (pvb) pvb->Release();
                PickRecord(device, pk, NumVertices, primCount);
                // "Mark all of target": auto-mark skinned draws whose bone palette matches the
                // seeded agent position (one-shot pass, armed for this frame by PickCommit).
                if (g_engine.pick_mark_armed) {
                    const auto pit = g_engine.pick_index.find(pk);
                    if (pit != g_engine.pick_index.end() && g_engine.pick_list[pit->second].skinned &&
                        BoneMatchesPos(device, g_engine.pick_mark_pos, g_engine.pick_mark_tol)) {
                        g_engine.pick_marked.insert(pk);
                    }
                }
                // Two distinct tints so the states are never confused: GREEN = marked (in the
                // export set), AMBER = cursor only (Prev/Next focus). Mark wins when a draw is
                // both. This is what makes "click a green row to deselect it" work -- unmarking
                // drops the draw to amber (still the cursor) instead of leaving it green.
                const bool marked = g_engine.pick_marked.count(pk) != 0;
                const bool is_cursor = g_engine.pick_has_sel && pk == g_engine.pick_sel;
                IDirect3DTexture9* tint = marked ? g_engine.pick_highlight
                                        : (is_cursor ? g_engine.pick_cursor_highlight : nullptr);
                if (tint) {
                    IDirect3DBaseTexture9* saved = nullptr;
                    device->GetTexture(0, &saved);
                    device->SetTexture(0, tint);
                    const HRESULT r = g_original_dip(device, Type, BaseVertexIndex, MinVertexIndex,
                                                     NumVertices, startIndex, primCount);
                    device->SetTexture(0, saved);
                    if (saved) saved->Release();
                    return r;
                }
            }
        }
        return g_original_dip(device, Type, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
    }

    // --- sibling entry points: count-only (Increment 0) -----------------------
    HRESULT __stdcall DP_Hook(IDirect3DDevice9* device, D3DPRIMITIVETYPE Type, UINT StartVertex, UINT PrimitiveCount)
    {
        if (g_engine.armed) {
            g_engine.stats.dp_calls++;
            if (IsTriangleType(Type)) g_engine.stats.dp_tris++;
        }
        return g_original_dp(device, Type, StartVertex, PrimitiveCount);
    }

    HRESULT __stdcall DPUP_Hook(IDirect3DDevice9* device, D3DPRIMITIVETYPE Type, UINT PrimitiveCount,
                                const void* pVertexStreamZeroData, UINT VertexStreamZeroStride)
    {
        if (g_engine.armed) {
            g_engine.stats.dpup_calls++;
            if (IsTriangleType(Type)) g_engine.stats.dpup_tris++;
        }
        return g_original_dpup(device, Type, PrimitiveCount, pVertexStreamZeroData, VertexStreamZeroStride);
    }

    HRESULT __stdcall DIPUP_Hook(IDirect3DDevice9* device, D3DPRIMITIVETYPE Type, UINT MinVertexIndex,
                                 UINT NumVertices, UINT PrimitiveCount, const void* pIndexData,
                                 D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData,
                                 UINT VertexStreamZeroStride)
    {
        if (g_engine.armed) {
            g_engine.stats.dipup_calls++;
            if (IsTriangleType(Type)) g_engine.stats.dipup_tris++;
        }
        return g_original_dipup(device, Type, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData,
                                IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
    }

} // namespace

namespace Capture {

    bool Install(IDirect3DDevice9* device)
    {
        if (g_engine.installed || !device) {
            return g_engine.installed;
        }
        // This plugin links its OWN (static) MinHook; GWToolbox/GWCA initialising theirs
        // does not initialise ours. Without this the CreateHook below silently no-ops and
        // the hook never fires -- the cause of "no geometry captured".
        const MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            return false;
        }
        void** vtable = *reinterpret_cast<void***>(device);
        g_dip_target = vtable[82];
        if (MH_CreateHook(g_dip_target, reinterpret_cast<void*>(&DIP_Hook),
                          reinterpret_cast<void**>(&g_original_dip)) != MH_OK) {
            g_dip_target = nullptr;
            return false;
        }
        if (MH_EnableHook(g_dip_target) != MH_OK) {
            MH_RemoveHook(g_dip_target);
            g_dip_target = nullptr;
            return false;
        }
        // Sibling entry points -- best-effort counters (Increment 0). A failure here
        // must never break the primary DIP capture, so we null the target and move on.
        g_dp_target = vtable[81];
        if (MH_CreateHook(g_dp_target, reinterpret_cast<void*>(&DP_Hook),
                          reinterpret_cast<void**>(&g_original_dp)) != MH_OK ||
            MH_EnableHook(g_dp_target) != MH_OK) {
            g_dp_target = nullptr;
        }
        g_dpup_target = vtable[83];
        if (MH_CreateHook(g_dpup_target, reinterpret_cast<void*>(&DPUP_Hook),
                          reinterpret_cast<void**>(&g_original_dpup)) != MH_OK ||
            MH_EnableHook(g_dpup_target) != MH_OK) {
            g_dpup_target = nullptr;
        }
        g_dipup_target = vtable[84];
        if (MH_CreateHook(g_dipup_target, reinterpret_cast<void*>(&DIPUP_Hook),
                          reinterpret_cast<void**>(&g_original_dipup)) != MH_OK ||
            MH_EnableHook(g_dipup_target) != MH_OK) {
            g_dipup_target = nullptr;
        }
        EnsurePickHighlight(device); // green tint for pick-mode highlighting
        // Size the probe window to the whole VS constant file so the full bone palette is
        // captured (SM2/3 devices expose 256; a too-small window truncates the palette and
        // freezes out-of-window bones -- the lower body -- at bind pose). Never read fewer
        // than the proven 96, never more than the buffer cap; failure leaves the 96 default.
        D3DCAPS9 caps;
        if (SUCCEEDED(device->GetDeviceCaps(&caps)) && caps.MaxVertexShaderConst > 0) {
            int cap = static_cast<int>(caps.MaxVertexShaderConst);
            if (cap < 96) cap = 96;
            if (cap > kProbeRegCount) cap = kProbeRegCount;
            g_probe_reg_count = cap;
        }
        g_engine.installed = true;
        return true;
    }

    void Remove()
    {
        if (!g_engine.installed) {
            return;
        }
        g_engine.armed = false;
        if (g_dip_target) {
            MH_DisableHook(g_dip_target);
            MH_RemoveHook(g_dip_target);
            g_dip_target = nullptr;
        }
        if (g_dp_target)    { MH_DisableHook(g_dp_target);    MH_RemoveHook(g_dp_target);    g_dp_target = nullptr; }
        if (g_dpup_target)  { MH_DisableHook(g_dpup_target);  MH_RemoveHook(g_dpup_target);  g_dpup_target = nullptr; }
        if (g_dipup_target) { MH_DisableHook(g_dipup_target); MH_RemoveHook(g_dipup_target); g_dipup_target = nullptr; }
        g_engine.pick_active = false;
        g_engine.pick_has_sel = false;
        g_engine.pick_list.clear();
        g_engine.pick_index.clear();
        g_engine.pick_id_of.clear();
        g_engine.pick_next_id = 0;
        g_engine.pick_filtered.clear();
        g_engine.pick_marked.clear();
        g_engine.capture_set.clear();
        if (g_engine.pick_highlight) { g_engine.pick_highlight->Release(); g_engine.pick_highlight = nullptr; }
        if (g_engine.pick_cursor_highlight) { g_engine.pick_cursor_highlight->Release(); g_engine.pick_cursor_highlight = nullptr; }
        g_engine.ResetData();
        g_engine.installed = false;
    }

    bool IsInstalled() { return g_engine.installed; }

    void Arm(const Config& cfg)
    {
        g_engine.ResetData();
        g_engine.cfg = cfg;
        ParseExcludes(g_engine.cfg.exclude_list, g_engine.exclude_sigs);
        g_engine.armed = true;
        g_engine.recorded = false;
        g_engine.frames_since_arm = 0;
        g_engine.capture_selected = false; // a normal, filtered capture
        g_engine.capture_set.clear();
    }

    CaptureState Advance()
    {
        if (!g_engine.armed) {
            return g_engine.chunks.empty() ? CaptureState::Idle : CaptureState::Ready;
        }
        g_engine.frames_since_arm++;

        // Tally unique textures, disarm, and report Ready (or Failed if nothing was captured).
        const auto finish = [&]() -> CaptureState {
            g_engine.armed = false;
            std::set<void*> tex;
            for (const auto& c : g_engine.chunks) {
                if (c.texture_ptr) tex.insert(c.texture_ptr);
            }
            g_engine.stats.unique_textures = static_cast<uint32_t>(tex.size());
            return g_engine.chunks.empty() ? CaptureState::Failed : CaptureState::Ready;
        };

        // Pick snap: keep accumulating across EndScene passes until every marked signature is
        // grabbed, or we time out. A single armed frame frequently lands on a minor render pass
        // (portrait/reflection/UI) that misses the world draws entirely, so one frame captures
        // nothing -- this window is what makes the snap reliable.
        if (g_engine.capture_selected) {
            const bool got_all = g_engine.capture_done.size() >= g_engine.capture_set.size();
            const bool timed_out = g_engine.frames_since_arm > kPickSnapMaxFrames;
            return (got_all || timed_out) ? finish() : CaptureState::Waiting;
        }

        // Filtered (single-character) capture: accumulate a few frames so the window spans a real
        // world pass, not a minor EndScene pass (the "seen=7" miss). Sig-deduped, so no ghosting.
        if (g_engine.cfg.scope == CaptureScope::Filtered) {
            if (g_engine.recorded && g_engine.frames_since_arm >= kFilteredAccumFrames) return finish();
            if (g_engine.frames_since_arm > kFilteredAccumFrames + 5 && !g_engine.recorded) {
                g_engine.armed = false;
                return CaptureState::Failed;
            }
            return CaptureState::Waiting;
        }

        // WholeScene: single frame (diagnostic baseline).
        if (g_engine.recorded && g_engine.frames_since_arm >= 1) {
            return finish();
        }
        if (g_engine.frames_since_arm > 4 && !g_engine.recorded) {
            g_engine.armed = false;
            return CaptureState::Failed;
        }
        return CaptureState::Waiting;
    }

    void Reset() { g_engine.ResetData(); g_engine.armed = false; g_engine.recorded = false; }

    void TrimOutliers(const Config& cfg)
    {
        auto& chunks = g_engine.chunks;
        // Two independent post-capture culls share the robust scene center: the MAD
        // outlier trim (all scopes) and the Filtered-only center-radius cap. The
        // radius used to live *inside* the trim and silently did nothing whenever
        // "Trim outlier fliers" was off -- run whichever the user actually enabled.
        const bool radius_on = cfg.scope == CaptureScope::Filtered && cfg.filter_center_radius > 0.f;
        if ((!cfg.trim_outliers && !radius_on) || chunks.size() < 4) {
            return; // nothing active, or too few chunks for a meaningful robust center
        }

        const auto median = [](std::vector<float> v) -> float {
            if (v.empty()) return 0.f;
            const size_t mid = v.size() / 2;
            std::nth_element(v.begin(), v.begin() + mid, v.end());
            return v[mid];
        };

        std::vector<std::array<float, 3>> centers(chunks.size());
        for (size_t i = 0; i < chunks.size(); ++i) {
            for (int k = 0; k < 3; ++k) {
                centers[i][k] = (chunks[i].aabb_min[k] + chunks[i].aabb_max[k]) * 0.5f;
            }
        }

        float med[3], mad[3];
        for (int k = 0; k < 3; ++k) {
            std::vector<float> axis(centers.size());
            for (size_t i = 0; i < centers.size(); ++i) axis[i] = centers[i][k];
            med[k] = median(axis);
            for (size_t i = 0; i < axis.size(); ++i) axis[i] = std::fabs(axis[i] - med[k]);
            // Floor the deviation scale so a perfectly-aligned axis (e.g. x~0 for a
            // centered character) doesn't flag every sibling chunk as an outlier.
            mad[k] = (std::max)(median(axis), 5.0f);
        }

        const float k_mult = (cfg.trim_k > 0.f) ? cfg.trim_k : 6.0f;
        const float radius2 = cfg.filter_center_radius * cfg.filter_center_radius;

        std::vector<MeshChunk> kept;
        kept.reserve(chunks.size());
        for (size_t i = 0; i < chunks.size(); ++i) {
            bool outlier = false;
            if (cfg.trim_outliers) {
                for (int k = 0; k < 3; ++k) {
                    if (std::fabs(centers[i][k] - med[k]) > k_mult * mad[k]) { outlier = true; break; }
                }
            }
            if (!outlier && radius_on) {
                float d2 = 0.f;
                for (int k = 0; k < 3; ++k) { const float d = centers[i][k] - med[k]; d2 += d * d; }
                if (d2 > radius2) outlier = true;
            }
            if (outlier) {
                g_engine.stats.draws_trimmed++;
                if (g_engine.stats.vertices >= chunks[i].positions.size() / 3)
                    g_engine.stats.vertices -= static_cast<uint32_t>(chunks[i].positions.size() / 3);
                if (g_engine.stats.triangles >= chunks[i].indices.size() / 3)
                    g_engine.stats.triangles -= static_cast<uint32_t>(chunks[i].indices.size() / 3);
                if (chunks[i].texture_ptr) {
                    static_cast<IDirect3DTexture9*>(chunks[i].texture_ptr)->Release();
                    chunks[i].texture_ptr = nullptr;
                }
            }
            else {
                kept.push_back(std::move(chunks[i]));
            }
        }
        chunks.swap(kept);
    }

    // --- pick mode (render thread only) -----------------------------------
    void PickSetActive(bool on) { g_engine.pick_active = on; }
    bool PickActive() { return g_engine.pick_active; }

    void PickCommit()
    {
        if (!g_engine.pick_active) {
            return;
        }
        // Age out draws not seen for a while (despawned / culled), then advance the frame.
        const unsigned now = g_engine.pick_frame;
        bool pruned = false;
        std::vector<PickEntry> kept;
        kept.reserve(g_engine.pick_list.size());
        for (const auto& e : g_engine.pick_list) {
            if (now - e.last_seen <= kPickAgeFrames) kept.push_back(e);
            else pruned = true;
        }
        if (pruned) {
            g_engine.pick_list.swap(kept);
            g_engine.pick_index.clear();
            for (size_t i = 0; i < g_engine.pick_list.size(); ++i) {
                g_engine.pick_index.emplace(g_engine.pick_list[i].key, i);
            }
        }
        // Rebuild the filtered view the UI pages through (skinned-only cuts the scene's
        // ~hundreds of draws down to just the characters).
        g_engine.pick_filtered.clear();
        g_engine.pick_filtered.reserve(g_engine.pick_list.size());
        for (size_t i = 0; i < g_engine.pick_list.size(); ++i) {
            if (!g_engine.pick_skinned_only || g_engine.pick_list[i].skinned) {
                g_engine.pick_filtered.push_back(i);
            }
        }
        // Present the view in stable ordinal order so rows never shuffle under the user, even
        // after a prune compacts pick_list or a draw ages out and later reappends.
        std::sort(g_engine.pick_filtered.begin(), g_engine.pick_filtered.end(),
                  [](size_t a, size_t b) { return g_engine.pick_list[a].id < g_engine.pick_list[b].id; });
        // Arm the "mark all of target" pass for exactly the next frame's hook.
        g_engine.pick_mark_armed = g_engine.pick_mark_pending;
        g_engine.pick_mark_pending = false;
        g_engine.pick_frame++;
    }

    void PickMarkMatching(const float* pos, float tol)
    {
        g_engine.pick_mark_pos[0] = pos[0];
        g_engine.pick_mark_pos[1] = pos[1];
        g_engine.pick_mark_pos[2] = pos[2];
        g_engine.pick_mark_tol = (tol > 0.f) ? tol : 250.f;
        g_engine.pick_mark_pending = true; // consumed by PickCommit -> next frame's hook
    }

    void PickSetSkinnedOnly(bool on) { g_engine.pick_skinned_only = on; }
    bool PickSkinnedOnly() { return g_engine.pick_skinned_only; }
    void PickSetInclude2D(bool on) { g_engine.pick_include_2d = on; }
    bool PickInclude2D() { return g_engine.pick_include_2d; }

    int PickCount() { return static_cast<int>(g_engine.pick_filtered.size()); }

    int PickIndex() // position of the selection within the filtered view, or -1
    {
        if (!g_engine.pick_has_sel) return -1;
        const auto it = g_engine.pick_index.find(g_engine.pick_sel);
        if (it == g_engine.pick_index.end()) return -1;
        for (size_t fi = 0; fi < g_engine.pick_filtered.size(); ++fi) {
            if (g_engine.pick_filtered[fi] == it->second) return static_cast<int>(fi);
        }
        return -1; // selected draw is filtered out of the current view
    }

    void PickSelect(int index)
    {
        if (index < 0 || index >= static_cast<int>(g_engine.pick_filtered.size())) return;
        g_engine.pick_sel = g_engine.pick_list[g_engine.pick_filtered[index]].key;
        g_engine.pick_has_sel = true;
    }

    void PickCycle(int delta)
    {
        const int n = static_cast<int>(g_engine.pick_filtered.size());
        if (n == 0) { g_engine.pick_has_sel = false; return; }
        const int cur = PickIndex();
        int next;
        if (cur < 0) next = (delta >= 0) ? 0 : n - 1;
        else { next = cur + delta; next = ((next % n) + n) % n; } // wrap
        PickSelect(next);
    }

    PickInfo PickRow(int index)
    {
        PickInfo r;
        if (index >= 0 && index < static_cast<int>(g_engine.pick_filtered.size())) {
            const PickEntry& e = g_engine.pick_list[g_engine.pick_filtered[index]];
            r.id = e.id;
            r.verts = e.verts; r.tris = e.tris; r.tex_w = e.tex_w; r.tex_h = e.tex_h; r.skinned = e.skinned;
        }
        return r;
    }

    // Filter-independent: a hidden-by-filter selection can still be snapped/highlighted.
    bool HasSelection()
    {
        return g_engine.pick_has_sel &&
               g_engine.pick_index.find(g_engine.pick_sel) != g_engine.pick_index.end();
    }

    // --- multi-select marks (the export set) --------------------------------
    int PickMarkedCount() { return static_cast<int>(g_engine.pick_marked.size()); }

    bool PickRowMarked(int index)
    {
        if (index < 0 || index >= static_cast<int>(g_engine.pick_filtered.size())) return false;
        return g_engine.pick_marked.count(g_engine.pick_list[g_engine.pick_filtered[index]].key) != 0;
    }

    void PickToggleMarkRow(int index)
    {
        if (index < 0 || index >= static_cast<int>(g_engine.pick_filtered.size())) return;
        const PickSig k = g_engine.pick_list[g_engine.pick_filtered[index]].key;
        const auto it = g_engine.pick_marked.find(k);
        if (it == g_engine.pick_marked.end()) g_engine.pick_marked.insert(k);
        else g_engine.pick_marked.erase(it);
    }

    void PickToggleMark() // toggle the current cursor draw in/out of the export set
    {
        if (!g_engine.pick_has_sel) return;
        const auto it = g_engine.pick_marked.find(g_engine.pick_sel);
        if (it == g_engine.pick_marked.end()) g_engine.pick_marked.insert(g_engine.pick_sel);
        else g_engine.pick_marked.erase(it);
    }

    void PickClearMarks() { g_engine.pick_marked.clear(); }

    void PickClearList()
    {
        // Reset the running-window list when switching targets, so stale pieces from a previous
        // agent/scene stop cluttering it. Marks and the cursor survive (keyed by content sig): a
        // still-drawn marked piece simply re-appears and re-highlights, and stays snappable.
        g_engine.pick_list.clear();
        g_engine.pick_index.clear();
        g_engine.pick_id_of.clear();
        g_engine.pick_next_id = 0;
        g_engine.pick_filtered.clear();
    }

    void PickMarkAllFiltered()
    {
        // Mark every draw currently in the filtered view (exactly what the UI lists). With
        // "Skinned only" on this is "mark all characters"; a fast alternative to clicking each
        // row. Snap then exports the union of the marks.
        for (const size_t idx : g_engine.pick_filtered) {
            g_engine.pick_marked.insert(g_engine.pick_list[idx].key);
        }
    }

    void ArmSelected(const Config& cfg)
    {
        // Export the marked set; if nothing is marked, fall back to the cursor draw so a
        // quick single snap still works without explicitly marking.
        std::set<PickSig> set = g_engine.pick_marked;
        if (set.empty() && HasSelection()) set.insert(g_engine.pick_sel);
        if (set.empty()) return;
        g_engine.ResetData();          // clears chunks/stats/capture_done; pick list + marks survive
        g_engine.cfg = cfg;
        ParseExcludes(g_engine.cfg.exclude_list, g_engine.exclude_sigs);
        g_engine.armed = true;
        g_engine.recorded = false;
        g_engine.frames_since_arm = 0;
        g_engine.capture_selected = true;
        g_engine.capture_set = std::move(set);
    }

    std::vector<MeshChunk>& Chunks() { return g_engine.chunks; }
    const std::vector<ProbeSample>& ProbeSamples() { return g_engine.probes; }
    int ProbeRegCount() { return g_probe_reg_count; }
    const std::vector<DrawLogEntry>& DrawLog() { return g_engine.draw_log; }
    const CaptureStats& Stats() { return g_engine.stats; }

} // namespace Capture
} // namespace Guildlite
