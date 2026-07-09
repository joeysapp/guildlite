#include "ObjWriter.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>

namespace Guildlite {
namespace {

    std::string MaterialName(const std::string& texture_file)
    {
        std::string stem = texture_file;
        const size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) {
            stem = stem.substr(0, dot);
        }
        std::string out = "mat_";
        for (const char c : stem) {
            out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
        }
        return out;
    }

    uint64_t FileSize(const std::filesystem::path& p)
    {
        std::error_code ec;
        const auto s = std::filesystem::file_size(p, ec);
        return ec ? 0u : static_cast<uint64_t>(s);
    }

    // GW authors models with a configurable "up" axis (Z by default); OBJ/STL and
    // DCC/OS 3D viewers are Y-up, so a raw grab lies on its side. Rotate the chosen
    // up axis onto +Y with a right-handed basis so exports stand upright. It is a
    // pure rotation, so the same map applies to normals.
    inline void RemapUp(int up_axis, float& x, float& y, float& z)
    {
        const float ix = x, iy = y, iz = z;
        switch (up_axis) {
            case 0: x = -iy; y = ix; z = iz; break;  // X-up -> Y-up
            case 2: x = ix;  y = -iz; z = iy; break; // Z-up -> Y-up, head up (GW default)
            default: break;                          // 1 = already Y-up, leave as-is
        }
    }

} // namespace

namespace ObjWriter {

    WriteResult WriteObj(const std::vector<MeshChunk>& chunks, const Config& cfg,
                         const std::filesystem::path& dir, const std::wstring& stem)
    {
        WriteResult res;
        const auto obj_path = dir / (stem + L".obj");
        const auto mtl_path = dir / (stem + L".mtl");

        std::ofstream out(obj_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            res.error = "could not open .obj for writing";
            return res;
        }

        const bool want_uv = cfg.export_uvs && cfg.detail == DetailLevel::Advanced;
        const bool want_normal = cfg.export_normals;
        const bool want_tex = cfg.export_textures && cfg.detail == DetailLevel::Advanced;

        // Material identity is (texture, blend-CLASS), not texture alone, and the class
        // is Opaque or Additive -- NOT "alpha-blended => texture alpha is opacity".
        //
        // GW never uses a diffuse texture's alpha as framebuffer opacity on character
        // draws: for OPAQUE and SRCALPHA/INVSRCALPHA armour/hair/skirt draws the alpha
        // is a gloss/spec/layer mask (the pixel shader outputs ~1.0; the blend is only
        // edge AA). Proof: 72% of a skirt's sampled texels are alpha==0 yet it is solid
        // dark armour in-game. So emitting map_d there turned solid armour into a ghost.
        // Only ADDITIVE effect draws (is_effect: DEST=ONE/INVSRCCOLOR -- flames, auras)
        // are see-through, and there the transparency is "black adds nothing", which the
        // opaque alpha channel cannot express anyway. So: opaque draws get a plain solid
        // material (no map_d); effect draws get an emissive material ('_fx') whose black
        // is keyed transparent downstream (blender_render / the glTF exporter use the
        // sidecar .json). This also collapses the old redundant opaque/'_a' pair.
        enum class BlendClass { Opaque, Additive };
        auto classify = [](const MeshChunk& c) {
            return c.is_effect ? BlendClass::Additive : BlendClass::Opaque;
        };
        auto material_name = [](const std::string& tex, BlendClass cls) {
            return MaterialName(tex) + (cls == BlendClass::Additive ? "_fx" : "");
        };
        struct MatDef { std::string texture_file; BlendClass cls; };
        std::map<std::string, MatDef> materials; // material name -> definition
        if (want_tex) {
            for (const auto& c : chunks) {
                if (c.texture_file.empty()) continue;
                const BlendClass cls = classify(c);
                materials.emplace(material_name(c.texture_file, cls),
                                  MatDef{c.texture_file, cls});
            }
        }

        // Skin weights are emitted as "#vbld" comment lines interleaved with the vertices
        // (standard OBJ ignores them; a mmobj-style importer can read them). Announce the
        // format once at the top if any chunk actually carries them.
        bool any_blend = false;
        if (cfg.export_skin_weights) {
            for (const auto& c : chunks) {
                const size_t nv = c.positions.size() / 3;
                if (nv > 0 && c.blend_indices.size() == nv * 4 && c.blend_weights.size() == nv * 4) {
                    any_blend = true;
                    break;
                }
            }
        }

        out << "# Guildlite model snapshot\n";
        out << "# objects: " << chunks.size() << "\n";
        if (any_blend) {
            out << "# skin: a '#vbld i0 w0 i1 w1 i2 w2 i3 w3' line after each 'v' gives that\n";
            out << "#   vertex's 4 bone-palette indices + weights (GW packs the indices in a\n";
            out << "#   D3DCOLOR; weight is 1,0,0,0 when rigidly bound to one bone). The bone\n";
            out << "#   transforms live in GW's vertex-shader constant palette -- see the .json\n";
            out << "#   manifest (probe[]) captured alongside this file.\n";
        }
        if (!materials.empty()) {
            const std::string mtl_name(mtl_path.filename().string());
            out << "mtllib " << mtl_name << "\n";
        }

        size_t v_off = 0, vt_off = 0, vn_off = 0;
        for (const auto& c : chunks) {
            const size_t nverts = c.positions.size() / 3;
            if (nverts == 0 || c.indices.empty()) {
                continue;
            }
            const bool has_uv = want_uv && c.uvs.size() == nverts * 2;
            const bool has_normal = want_normal && c.normals.size() == nverts * 3;
            const bool has_blend = cfg.export_skin_weights &&
                                   c.blend_indices.size() == nverts * 4 &&
                                   c.blend_weights.size() == nverts * 4;

            out << "o object_" << c.draw_index << "\n";
            if (want_tex && !c.texture_file.empty()) {
                out << "usemtl " << material_name(c.texture_file, classify(c)) << "\n";
            }

            for (size_t i = 0; i < nverts; ++i) {
                float x = c.positions[i * 3 + 0], y = c.positions[i * 3 + 1], z = c.positions[i * 3 + 2];
                RemapUp(cfg.up_axis, x, y, z);
                out << "v " << x << ' ' << y << ' ' << z << "\n";
                if (has_blend) {
                    out << "#vbld";
                    for (int k = 0; k < 4; ++k) {
                        out << ' ' << static_cast<unsigned>(c.blend_indices[i * 4 + k])
                            << ' ' << c.blend_weights[i * 4 + k];
                    }
                    out << "\n";
                }
            }
            if (has_uv) {
                for (size_t i = 0; i < nverts; ++i) {
                    // Flip V: D3D texcoords grow downward, OBJ/DCC tools expect upward.
                    out << "vt " << c.uvs[i * 2 + 0] << ' ' << (1.0f - c.uvs[i * 2 + 1]) << "\n";
                }
            }
            if (has_normal) {
                for (size_t i = 0; i < nverts; ++i) {
                    float nx = c.normals[i * 3 + 0], ny = c.normals[i * 3 + 1], nz = c.normals[i * 3 + 2];
                    RemapUp(cfg.up_axis, nx, ny, nz);
                    out << "vn " << nx << ' ' << ny << ' ' << nz << "\n";
                }
            }

            for (size_t t = 0; t + 2 < c.indices.size(); t += 3) {
                const size_t a = c.indices[t + 0];
                const size_t b = c.indices[t + 1];
                const size_t d = c.indices[t + 2];
                const size_t va = a + v_off + 1, vb = b + v_off + 1, vd = d + v_off + 1;
                out << 'f';
                if (has_uv && has_normal) {
                    const size_t ta = a + vt_off + 1, tb = b + vt_off + 1, td = d + vt_off + 1;
                    const size_t na = a + vn_off + 1, nb = b + vn_off + 1, nd = d + vn_off + 1;
                    out << ' ' << va << '/' << ta << '/' << na
                        << ' ' << vb << '/' << tb << '/' << nb
                        << ' ' << vd << '/' << td << '/' << nd << "\n";
                }
                else if (has_normal) {
                    const size_t na = a + vn_off + 1, nb = b + vn_off + 1, nd = d + vn_off + 1;
                    out << ' ' << va << "//" << na << ' ' << vb << "//" << nb << ' ' << vd << "//" << nd << "\n";
                }
                else if (has_uv) {
                    const size_t ta = a + vt_off + 1, tb = b + vt_off + 1, td = d + vt_off + 1;
                    out << ' ' << va << '/' << ta << ' ' << vb << '/' << tb << ' ' << vd << '/' << td << "\n";
                }
                else {
                    out << ' ' << va << ' ' << vb << ' ' << vd << "\n";
                }
            }

            v_off += nverts;
            if (has_uv) vt_off += nverts;
            if (has_normal) vn_off += nverts;
        }
        out.close();

        if (!materials.empty()) {
            std::ofstream mtl(mtl_path, std::ios::binary | std::ios::trunc);
            if (mtl.is_open()) {
                mtl << "# Guildlite materials\n";
                for (const auto& [name, def] : materials) {
                    mtl << "newmtl " << name << "\n";
                    if (def.cls == BlendClass::Additive) {
                        // GW draws this additively (flame/aura/glow): black adds nothing and
                        // must read as transparent. Emit it unlit+emissive; the black is keyed
                        // out by luminance downstream (blender_render / glTF read the .json).
                        mtl << "# guildlite_blend additive\n";
                        mtl << "Ka 0 0 0\n";
                        mtl << "Kd 0 0 0\n";           // unlit: colour comes from emission
                        mtl << "Ks 0 0 0\n";
                        mtl << "Ke 1 1 1\n";           // emissive scale; map_Ke supplies colour
                        mtl << "d 1.0\n";
                        mtl << "illum 1\n";
                        mtl << "map_Kd " << def.texture_file << "\n";
                        mtl << "map_Ke " << def.texture_file << "\n";
                    } else {
                        // Opaque: the diffuse alpha here is a gloss/layer mask, NOT opacity,
                        // so no map_d -- the piece renders solid (armour, hair, skirt, body).
                        mtl << "# guildlite_blend opaque\n";
                        mtl << "Ka 0.02 0.02 0.02\n";
                        mtl << "Kd 1.0 1.0 1.0\n";     // white base so the texture shows unmodulated
                        mtl << "Ks 0.30 0.30 0.30\n";  // modest spec so lit renders read as 3D, not flat
                        mtl << "Ns 24.0\n";
                        mtl << "d 1.0\n";
                        mtl << "illum 2\n";
                        mtl << "map_Kd " << def.texture_file << "\n";
                    }
                    mtl << "\n";
                }
            }
        }

        res.ok = true;
        res.main_file = obj_path;
        res.bytes = FileSize(obj_path);
        return res;
    }

    WriteResult WriteStl(const std::vector<MeshChunk>& chunks, const Config& cfg,
                         const std::filesystem::path& dir, const std::wstring& stem)
    {
        WriteResult res;
        const auto stl_path = dir / (stem + L".stl");
        std::ofstream out(stl_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            res.error = "could not open .stl for writing";
            return res;
        }

        char header[80] = {};
        const char* label = "Guildlite binary STL model snapshot";
        std::memcpy(header, label, std::strlen(label));
        out.write(header, sizeof(header));

        uint32_t tri_total = 0;
        for (const auto& c : chunks) {
            tri_total += static_cast<uint32_t>(c.indices.size() / 3);
        }
        out.write(reinterpret_cast<const char*>(&tri_total), sizeof(tri_total));

        const uint16_t attr = 0;
        for (const auto& c : chunks) {
            const size_t nverts = c.positions.size() / 3;
            for (size_t t = 0; t + 2 < c.indices.size(); t += 3) {
                const uint32_t ia = c.indices[t + 0], ib = c.indices[t + 1], ic = c.indices[t + 2];
                if (ia >= nverts || ib >= nverts || ic >= nverts) {
                    continue;
                }
                float pa[3] = {c.positions[ia * 3], c.positions[ia * 3 + 1], c.positions[ia * 3 + 2]};
                float pb[3] = {c.positions[ib * 3], c.positions[ib * 3 + 1], c.positions[ib * 3 + 2]};
                float pc[3] = {c.positions[ic * 3], c.positions[ic * 3 + 1], c.positions[ic * 3 + 2]};
                RemapUp(cfg.up_axis, pa[0], pa[1], pa[2]);
                RemapUp(cfg.up_axis, pb[0], pb[1], pb[2]);
                RemapUp(cfg.up_axis, pc[0], pc[1], pc[2]);
                const float ux = pb[0] - pa[0], uy = pb[1] - pa[1], uz = pb[2] - pa[2];
                const float vx = pc[0] - pa[0], vy = pc[1] - pa[1], vz = pc[2] - pa[2];
                float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
                const float normal[3] = {nx, ny, nz};
                out.write(reinterpret_cast<const char*>(normal), sizeof(normal));
                out.write(reinterpret_cast<const char*>(pa), sizeof(float) * 3);
                out.write(reinterpret_cast<const char*>(pb), sizeof(float) * 3);
                out.write(reinterpret_cast<const char*>(pc), sizeof(float) * 3);
                out.write(reinterpret_cast<const char*>(&attr), sizeof(attr));
            }
        }
        out.close();

        res.ok = true;
        res.main_file = stl_path;
        res.bytes = FileSize(stl_path);
        return res;
    }

} // namespace ObjWriter
} // namespace Guildlite
