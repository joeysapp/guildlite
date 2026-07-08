// datcli — a tiny cross-platform harness over datcore, used to validate the
// Gw.dat archive + decompression port against a real 3.9 GB archive.
//
//   datcli info    <dat>                     master-file-table summary (fast)
//   datcli census  <dat> [limit]             decompress+classify entries (0 = all)
//   datcli extract <dat> <index> <outfile>   write one decompressed entry to disk
//
// Bare `datcli <dat>` is shorthand for `census <dat>`.
#include "datcore/dat.h"
#include "datcore/model.h"
#include "datcore/texture.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace datcore;

static void usage() {
    fprintf(stderr,
            "usage:\n"
            "  datcli info    <dat>\n"
            "  datcli census  <dat> [limit]     (limit 0 = all entries)\n"
            "  datcli extract <dat> <index> <outfile>\n"
            "  datcli obj     <dat> <index> <outfile.obj>\n"
            "  datcli objtex  <dat> <index> <outdir>       model + textures (OBJ+MTL+PNG)\n");
}

static int cmd_tex(Dat& dat, size_t index, const char* out) {
    if (index >= dat.num_files()) { fprintf(stderr, "index out of range\n"); return 2; }
    std::vector<uint8_t> blob = dat.read_file(index, true);
    if (!type_is_texture(dat.mft()[index].type)) {
        fprintf(stderr, "entry %zu is %s, not a texture\n", index, type_to_string(dat.mft()[index].type));
        return 2;
    }
    Texture t;
    if (!decode_texture(blob.data(), blob.size(), t)) {
        fprintf(stderr, "entry %zu: decode failed (type=%s)\n", index, type_to_string(dat.mft()[index].type));
        return 2;
    }
    if (!write_png(t, out)) { fprintf(stderr, "cannot write %s\n", out); return 2; }
    printf("entry %zu -> %s  (%dx%d, %s)\n", index, out, t.width, t.height,
           t.needed_asm ? "PARTIAL: needed asm stage (run on Windows for full fidelity)" : "native decode OK");
    return 0;
}

static int cmd_texscan(Dat& dat, size_t limit) {
    const size_t total = dat.num_files();
    const size_t n = (limit == 0 || limit > total) ? total : limit;
    size_t tex = 0, decoded = 0, native = 0, needs_asm = 0, failed = 0;
    for (size_t i = 0; i < n; ++i) {
        std::vector<uint8_t> blob = dat.read_file(i, true);
        int ty = dat.mft()[i].type;
        if (!type_is_texture(ty) || ty == DDS) continue; // DDS bypasses ATEX (separate path)
        ++tex;
        Texture t;
        if (decode_texture(blob.data(), blob.size(), t)) {
            ++decoded;
            if (t.needed_asm) ++needs_asm; else ++native;
        } else ++failed;
        if ((i & 0x3fff) == 0x3fff) fprintf(stderr, "\r  scanned %zu / %zu ...", i + 1, n), fflush(stderr);
    }
    fprintf(stderr, "\r%40s\r", "");
    printf("texture scan of %zu / %zu entries\n", n, total);
    printf("  ATEX/ATTX textures        : %zu\n", tex);
    printf("  decoded native (no asm)   : %zu (%.1f%%)\n", native, tex ? 100.0 * native / tex : 0.0);
    printf("  need asm stage (partial)  : %zu\n", needs_asm);
    printf("  decode failed             : %zu\n", failed);
    return 0;
}

static int cmd_scan(Dat& dat, size_t limit) {
    const size_t total = dat.num_files();
    const size_t n = (limit == 0 || limit > total) ? total : limit;
    size_t models = 0, with_geom = 0, partial = 0;
    uint64_t tris = 0, verts = 0;
    for (size_t i = 0; i < n; ++i) {
        std::vector<uint8_t> blob = dat.read_file(i, true);
        if (dat.mft()[i].type != FFNA_Type2) continue;
        ++models;
        Model m;
        if (parse_model(blob.data(), blob.size(), m)) {
            ++with_geom; verts += m.total_vertices(); tris += m.total_triangles();
            if (!m.fully_parsed) ++partial;
        }
        if ((i & 0x3fff) == 0x3fff)
            fprintf(stderr, "\r  scanned %zu / %zu ...", i + 1, n), fflush(stderr);
    }
    fprintf(stderr, "\r%40s\r", "");
    printf("model scan of %zu / %zu entries\n", n, total);
    printf("  FFNA-Model entries        : %zu\n", models);
    printf("  with 0xFA0 geometry (ok)  : %zu (%.1f%% of models)%s\n", with_geom,
           models ? 100.0 * with_geom / models : 0.0,
           partial ? "" : "");
    printf("    of which partial parse  : %zu\n", partial);
    printf("  other-format (0xBB8 etc.) : %zu\n", models - with_geom);
    printf("  exportable geometry total : %llu vertices, %llu triangles\n",
           (unsigned long long)verts, (unsigned long long)tris);
    return 0;
}

static int cmd_obj(Dat& dat, size_t index, const char* out) {
    if (index >= dat.num_files()) { fprintf(stderr, "index out of range\n"); return 2; }
    std::vector<uint8_t> blob = dat.read_file(index, true);
    if (dat.mft()[index].type != FFNA_Type2) {
        fprintf(stderr, "entry %zu is %s, not a model (FFNA-Model)\n",
                index, type_to_string(dat.mft()[index].type));
        return 2;
    }
    Model m;
    if (!parse_model(blob.data(), blob.size(), m)) {
        fprintf(stderr, "entry %zu: no geometry recovered%s\n",
                index, m.fully_parsed ? "" : " (parser bailed early)");
        return 2;
    }
    if (!write_obj(m, out)) { fprintf(stderr, "cannot write %s\n", out); return 2; }
    printf("entry %zu -> %s\n", index, out);
    printf("  submeshes=%zu vertices=%zu triangles=%zu parsed=%s\n",
           m.submeshes.size(), m.total_vertices(), m.total_triangles(),
           m.fully_parsed ? "full" : "partial");
    return 0;
}

static int cmd_info(Dat& dat) {
    size_t compressed = 0, base = 0;
    for (const auto& e : dat.mft()) {
        if (e.a) ++compressed;
        if (!e.b) ++base;
    }
    printf("master file table\n");
    printf("  entries      : %zu\n", dat.num_files());
    printf("  sector size  : %d\n", dat.sector_size());
    printf("  compressed   : %zu\n", compressed);
    printf("  base/reserved: %zu\n", base);
    return 0;
}

static int cmd_census(Dat& dat, size_t limit) {
    const size_t total = dat.num_files();
    const size_t n = (limit == 0 || limit > total) ? total : limit;

    size_t counts[FILETYPE_COUNT] = {0};
    size_t read_ok = 0;
    uint64_t bytes_out = 0;

    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; ++i) {
        std::vector<uint8_t> blob = dat.read_file(i, true);
        int t = dat.mft()[i].type;
        if (t < 0 || t >= FILETYPE_COUNT) t = UNKNOWN;
        counts[t]++;
        if (!blob.empty()) { ++read_ok; bytes_out += blob.size(); }
        if ((i & 0x3fff) == 0x3fff)
            fprintf(stderr, "\r  scanned %zu / %zu ...", i + 1, n), fflush(stderr);
    }
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    fprintf(stderr, "\r%40s\r", "");

    printf("census of %zu / %zu entries in %.1fs (%.0f entries/s)\n",
           n, total, secs, secs > 0 ? n / secs : 0.0);
    printf("  decompressed : %zu blobs, %.1f MB uncompressed\n",
           read_ok, bytes_out / (1024.0 * 1024.0));
    printf("  by type:\n");
    for (int t = 0; t < FILETYPE_COUNT; ++t) {
        if (counts[t] == 0) continue;
        printf("    %-14s %8zu\n", type_to_string(t), counts[t]);
    }
    printf("  ----\n");
    printf("    %-14s %8zu\n", "FFNA-Model", counts[FFNA_Type2]);
    printf("    %-14s %8zu\n", "FFNA-Map", counts[FFNA_Type3]);
    size_t tex = 0;
    for (int t = ATEXDXT1; t <= ATTXDXTL; ++t) tex += counts[t];
    tex += counts[DDS];
    printf("    %-14s %8zu\n", "textures(all)", tex);
    return 0;
}

static int cmd_extract(Dat& dat, size_t index, const char* out) {
    if (index >= dat.num_files()) { fprintf(stderr, "index out of range\n"); return 2; }
    std::vector<uint8_t> blob = dat.read_file(index, true);
    if (blob.empty()) { fprintf(stderr, "entry %zu produced no data (type=%s)\n",
                                index, type_to_string(dat.mft()[index].type)); return 2; }
    FILE* f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", out); return 2; }
    fwrite(blob.data(), 1, blob.size(), f);
    fclose(f);
    printf("entry %zu: type=%s hash=%d -> %s (%zu bytes)\n",
           index, type_to_string(dat.mft()[index].type), dat.mft()[index].hash, out, blob.size());
    return 0;
}

static int cmd_objtex(Dat& dat, size_t index, const char* outdir) {
    if (index >= dat.num_files()) { fprintf(stderr, "index out of range\n"); return 2; }
    std::vector<uint8_t> blob = dat.read_file(index, true);
    if (dat.mft()[index].type != FFNA_Type2) {
        fprintf(stderr, "entry %zu is %s, not a model\n", index, type_to_string(dat.mft()[index].type));
        return 2;
    }
    Model m;
    if (!parse_model(blob.data(), blob.size(), m)) { fprintf(stderr, "entry %zu: no geometry\n", index); return 2; }

    char stem[64];
    snprintf(stem, sizeof(stem), "model_%zu", index);
    const std::string base = std::string(outdir) + "/" + stem;

    // Resolve + decode each referenced texture -> PNG. Material k = k-th texture
    // that decoded OK. (Real per-submesh texture binding lived in GetMesh, which
    // the parser port drops; see below for the crude submesh->material mapping.)
    std::string mtl_body;
    std::vector<int> mats; // material indices in texref order
    int num_mat = 0, partial = 0;
    for (const auto& ref : m.texture_refs) {
        int midx = dat.index_for_fileref(ref.id0, ref.id1);
        if (getenv("DATCORE_DEBUG"))
            fprintf(stderr, "[objtex] texref(%u,%u) -> mft %d\n", ref.id0, ref.id1, midx);
        if (midx < 0) continue;
        std::vector<uint8_t> tb = dat.read_file(static_cast<size_t>(midx), true);
        Texture tex;
        if (!decode_texture(tb.data(), tb.size(), tex)) continue;
        char png[96];
        snprintf(png, sizeof(png), "%s_tex%d.png", stem, num_mat);
        if (!write_png(tex, (std::string(outdir) + "/" + png).c_str())) continue;
        char mat[256];
        snprintf(mat, sizeof(mat), "newmtl mat%d\nKd 1 1 1\nmap_Kd %s\n%s", num_mat, png,
                 tex.needed_asm ? "# partial decode (asm stub — run on Windows for full fidelity)\n" : "");
        mtl_body += mat;
        mats.push_back(num_mat);
        if (tex.needed_asm) ++partial;
        ++num_mat;
    }

    // Crude submesh->material assignment: pair 1:1 where possible, else material 0.
    std::vector<int> submesh_mat(m.submeshes.size(), -1);
    for (size_t s = 0; s < m.submeshes.size() && !mats.empty(); ++s)
        submesh_mat[s] = mats[s < mats.size() ? s : 0];

    if (num_mat > 0) {
        FILE* mf = fopen((base + ".mtl").c_str(), "wb");
        if (mf) { fputs(mtl_body.c_str(), mf); fclose(mf); }
    }
    const std::string mtllib = num_mat > 0 ? (std::string(stem) + ".mtl") : "";
    if (!write_obj_textured(m, base + ".obj", mtllib, submesh_mat)) {
        fprintf(stderr, "failed to write %s.obj\n", base.c_str()); return 2;
    }

    printf("entry %zu -> %s.obj\n", index, base.c_str());
    printf("  submeshes=%zu texrefs=%zu textures_decoded=%d%s\n",
           m.submeshes.size(), m.texture_refs.size(), num_mat,
           partial ? " (some partial — asm stub)" : "");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    std::string cmd = argv[1];
    const char* datpath = nullptr;
    bool bare = (cmd != "info" && cmd != "census" && cmd != "extract" && cmd != "obj" &&
                 cmd != "scan" && cmd != "tex" && cmd != "texscan" && cmd != "objtex");
    if (bare) { cmd = "census"; datpath = argv[1]; }
    else if (argc >= 3) { datpath = argv[2]; }
    if (!datpath) { usage(); return 1; }

    Dat dat;
    fprintf(stderr, "opening %s ...\n", datpath);
    if (!dat.open(datpath)) { fprintf(stderr, "error: %s\n", dat.error().c_str()); return 1; }
    fprintf(stderr, "opened: %zu MFT entries\n", dat.num_files());

    if (cmd == "info") return cmd_info(dat);
    if (cmd == "census") {
        size_t limit = 20000;
        const char* limarg = bare ? (argc >= 3 ? argv[2] : nullptr) : (argc >= 4 ? argv[3] : nullptr);
        if (limarg) limit = strtoull(limarg, nullptr, 10);
        return cmd_census(dat, limit);
    }
    if (cmd == "extract") {
        if (argc < 5) { usage(); return 1; }
        return cmd_extract(dat, strtoull(argv[3], nullptr, 10), argv[4]);
    }
    if (cmd == "obj") {
        if (argc < 5) { usage(); return 1; }
        return cmd_obj(dat, strtoull(argv[3], nullptr, 10), argv[4]);
    }
    if (cmd == "objtex") {
        if (argc < 5) { usage(); return 1; }
        return cmd_objtex(dat, strtoull(argv[3], nullptr, 10), argv[4]);
    }
    if (cmd == "scan") {
        size_t limit = 40000;
        if (argc >= 4) limit = strtoull(argv[3], nullptr, 10);
        return cmd_scan(dat, limit);
    }
    if (cmd == "tex") {
        if (argc < 5) { usage(); return 1; }
        return cmd_tex(dat, strtoull(argv[3], nullptr, 10), argv[4]);
    }
    if (cmd == "texscan") {
        size_t limit = 40000;
        if (argc >= 4) limit = strtoull(argv[3], nullptr, 10);
        return cmd_texscan(dat, limit);
    }
    usage();
    return 1;
}
