#include "Capture.h"

#include <d3d9.h>
#include <MinHook.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <set>
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
        switch (pos) {
            case D3DFVF_XYZ:    offset = 12; break;
            case D3DFVF_XYZRHW: offset = 16; out.transformed = true; break;
            case D3DFVF_XYZW:   offset = 16; break;
            // XYZB* carry per-vertex bone blend weights/indices => a skinned character.
            case D3DFVF_XYZB1:  offset = 12 + 4;  out.has_blend = true; break;
            case D3DFVF_XYZB2:  offset = 12 + 8;  out.has_blend = true; break;
            case D3DFVF_XYZB3:  offset = 12 + 12; out.has_blend = true; break;
            case D3DFVF_XYZB4:  offset = 12 + 16; out.has_blend = true; break;
            case D3DFVF_XYZB5:  offset = 12 + 20; out.has_blend = true; break;
            default: return false;
        }
        out.pos_offset = 0; // xyz is always the first three floats
        if (out.transformed) {
            return true; // screen-space; caller will drop it
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
        std::vector<ProbeSample> probes;
        std::vector<DrawLogEntry> draw_log; // per-draw disposition when cfg.log_draws
        CaptureStats stats;

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
            probes.clear();
            draw_log.clear();
            stats = CaptureStats{};
        }
    };

    constexpr size_t kDrawLogMax = 1500; // safety cap so a pathological frame can't OOM the log

    constexpr size_t kProbeMaxSamples = 6; // enough distinct agents to solve the isolation register

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

    bool PassesFilter(const Config& cfg, const MeshChunk& chunk, UINT numVertices, UINT primCount)
    {
        if (cfg.scope != CaptureScope::Filtered) {
            return true;
        }
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
    bool BoneMatchesTarget(IDirect3DDevice9* device, const Config& cfg)
    {
        float regs[kProbeRegCount * 4];
        if (FAILED(device->GetVertexShaderConstantF(0, regs, static_cast<UINT>(kProbeRegCount)))) {
            return false;
        }
        const float tol2 = cfg.isolate_tolerance * cfg.isolate_tolerance;
        for (int r = 0; r + 2 < kProbeRegCount; ++r) {
            const float dx = regs[r * 4 + 3] - cfg.match_pos[0];
            const float dy = regs[(r + 1) * 4 + 3] - cfg.match_pos[1];
            const float dz = regs[(r + 2) * 4 + 3] - cfg.match_pos[2];
            if (dx * dx + dy * dy + dz * dz <= tol2) {
                return true;
            }
        }
        return false;
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
            const DrawKey key{
                reinterpret_cast<uintptr_t>(vb), reinterpret_cast<uintptr_t>(ib),
                static_cast<uint32_t>(BaseVertexIndex), startIndex, primCount, MinVertexIndex};
            if (vb) vb->Release();
            if (ib) ib->Release();

            const bool duplicate = g_engine.cfg.dedupe && !g_engine.seen.insert(key).second;
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
                    bool keep = PassesFilter(g_engine.cfg, chunk, NumVertices, primCount);
                    bool effect_drop = false;
                    if (keep && g_engine.cfg.drop_effects && chunk.is_effect) {
                        keep = false; // additive/screen effect plane -- renders as a black panel
                        effect_drop = true;
                    }
                    bool isolation_drop = false;
                    if (keep && g_engine.cfg.isolate_by_bone && g_engine.cfg.has_match_pos &&
                        chunk.has_vertex_shader && !BoneMatchesTarget(device, g_engine.cfg)) {
                        keep = false; // skinned, but belongs to a different agent
                        isolation_drop = true;
                    }
                    if (keep) {
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
                            ps.regs.resize(static_cast<size_t>(kProbeRegCount) * 4, 0.f);
                            if (SUCCEEDED(device->GetVertexShaderConstantF(0, ps.regs.data(),
                                                                           static_cast<UINT>(kProbeRegCount)))) {
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
        g_engine.ResetData();
        g_engine.installed = false;
    }

    bool IsInstalled() { return g_engine.installed; }

    void Arm(const Config& cfg)
    {
        g_engine.ResetData();
        g_engine.cfg = cfg;
        g_engine.armed = true;
        g_engine.recorded = false;
        g_engine.frames_since_arm = 0;
    }

    CaptureState Advance()
    {
        if (!g_engine.armed) {
            return g_engine.chunks.empty() ? CaptureState::Idle : CaptureState::Ready;
        }
        g_engine.frames_since_arm++;
        if (g_engine.recorded && g_engine.frames_since_arm >= 1) {
            g_engine.armed = false;
            g_engine.stats.unique_textures = 0;
            std::set<void*> tex;
            for (const auto& c : g_engine.chunks) {
                if (c.texture_ptr) tex.insert(c.texture_ptr);
            }
            g_engine.stats.unique_textures = static_cast<uint32_t>(tex.size());
            return g_engine.chunks.empty() ? CaptureState::Failed : CaptureState::Ready;
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

    std::vector<MeshChunk>& Chunks() { return g_engine.chunks; }
    const std::vector<ProbeSample>& ProbeSamples() { return g_engine.probes; }
    const std::vector<DrawLogEntry>& DrawLog() { return g_engine.draw_log; }
    const CaptureStats& Stats() { return g_engine.stats; }

} // namespace Capture
} // namespace Guildlite
