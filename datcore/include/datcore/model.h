#pragma once
// Portable FFNA type-2 (model) geometry extraction + OBJ export.
// Wraps the trimmed GWMB parser (ffna_model.h) behind a clean, DirectX-free API.
#include <cstdint>
#include <string>
#include <vector>

namespace datcore {

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec2 { float u = 0, v = 0; };

// One GW GeometryModel -> one submesh. Positions/normals/uv0 are parallel arrays
// (same length when present, since a submesh has a single vertex format).
struct Submesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;      // empty if the FVF had no normals
    std::vector<Vec2> uv0;          // empty if the FVF had no texcoords
    std::vector<uint32_t> indices;      // High LOD triangle list
    std::vector<uint32_t> indices_med;  // Medium LOD (may be empty / == high)
    std::vector<uint32_t> indices_low;  // Low LOD (may be empty / == med)
    bool has_normals = false;
    bool has_uv0 = false;
};

// A texture cross-reference (id0,id1) from the model's 0xFA5 chunk; resolve to an
// MFT index with Dat::index_for_fileref(id0, id1).
struct TexRef { uint16_t id0 = 0; uint16_t id1 = 0; };

struct Model {
    std::vector<Submesh> submeshes;
    std::vector<TexRef> texture_refs;   // model's referenced texture files (0xFA5)
    bool fully_parsed = false;          // false if the parser bailed partway
    size_t total_vertices() const {
        size_t n = 0; for (auto& s : submeshes) n += s.positions.size(); return n;
    }
    size_t total_triangles() const {
        size_t n = 0; for (auto& s : submeshes) n += s.indices.size() / 3; return n;
    }
};

// Write OBJ with material references. submesh_material[i] = index into the MTL's
// materials (material name "mat<index>"), or -1 for no material. `mtllib_basename`
// is written as the `mtllib` line (e.g. "foo.mtl"). Same geometry conventions as
// write_obj (V-flip, reversed winding).
bool write_obj_textured(const Model& m, const std::string& obj_path,
                        const std::string& mtllib_basename,
                        const std::vector<int>& submesh_material);

// Parse an in-memory decompressed FFNA type-2 blob. Returns false if it is not a
// model or no geometry was recovered.
bool parse_model(const uint8_t* data, size_t size, Model& out);

// Write High-LOD geometry to a Wavefront OBJ (three.js / Blender friendly:
// UV V-flipped, winding reversed). Returns false on file error.
bool write_obj(const Model& m, const std::string& path);

} // namespace datcore
