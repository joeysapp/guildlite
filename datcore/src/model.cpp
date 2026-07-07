#include "datcore/model.h"
#include "datcore/ffna_model.h"

#include <cstdio>
#include <span>
#include <utility>

namespace datcore {

bool parse_model(const uint8_t* data, size_t size, Model& out) {
    if (size < 5) return false;
    if (!(data[0] == 'f' && data[1] == 'f' && data[2] == 'n' && data[3] == 'a')) return false;
    if (data[4] != 2) return false; // FFNAType::Model

    // The reference parser takes a mutable std::span<unsigned char>&; give it a
    // private copy so we never mutate the caller's buffer.
    std::vector<unsigned char> buf(data, data + size);
    std::span<unsigned char> sp(buf.data(), buf.size());
    FFNA_ModelFile mf(0, sp);
    out.fully_parsed = mf.parsed_correctly;

    if (getenv("DATCORE_DEBUG")) {
        fprintf(stderr, "[dbg] parsed_ok=%d riff_chunks=%zu models=%zu chunk_ids=[",
                mf.parsed_correctly, mf.riff_chunks.size(), mf.geometry_chunk.models.size());
        for (auto& kv : mf.riff_chunks) fprintf(stderr, "0x%X ", kv.first);
        fprintf(stderr, "]\n");
        for (size_t i = 0; i < mf.geometry_chunk.models.size(); ++i) {
            const auto& g = mf.geometry_chunk.models[i];
            fprintf(stderr, "[dbg]   model[%zu] verts=%zu idx=%zu (n0=%u n1=%u n2=%u) fvf=0x%X\n",
                    i, g.vertices.size(), g.indices.size(),
                    g.num_indices0, g.num_indices1, g.num_indices2, g.dat_fvf);
        }
    }

    for (const auto& gm : mf.geometry_chunk.models) {
        if (gm.vertices.empty() || gm.indices.empty()) continue;
        Submesh sm;
        sm.positions.reserve(gm.vertices.size());
        for (const auto& v : gm.vertices) {
            sm.positions.push_back({v.x, v.y, v.z});
            if (v.has_normal) sm.normals.push_back({v.normal_x, v.normal_y, v.normal_z});
            if (v.has_tex_coord[0]) sm.uv0.push_back({v.tex_coord[0][0], v.tex_coord[0][1]});
        }
        sm.has_normals = !sm.normals.empty() && sm.normals.size() == sm.positions.size();
        sm.has_uv0 = !sm.uv0.empty() && sm.uv0.size() == sm.positions.size();

        // Index buffer holds up to three concatenated LOD levels, deduplicated
        // when consecutive counts match (same rule the parser used to size it):
        //   total = n0 + (n0!=n1)*n1 + (n1!=n2)*n2
        const auto& idx = gm.indices;
        const uint32_t n0 = gm.num_indices0, n1 = gm.num_indices1, n2 = gm.num_indices2;
        size_t cur = 0;
        auto take = [&](uint32_t count, std::vector<uint32_t>& dst) {
            for (uint32_t i = 0; i < count && cur < idx.size(); ++i, ++cur) dst.push_back(idx[cur]);
        };
        take(n0, sm.indices);
        if (n0 != n1) take(n1, sm.indices_med);
        if (n1 != n2) take(n2, sm.indices_low);

        out.submeshes.push_back(std::move(sm));
    }
    return !out.submeshes.empty();
}

bool write_obj(const Model& m, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    fprintf(f, "# Guild Wars model, exported by datcore (FFNA type-2, High LOD)\n");
    fprintf(f, "# submeshes=%zu vertices=%zu triangles=%zu%s\n",
            m.submeshes.size(), m.total_vertices(), m.total_triangles(),
            m.fully_parsed ? "" : " (PARTIAL parse)");

    uint32_t vbase = 1; // OBJ indices are 1-based and file-global
    for (size_t s = 0; s < m.submeshes.size(); ++s) {
        const Submesh& sm = m.submeshes[s];
        fprintf(f, "o submesh_%zu\n", s);
        for (const auto& p : sm.positions) fprintf(f, "v %.6f %.6f %.6f\n", p.x, p.y, p.z);
        if (sm.has_uv0)
            for (const auto& t : sm.uv0) fprintf(f, "vt %.6f %.6f\n", t.u, 1.0f - t.v); // OBJ V-flip
        if (sm.has_normals)
            for (const auto& n : sm.normals) fprintf(f, "vn %.6f %.6f %.6f\n", n.x, n.y, n.z);

        for (size_t i = 0; i + 2 < sm.indices.size(); i += 3) {
            // reverse winding (a,c,b) so front faces survive the handedness flip
            uint32_t a = sm.indices[i] + vbase;
            uint32_t b = sm.indices[i + 1] + vbase;
            uint32_t c = sm.indices[i + 2] + vbase;
            if (sm.has_uv0 && sm.has_normals)
                fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, c, c, c, b, b, b);
            else if (sm.has_uv0)
                fprintf(f, "f %u/%u %u/%u %u/%u\n", a, a, c, c, b, b);
            else if (sm.has_normals)
                fprintf(f, "f %u//%u %u//%u %u//%u\n", a, a, c, c, b, b);
            else
                fprintf(f, "f %u %u %u\n", a, c, b);
        }
        vbase += static_cast<uint32_t>(sm.positions.size());
    }
    fclose(f);
    return true;
}

} // namespace datcore
