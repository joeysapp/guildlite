#include "datcore/catalog.h"
#include "datcore/dat.h"
#include "datcore/model.h"
#include "datcore/texture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace datcore {

bool build_catalog(Dat& dat, std::vector<CatalogEntry>& out, size_t limit,
                   void (*progress_cb)(size_t, size_t)) {
    const size_t total = dat.num_files();
    const size_t n = (limit == 0 || limit > total) ? total : limit;
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        std::vector<uint8_t> blob = dat.read_file(i, true);
        const MFTEntry& m = dat.mft()[i];
        CatalogEntry e;
        e.mft = static_cast<uint32_t>(i);
        e.hash = m.hash;
        e.murmur = m.murmurhash3;
        e.type = m.type;
        e.usize = m.uncompressed_size;
        if (!blob.empty()) {
            if (m.type == FFNA_Type2) {
                Model mod;
                if (parse_model(blob.data(), blob.size(), mod)) { // geometry only (no Dat)
                    e.nsub = static_cast<int>(mod.submeshes.size());
                    e.nverts = static_cast<int>(mod.total_vertices());
                    e.ntris = static_cast<int>(mod.total_triangles());
                    for (const auto& tr : mod.texture_refs)
                        e.tex_refs.push_back(decode_filename(tr.id0, tr.id1));
                    if (!mod.amat_refs.empty())
                        e.amat_ref = decode_filename(mod.amat_refs[0].id0, mod.amat_refs[0].id1);
                }
            } else if (type_is_texture(m.type) && m.type != DDS) {
                int w = 0, h = 0; char fmt = 0;
                if (texture_info(blob.data(), blob.size(), w, h, fmt)) {
                    e.w = w; e.h = h; e.tex_fmt = fmt;
                }
            }
        }
        out.push_back(std::move(e));
        if (progress_cb && (i & 0x1fff) == 0x1fff) progress_cb(i + 1, n);
    }
    if (progress_cb) progress_cb(n, n);
    return true;
}

bool write_catalog_tsv(const std::vector<CatalogEntry>& cat, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "#mft\thash\tmurmur\ttype\tusize\tw\th\tfmt\tnsub\tnverts\tntris\tamat\ttexrefs\n");
    for (const auto& e : cat) {
        fprintf(f, "%u\t%d\t%08x\t%s\t%d\t%d\t%d\t%c\t%d\t%d\t%d\t%d\t",
                e.mft, e.hash, e.murmur, type_to_string(e.type), e.usize,
                e.w, e.h, e.tex_fmt ? e.tex_fmt : '-', e.nsub, e.nverts, e.ntris, e.amat_ref);
        for (size_t j = 0; j < e.tex_refs.size(); ++j)
            fprintf(f, "%s%d", j ? "," : "", e.tex_refs[j]);
        fputc('\n', f);
    }
    fclose(f);
    return true;
}

bool read_catalog_tsv(const std::string& path, std::vector<CatalogEntry>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[16384];
    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0] == '#' || buf[0] == '\n' || buf[0] == '\r' || buf[0] == '\0') continue;
        char* fields[13] = {0};
        int nf = 0;
        for (char* tok = strtok(buf, "\t\r\n"); tok && nf < 13; tok = strtok(nullptr, "\t\r\n"))
            fields[nf++] = tok;
        if (nf < 12) continue;
        CatalogEntry e;
        e.mft      = static_cast<uint32_t>(strtoul(fields[0], nullptr, 10));
        e.hash     = static_cast<int32_t>(strtol(fields[1], nullptr, 10));
        e.murmur   = static_cast<uint32_t>(strtoul(fields[2], nullptr, 16));
        e.type     = type_from_string(fields[3]);
        e.usize    = static_cast<int32_t>(strtol(fields[4], nullptr, 10));
        e.w        = atoi(fields[5]);
        e.h        = atoi(fields[6]);
        e.tex_fmt  = (fields[7][0] == '-') ? 0 : fields[7][0];
        e.nsub     = atoi(fields[8]);
        e.nverts   = atoi(fields[9]);
        e.ntris    = atoi(fields[10]);
        e.amat_ref = static_cast<int32_t>(strtol(fields[11], nullptr, 10));
        if (nf == 13)
            for (char* r = strtok(fields[12], ","); r; r = strtok(nullptr, ","))
                e.tex_refs.push_back(static_cast<int32_t>(strtol(r, nullptr, 10)));
        out.push_back(std::move(e));
    }
    fclose(f);
    return true;
}

} // namespace datcore
