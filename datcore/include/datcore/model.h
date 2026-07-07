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

struct Model {
    std::vector<Submesh> submeshes;
    bool fully_parsed = false;      // false if the parser bailed partway
    size_t total_vertices() const {
        size_t n = 0; for (auto& s : submeshes) n += s.positions.size(); return n;
    }
    size_t total_triangles() const {
        size_t n = 0; for (auto& s : submeshes) n += s.indices.size() / 3; return n;
    }
};

// Parse an in-memory decompressed FFNA type-2 blob. Returns false if it is not a
// model or no geometry was recovered.
bool parse_model(const uint8_t* data, size_t size, Model& out);

// Write High-LOD geometry to a Wavefront OBJ (three.js / Blender friendly:
// UV V-flipped, winding reversed). Returns false on file error.
bool write_obj(const Model& m, const std::string& path);

} // namespace datcore
