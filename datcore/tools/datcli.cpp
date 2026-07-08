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
#include "datcore/catalog.h"
#include "datcore/labels.h"

#include <chrono>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace datcore;

static void usage() {
    fprintf(stderr,
            "usage:  (<sel> = an MFT index, or hash:<n>)\n"
            "  datcli info    <dat>\n"
            "  datcli census  <dat> [limit]                (limit 0 = all entries)\n"
            "  datcli index   <dat> [out.tsv] [limit]      build searchable catalog\n"
            "  datcli search  <catalog.tsv> [--type T] [--min-tris N] [--max-tris N]\n"
            "                               [--dim N] [--fmt C] [--hash H] [--limit N] [--dyeable]\n"
            "                               [--labels labels.json] [--name QUERY]\n"
            "  datcli show    <catalog.tsv> <mft|hash:N|murmur:HEX> [--labels labels.json]\n"
            "  datcli label   <labels.json> <hash:N|N|murmur:HEX> <name>\n"
            "                               [--category C] [--tag T]... [--source S] [--notes N]\n"
            "  datcli extract <dat> <sel> <outfile>\n"
            "  datcli obj     <dat> <sel> <outfile.obj>\n"
            "  datcli objtex  <dat> <sel> <outdir>         model + textures (OBJ+MTL+PNG)\n"
            "  datcli tex     <dat> <sel> <outfile.png>\n"
            "  datcli armor   <dat> <composites.tsv> <model_file_id> <outdir> [--gray]\n");
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
    if (!parse_model(blob.data(), blob.size(), m, &dat)) { fprintf(stderr, "entry %zu: no geometry\n", index); return 2; }

    char stem[64];
    snprintf(stem, sizeof(stem), "model_%zu", index);
    const std::string base = std::string(outdir) + "/" + stem;

    // Decode only the textures actually used — each submesh's diffuse_texture_ref,
    // resolved by parse_model via GW's GetMesh logic + AMAT. One MTL material per
    // distinct texture ref, decoded once and reused.
    std::string mtl_body;
    std::vector<int> texref_to_mat(m.texture_refs.size(), -1);
    int num_mat = 0, partial = 0;
    auto ensure_material = [&](int tr) -> int {
        if (tr < 0 || tr >= static_cast<int>(m.texture_refs.size())) return -1;
        if (texref_to_mat[tr] >= 0) return texref_to_mat[tr];
        const auto& ref = m.texture_refs[tr];
        int midx = dat.index_for_fileref(ref.id0, ref.id1);
        if (midx < 0) return -1;
        std::vector<uint8_t> tb = dat.read_file(static_cast<size_t>(midx), true);
        Texture tex;
        if (!decode_texture(tb.data(), tb.size(), tex)) return -1;
        char png[96];
        snprintf(png, sizeof(png), "%s_tex%d.png", stem, num_mat);
        if (!write_png(tex, (std::string(outdir) + "/" + png).c_str())) return -1;
        char mat[256];
        snprintf(mat, sizeof(mat), "newmtl mat%d\nKd 1 1 1\nmap_Kd %s\n%s", num_mat, png,
                 tex.needed_asm ? "# partial decode (asm stub — run on Windows for full fidelity)\n" : "");
        mtl_body += mat;
        if (tex.needed_asm) ++partial;
        texref_to_mat[tr] = num_mat;
        return num_mat++;
    };

    auto is_null_ref = [&](int i) {
        return i >= 0 && i < static_cast<int>(m.texture_refs.size()) &&
               m.texture_refs[i].id0 == 0 && m.texture_refs[i].id1 == 0;
    };
    std::vector<int> submesh_mat(m.submeshes.size(), -1);
    int dye_slots = 0;
    for (size_t s = 0; s < m.submeshes.size(); ++s) {
        int tr = m.submeshes[s].diffuse_texture_ref;
        if (is_null_ref(tr)) ++dye_slots; // GW points this submesh at an empty (runtime-dyed) slot
        if (tr >= static_cast<int>(m.texture_refs.size())) tr = static_cast<int>(m.texture_refs.size()) - 1;
        if (tr < 0 && !m.texture_refs.empty()) tr = 0;
        int mat = is_null_ref(tr) ? -1 : ensure_material(tr);
        // Fall back to the LAST non-null texref — for dyeable armor that's the grayscale
        // dye-mask/base, not an unrelated shared texture that may be listed first.
        for (int alt = static_cast<int>(m.texture_refs.size()) - 1; mat < 0 && alt >= 0; --alt) {
            if (is_null_ref(alt)) continue;
            mat = ensure_material(alt);
        }
        submesh_mat[s] = mat;
        if (getenv("DATCORE_DEBUG"))
            fprintf(stderr, "[objtex] submesh %zu -> texref %d -> mat %d\n", s, tr, submesh_mat[s]);
    }
    if (dye_slots > 0)
        printf("  note: %d submesh(es) use an empty dye slot -> looks like DYEABLE armor/item; the DAT\n"
               "        has no final texture (runtime-composed from dye). Exporting the grayscale base.\n", dye_slots);

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

// Resolve an asset selector to an MFT index: a plain number is the MFT index;
// "hash:N" (N decimal or 0x-hex) resolves the ANet file hash. Returns SIZE_MAX
// if unresolvable (callers range-check).
static size_t resolve_index(Dat& dat, const char* sel) {
    if (strncmp(sel, "hash:", 5) == 0) {
        int idx = dat.index_for_hash(static_cast<int>(strtol(sel + 5, nullptr, 0)));
        return idx < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(idx);
    }
    return strtoull(sel, nullptr, 0);
}

static void print_catalog_line(const CatalogEntry& e, const Label* lbl = nullptr) {
    if (lbl && !lbl->name.empty()) printf("\"%s\"  ", lbl->name.c_str());
    printf("mft=%-6u hash=%-8d %-12s ", e.mft, e.hash, type_to_string(e.type));
    if (e.w > 0) printf("%dx%d fmt=%c", e.w, e.h, e.tex_fmt ? e.tex_fmt : '-');
    else if (e.ntris > 0) printf("%dsub %dv/%dt", e.nsub, e.nverts, e.ntris);
    if (!e.tex_refs.empty()) {
        printf("  tex=[");
        for (size_t j = 0; j < e.tex_refs.size() && j < 6; ++j) printf("%s%d", j ? "," : "", e.tex_refs[j]);
        printf("%s]", e.tex_refs.size() > 6 ? ",..." : "");
    }
    printf("\n");
}

static void index_progress(size_t done, size_t total) {
    fprintf(stderr, "\r  indexing %zu / %zu ...", done, total); fflush(stderr);
}

static int cmd_index(Dat& dat, const char* outpath, size_t limit) {
    std::vector<CatalogEntry> cat;
    auto t0 = std::chrono::steady_clock::now();
    build_catalog(dat, cat, limit, index_progress);
    fprintf(stderr, "\r%50s\r", "");
    if (!write_catalog_tsv(cat, outpath)) { fprintf(stderr, "cannot write %s\n", outpath); return 2; }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    size_t models = 0, exportable = 0, textures = 0;
    for (const auto& e : cat) {
        if (e.type == FFNA_Type2) { ++models; if (e.ntris > 0) ++exportable; }
        if (e.w > 0) ++textures;
    }
    printf("indexed %zu entries in %.1fs -> %s\n", cat.size(), secs, outpath);
    printf("  models=%zu (with geometry=%zu)  textures=%zu\n", models, exportable, textures);
    return 0;
}

static int cmd_search(const char* catpath, int argc, char** argv, int argstart) {
    std::vector<CatalogEntry> cat;
    if (!read_catalog_tsv(catpath, cat)) { fprintf(stderr, "cannot read catalog: %s\n", catpath); return 2; }
    int want_type = -1, min_tris = -1, max_tris = -1, min_dim = -1;
    char want_fmt = 0; long want_hash = 0; size_t lim = 50;
    const char* labels_path = nullptr; const char* name_q = nullptr;
    bool dyeable_only = false;
    for (int i = argstart; i < argc; ++i) {
        if (!strcmp(argv[i], "--type") && i + 1 < argc) want_type = type_from_string(argv[++i]);
        else if (!strcmp(argv[i], "--min-tris") && i + 1 < argc) min_tris = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-tris") && i + 1 < argc) max_tris = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dim") && i + 1 < argc) min_dim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fmt") && i + 1 < argc) want_fmt = argv[++i][0];
        else if (!strcmp(argv[i], "--hash") && i + 1 < argc) want_hash = strtol(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--limit") && i + 1 < argc) lim = strtoull(argv[++i], nullptr, 10);
        else if (!strcmp(argv[i], "--labels") && i + 1 < argc) labels_path = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name_q = argv[++i];
        else if (!strcmp(argv[i], "--dyeable")) dyeable_only = true;
    }
    // --name implies labels; default to labels.json beside the catalog if unspecified
    Labels labels; bool have_labels = false;
    if (labels_path || name_q) {
        std::string err;
        if (!labels.load(labels_path ? labels_path : "labels.json", &err)) {
            fprintf(stderr, "labels load error: %s\n", err.c_str()); return 2;
        }
        have_labels = true;
    }
    std::unordered_set<std::string> name_keys;
    if (name_q) for (auto& k : labels.search(name_q)) name_keys.insert(k);

    size_t matched = 0, shown = 0;
    for (const auto& e : cat) {
        if (want_type >= 0 && e.type != want_type) continue;
        if (dyeable_only) {
            // heuristic: a model with an empty (0,0) dye slot is (almost always) dyeable
            // armor/weapon/item — the final texture is composed in-game from dye.
            if (e.type != FFNA_Type2) continue;
            bool dye = false;
            for (int tr : e.tex_refs) if (tr == 0 || tr == -16711935) dye = true;
            if (!dye) continue;
        }
        if (min_tris >= 0 && e.ntris < min_tris) continue;
        if (max_tris >= 0 && e.ntris > max_tris) continue;
        if (min_dim >= 0 && (e.w < min_dim || e.h < min_dim)) continue;
        if (want_fmt && e.tex_fmt != want_fmt) continue;
        if (want_hash && e.hash != static_cast<int32_t>(want_hash)) continue;
        if (name_q) {
            bool m = (e.hash && name_keys.count(Labels::hash_key(e.hash))) ||
                     name_keys.count(Labels::murmur_key(e.murmur));
            if (!m) continue;
        }
        const Label* lbl = have_labels ? labels.resolve(e.hash, e.murmur) : nullptr;
        ++matched;
        if (shown < lim) { print_catalog_line(e, lbl); ++shown; }
    }
    printf("-- %zu match(es)%s\n", matched, matched > shown ? "  (raise --limit for more)" : "");
    return 0;
}

static int cmd_show(const char* catpath, const char* sel, const char* labels_path) {
    std::vector<CatalogEntry> cat;
    if (!read_catalog_tsv(catpath, cat)) { fprintf(stderr, "cannot read catalog: %s\n", catpath); return 2; }
    const CatalogEntry* e = nullptr;
    if (!strncmp(sel, "hash:", 5)) { long h = strtol(sel + 5, nullptr, 0); for (auto& c : cat) if (c.hash == h) { e = &c; break; } }
    else if (!strncmp(sel, "murmur:", 7)) { uint32_t m = strtoul(sel + 7, nullptr, 16); for (auto& c : cat) if (c.murmur == m) { e = &c; break; } }
    else { uint32_t mft = strtoul(sel, nullptr, 10); for (auto& c : cat) if (c.mft == mft) { e = &c; break; } }
    if (!e) { fprintf(stderr, "not found: %s\n", sel); return 2; }

    printf("mft=%u hash=%d murmur=%08x type=%s usize=%d\n",
           e->mft, e->hash, e->murmur, type_to_string(e->type), e->usize);
    if (labels_path) {
        Labels labels; std::string err;
        if (labels.load(labels_path, &err)) {
            if (const Label* l = labels.resolve(e->hash, e->murmur)) {
                printf("  label: \"%s\"  category=%s  source=%s\n",
                       l->name.c_str(), l->category.c_str(), l->source.c_str());
                if (!l->tags.empty()) { printf("    tags:"); for (auto& t : l->tags) printf(" %s", t.c_str()); printf("\n"); }
                if (!l->notes.empty()) printf("    notes: %s\n", l->notes.c_str());
            } else printf("  label: (none)\n");
        } else fprintf(stderr, "  (labels load error: %s)\n", err.c_str());
    }
    if (e->w > 0) printf("  texture: %dx%d fmt=%c\n", e->w, e->h, e->tex_fmt ? e->tex_fmt : '-');
    if (e->ntris > 0) printf("  model: %d submeshes, %d verts, %d tris, amat=%d\n", e->nsub, e->nverts, e->ntris, e->amat_ref);
    if (!e->tex_refs.empty()) {
        printf("  textures referenced:\n");
        for (int tr : e->tex_refs) {
            const CatalogEntry* t = nullptr;
            for (auto& c : cat) if (c.hash == tr && c.w > 0) { t = &c; break; }
            if (t) printf("    hash %-8d -> mft %-6u  %dx%d fmt=%c\n", tr, t->mft, t->w, t->h, t->tex_fmt ? t->tex_fmt : '-');
            else   printf("    hash %-8d -> (unresolved)\n", tr);
        }
    }
    if (e->w > 0 && e->hash) {
        int users = 0;
        for (auto& c : cat) for (int tr : c.tex_refs) if (tr == e->hash) { ++users; break; }
        printf("  used by %d model(s)\n", users);
    }
    return 0;
}

// Export a full armor via the composite bridge: model_file_id -> composites.tsv
// file_ids -> sub-models + their REAL detailed textures (the ones the model's own
// FFNA refs don't point to). Groups each sub-model with the textures that follow it.
static int cmd_armor(Dat& dat, const char* comp_path, uint32_t mfid, const char* outdir, bool gray) {
    FILE* cf = fopen(comp_path, "rb");
    if (!cf) { fprintf(stderr, "cannot read composites: %s\n", comp_path); return 2; }
    uint32_t file_ids[11] = {0};
    bool found = false;
    char line[1024];
    while (fgets(line, sizeof(line), cf)) {
        if (line[0] == '#') continue;
        unsigned id = 0, flags = 0, f[11] = {0};
        int n = sscanf(line, "%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u",
                       &id, &flags, &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9], &f[10]);
        if (n >= 2 && id == mfid) { for (int k = 0; k < 11; ++k) file_ids[k] = f[k]; found = true; break; }
    }
    fclose(cf);
    if (!found) { fprintf(stderr, "model_file_id %u not found in %s\n", mfid, comp_path); return 2; }

    // Group: each FFNA sub-model, followed by the textures listed after it.
    struct Group { std::vector<uint8_t> model; std::vector<int> tex_hashes; };
    std::vector<Group> groups;
    for (int k = 0; k < 11; ++k) {
        uint32_t fid = file_ids[k];
        if (!fid) continue;
        int mi = dat.index_for_hash(static_cast<int>(fid));
        if (mi < 0) continue;
        std::vector<uint8_t> blob = dat.read_file(static_cast<size_t>(mi), true);
        int type = dat.mft()[mi].type;
        if (type == FFNA_Type2) groups.push_back({std::move(blob), {}});
        else if (type_is_texture(type) && !groups.empty()) groups.back().tex_hashes.push_back(static_cast<int>(fid));
    }
    if (groups.empty()) { fprintf(stderr, "composite %u has no FFNA sub-models\n", mfid); return 2; }

    char stem[64]; snprintf(stem, sizeof(stem), "armor_%u", mfid);
    int parts = 0, texs = 0, partial = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        Model m;
        if (!parse_model(groups[gi].model.data(), groups[gi].model.size(), m, &dat)) continue;
        char pstem[80]; snprintf(pstem, sizeof(pstem), "%s_part%zu", stem, gi);
        const std::string base = std::string(outdir) + "/" + pstem;

        std::string mtl_body; int num_mat = 0;
        for (int th : groups[gi].tex_hashes) {   // export ALL of the piece's textures, not just the first
            int ti = dat.index_for_hash(th);
            if (ti < 0) continue;
            std::vector<uint8_t> tb = dat.read_file(static_cast<size_t>(ti), true);
            Texture tex;
            if (!decode_texture(tb.data(), tb.size(), tex)) continue;
            if (gray) tint_gray(tex);
            char png[96]; snprintf(png, sizeof(png), "%s_tex%d.png", pstem, num_mat);
            if (!write_png(tex, (std::string(outdir) + "/" + png).c_str())) continue;
            if (num_mat == 0) {   // the first is the MTL's diffuse (map_Kd); GW blends the rest,
                char mat[256];    // which OBJ/MTL can't represent -> they're exported for use in a DCC.
                snprintf(mat, sizeof(mat), "newmtl mat0\nKd 1 1 1\nmap_Kd %s\n", png);
                mtl_body += mat;
            }
            if (tex.needed_asm) ++partial;
            ++num_mat; ++texs;
        }
        std::vector<int> submesh_mat(m.submeshes.size(), num_mat > 0 ? 0 : -1);
        if (num_mat > 0) { FILE* mf = fopen((base + ".mtl").c_str(), "wb"); if (mf) { fputs(mtl_body.c_str(), mf); fclose(mf); } }
        write_obj_textured(m, base + ".obj", num_mat > 0 ? (std::string(pstem) + ".mtl") : "", submesh_mat);
        ++parts;
    }
    printf("armor %u -> %s/%s_part*.{obj,mtl,png}  (%d sub-models, %d textures%s%s)\n",
           mfid, outdir, stem, parts, texs, gray ? ", gray-dyed" : "", partial ? ", some partial (asm on non-Windows)" : "");
    return 0;
}

static int cmd_label(int argc, char** argv, int start) {
    // start: <labels.json> <key> <name> [flags]
    if (argc < start + 3) { usage(); return 1; }
    const char* path = argv[start];
    std::string key = argv[start + 1];
    if (key.rfind("hash:", 0) != 0 && key.rfind("murmur:", 0) != 0) key = "hash:" + key; // bare number -> hash:
    Label l; l.name = argv[start + 2]; l.source = "manual";
    for (int i = start + 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--category") && i + 1 < argc) l.category = argv[++i];
        else if (!strcmp(argv[i], "--tag") && i + 1 < argc) l.tags.push_back(argv[++i]);
        else if (!strcmp(argv[i], "--source") && i + 1 < argc) l.source = argv[++i];
        else if (!strcmp(argv[i], "--notes") && i + 1 < argc) l.notes = argv[++i];
    }
    Labels labels; std::string err;
    if (!labels.load(path, &err)) { fprintf(stderr, "labels load error: %s\n", err.c_str()); return 2; }
    labels.set(key, l);
    if (!labels.save(path)) { fprintf(stderr, "cannot write %s\n", path); return 2; }
    printf("labeled %s = \"%s\"  (%zu labels total)\n", key.c_str(), l.name.c_str(), labels.size());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    std::string cmd = argv[1];

    // Catalog / label commands operate on files (argv[2..]), not the dat.
    if (cmd == "search") { if (argc < 3) { usage(); return 1; } return cmd_search(argv[2], argc, argv, 3); }
    if (cmd == "show") {
        if (argc < 4) { usage(); return 1; }
        const char* lp = nullptr;
        for (int i = 4; i + 1 < argc; ++i) if (!strcmp(argv[i], "--labels")) lp = argv[i + 1];
        return cmd_show(argv[2], argv[3], lp);
    }
    if (cmd == "label") return cmd_label(argc, argv, 2);

    const char* datpath = nullptr;
    bool bare = (cmd != "info" && cmd != "census" && cmd != "extract" && cmd != "obj" &&
                 cmd != "scan" && cmd != "tex" && cmd != "texscan" && cmd != "objtex" &&
                 cmd != "index" && cmd != "search" && cmd != "show" && cmd != "label" &&
                 cmd != "armor");
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
    if (cmd == "index") {
        std::string out = (argc >= 4) ? argv[3] : (std::string(datpath) + ".catalog.tsv");
        size_t limit = (argc >= 5) ? strtoull(argv[4], nullptr, 10) : 0;
        return cmd_index(dat, out.c_str(), limit);
    }
    if (cmd == "extract") {
        if (argc < 5) { usage(); return 1; }
        return cmd_extract(dat, resolve_index(dat, argv[3]), argv[4]);
    }
    if (cmd == "obj") {
        if (argc < 5) { usage(); return 1; }
        return cmd_obj(dat, resolve_index(dat, argv[3]), argv[4]);
    }
    if (cmd == "objtex") {
        if (argc < 5) { usage(); return 1; }
        return cmd_objtex(dat, resolve_index(dat, argv[3]), argv[4]);
    }
    if (cmd == "armor") {
        if (argc < 6) { usage(); return 1; }
        bool gray = false;
        for (int i = 6; i < argc; ++i) if (!strcmp(argv[i], "--gray")) gray = true;
        return cmd_armor(dat, argv[3], static_cast<uint32_t>(strtoul(argv[4], nullptr, 0)), argv[5], gray);
    }
    if (cmd == "scan") {
        size_t limit = 40000;
        if (argc >= 4) limit = strtoull(argv[3], nullptr, 10);
        return cmd_scan(dat, limit);
    }
    if (cmd == "tex") {
        if (argc < 5) { usage(); return 1; }
        return cmd_tex(dat, resolve_index(dat, argv[3]), argv[4]);
    }
    if (cmd == "texscan") {
        size_t limit = 40000;
        if (argc >= 4) limit = strtoull(argv[3], nullptr, 10);
        return cmd_texscan(dat, limit);
    }
    usage();
    return 1;
}
