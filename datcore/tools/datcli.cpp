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
#include <filesystem>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace datcore;
namespace fs = std::filesystem;

static bool file_exists(const std::string& p) {
    std::error_code ec; return !p.empty() && fs::is_regular_file(p, ec);
}

// Resolve Gw.dat: explicit hint -> $GUILDLITE_GW_DAT -> ./Gw.dat -> Documents. "" if none.
static std::string resolve_dat(const std::string& hint) {
    std::vector<std::string> c;
    if (!hint.empty()) c.push_back(hint);
    if (const char* e = getenv("GUILDLITE_GW_DAT")) c.push_back(e);
    c.push_back("Gw.dat");
    if (const char* h = getenv("HOME")) {
        c.push_back(std::string(h) + "/Documents/Guild Wars/Gw.dat");
        c.push_back(std::string(h) + "/Documents/guildlite/Gw.dat");
    }
    if (const char* u = getenv("USERPROFILE")) {
        c.push_back(std::string(u) + "\\Documents\\Guild Wars\\Gw.dat");
        c.push_back(std::string(u) + "\\Documents\\guildlite\\Gw.dat");
    }
    for (const auto& p : c) if (file_exists(p)) return p;
    return "";
}

static void print_dat_help(const std::string& hint) {
    std::string tried = hint.empty() ? "" : (std::string(" (tried '") + hint + "')");
    fprintf(stderr,
        "error: Gw.dat not found%s.\n"
        "  Provide it: pass the path, set GUILDLITE_GW_DAT=/path/to/Gw.dat, or place it\n"
        "  at ./Gw.dat or <Documents>/Guild Wars/Gw.dat. A partial Gw.dat only holds assets\n"
        "  loaded in-game; for a COMPLETE archive run Guild Wars' image download: Gw.exe -image\n",
        tried.c_str());
}

// Compendium dir (catalog/composites/labels): $GUILDLITE_DATA, else Gw.dat's dir, else ".".
static std::string data_dir(const std::string& dat) {
    if (const char* e = getenv("GUILDLITE_DATA")) return e;
    if (!dat.empty()) { fs::path p(dat); if (p.has_parent_path()) return p.parent_path().string(); }
    return ".";
}

// armors.tsv (provided by us): $GUILDLITE_ARMORS -> <data>/armors.tsv -> repo copy.
static std::string find_armors(const std::string& ddir) {
    if (const char* e = getenv("GUILDLITE_ARMORS")) if (file_exists(e)) return e;
    for (const std::string& p : { ddir + "/armors.tsv", std::string("datcore/data/armors.tsv"),
                                  std::string("data/armors.tsv") })
        if (file_exists(p)) return p;
    return "";
}

// Equipment category from the GWToolbox ItemType/slot name.
static const char* item_category(const char* slot) {
    static const char* weapons[] = {"Axe","Sword","Staff","Wand","Spear","Shield",
                                     "Scythe","Offhand","Hammer","Daggers","Bow"};
    for (const char* w : weapons) if (!strcmp(slot, w)) return "weapon";
    if (!strncmp(slot, "Costume", 7)) return "costume";
    return "armor";
}

static void usage() {
    fprintf(stderr,
            "usage:\n"
            "  <sel> = MFT index or hash:<n>\n"
            "  Flags for Gw.dat, catalog.tsv, etc. are optional; datcli uses $GUILDLITE_GW_DAT / ./Gw.dat\n"
            "  and defaults data files to $GUILDLITE_DATA or Gw.dat's directory.\n\n"
            "  datcli setup   [--dat <path>] [--force]     provision catalog + labels (START HERE)\n\n"
            "  datcli index   [--dat <path>] [--out <catalog.tsv>] [--limit N]\n"
            "  datcli tag-armor [--armors <armors.tsv>] [--composites <composites.tsv>] [--catalog <catalog.tsv>] [--labels <labels.json>]\n\n"
            "Usage:\n"
            "  datcli objtex  <sel> <outdir> [--dat <path>]\n"
            "  datcli armor   <model_file_id> <outdir> [--dat <path>] [--composites <composites.tsv>] [--gray]\n"
            "  datcli obj     <sel> <out.obj> [--dat <path>]\n"
            "  datcli tex     <sel> <out.png> [--dat <path>]\n"
            "  datcli extract <sel> <out> [--dat <path>]\n\n"
            "Catalog/Search:\n"
            "  datcli label   <sel> <name> [--labels <labels.json>] [--category C] [--tag T]... [--source S] [--notes N]\n"
            "  datcli show    <sel> [--catalog <catalog.tsv>] [--labels <labels.json>]\n"
            "  datcli search  [--catalog <catalog.tsv>] [--labels <labels.json>] [--type T] [--min-tris N] [--max-tris N]\n"
            "                 [--dim N] [--fmt C] [--hash H] [--limit N] [--dyeable] [--name QUERY]\n\n"
            "Debugging:\n"
            "  datcli info    [--dat <path>]\n"
            "  datcli census  [--dat <path>] [--limit N]\n"
            "  datcli scan    [--dat <path>] [--limit N]\n"
            "  datcli texscan [--dat <path>] [--limit N]\n"
            );
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

static int cmd_search(const char* catpath, const std::vector<std::string>& args, size_t lim, const char* labels_path, bool explicit_labels) {
    std::vector<CatalogEntry> cat;
    if (!read_catalog_tsv(catpath, cat)) { fprintf(stderr, "cannot read catalog: %s\n", catpath); return 2; }
    int want_type = -1, min_tris = -1, max_tris = -1, min_dim = -1;
    char want_fmt = 0; long want_hash = 0;
    const char* name_q = nullptr;
    bool dyeable_only = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--type" && i + 1 < args.size()) want_type = type_from_string(args[++i].c_str());
        else if (args[i] == "--min-tris" && i + 1 < args.size()) min_tris = atoi(args[++i].c_str());
        else if (args[i] == "--max-tris" && i + 1 < args.size()) max_tris = atoi(args[++i].c_str());
        else if (args[i] == "--dim" && i + 1 < args.size()) min_dim = atoi(args[++i].c_str());
        else if (args[i] == "--fmt" && i + 1 < args.size()) want_fmt = args[++i][0];
        else if (args[i] == "--hash" && i + 1 < args.size()) want_hash = strtol(args[++i].c_str(), nullptr, 0);
        else if (args[i] == "--name" && i + 1 < args.size()) name_q = args[++i].c_str();
        else if (args[i] == "--dyeable") dyeable_only = true;
    }
    Labels labels; bool have_labels = false;
    if (labels_path && file_exists(labels_path)) {
        std::string err;
        if (labels.load(labels_path, &err)) have_labels = true;
        else if (explicit_labels) { fprintf(stderr, "labels load error: %s\n", err.c_str()); return 2; }
    } else if (explicit_labels) {
        fprintf(stderr, "labels file not found: %s\n", labels_path); return 2;
    }
    std::unordered_set<std::string> name_keys;
    if (name_q) for (auto& k : labels.search(name_q)) name_keys.insert(k);

    size_t matched = 0, shown = 0;
    for (const auto& e : cat) {
        if (want_type >= 0 && e.type != want_type) continue;
        if (dyeable_only) {
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

static int cmd_show(const char* catpath, const char* sel, const char* labels_path, bool explicit_labels) {
    std::vector<CatalogEntry> cat;
    if (!read_catalog_tsv(catpath, cat)) { fprintf(stderr, "cannot read catalog: %s\n", catpath); return 2; }
    const CatalogEntry* e = nullptr;
    if (!strncmp(sel, "hash:", 5)) { long h = strtol(sel + 5, nullptr, 0); for (auto& c : cat) if (c.hash == h) { e = &c; break; } }
    else if (!strncmp(sel, "murmur:", 7)) { uint32_t m = strtoul(sel + 7, nullptr, 16); for (auto& c : cat) if (c.murmur == m) { e = &c; break; } }
    else { uint32_t mft = strtoul(sel, nullptr, 10); for (auto& c : cat) if (c.mft == mft) { e = &c; break; } }
    if (!e) { fprintf(stderr, "not found: %s\n", sel); return 2; }

    printf("mft=%u hash=%d murmur=%08x type=%s usize=%d\n",
           e->mft, e->hash, e->murmur, type_to_string(e->type), e->usize);
    if (labels_path && file_exists(labels_path)) {
        Labels labels; std::string err;
        if (labels.load(labels_path, &err)) {
            if (const Label* l = labels.resolve(e->hash, e->murmur)) {
                printf("  label: \"%s\"  category=%s  source=%s\n",
                       l->name.c_str(), l->category.c_str(), l->source.c_str());
                if (!l->tags.empty()) { printf("    tags:"); for (auto& t : l->tags) printf(" %s", t.c_str()); printf("\n"); }
                if (!l->notes.empty()) printf("    notes: %s\n", l->notes.c_str());
            } else printf("  label: (none)\n");
        } else {
            if (explicit_labels) fprintf(stderr, "  (labels load error: %s)\n", err.c_str());
            else printf("  (labels load error: %s)\n", err.c_str());
        }
    } else if (explicit_labels) {
        fprintf(stderr, "labels file not found: %s\n", labels_path);
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

// Join armors.tsv (model_file_id -> name/prof/slot) x composites.tsv (model_file_id ->
// file_ids) x catalog (hash -> type) and write labels.json: every armor sub-model hash
// gets its real GW name + category "armor" + prof/slot/campaign tags. This is the
// definitive named "is armor" flag AND the bulk labeling of our DAT models.
static int cmd_tag_armor(const char* armors_path, const char* comp_path, const char* cat_path, const char* labels_path) {
    std::vector<CatalogEntry> cat;
    if (!read_catalog_tsv(cat_path, cat)) { fprintf(stderr, "cannot read catalog: %s\n", cat_path); return 2; }
    std::unordered_map<int, int> hash_type;
    for (const auto& e : cat) if (e.hash) hash_type[e.hash] = e.type;

    std::unordered_map<uint32_t, std::vector<int>> comp;
    if (FILE* f = fopen(comp_path, "rb")) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#') continue;
            unsigned id = 0, flags = 0, fd[11] = {0};
            int n = sscanf(line, "%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u",
                           &id, &flags, &fd[0], &fd[1], &fd[2], &fd[3], &fd[4], &fd[5], &fd[6], &fd[7], &fd[8], &fd[9], &fd[10]);
            if (n < 2) continue;
            std::vector<int> v;
            for (int k = 0; k < 11; ++k) if (fd[k]) v.push_back(static_cast<int>(fd[k]));
            comp[id] = std::move(v);
        }
        fclose(f);
    } else { fprintf(stderr, "cannot read composites: %s\n", comp_path); return 2; }

    Labels labels; std::string err;
    if (!labels.load(labels_path, &err)) { fprintf(stderr, "labels load error: %s\n", err.c_str()); return 2; }

    FILE* af = fopen(armors_path, "rb");
    if (!af) { fprintf(stderr, "cannot read armors: %s\n", armors_path); return 2; }
    auto make_label = [](char** f) {
        Label l;
        l.name = f[2];
        const char* c = item_category(f[4]);
        l.category = c;
        l.source = "armor-join";
        l.tags = { f[3], f[4], f[5] };                              // profession, slot/weapon-type, campaign
        if (strcmp(c, "weapon") != 0) l.tags.push_back("dyeable");  // armor/costume are dyeable
        return l;
    };
    char line[512];
    int items = 0, unresolved = 0, labeled = 0;
    while (fgets(line, sizeof(line), af)) {
        if (line[0] == '#') continue;
        char* fields[7] = {0}; int nf = 0;
        for (char* t = strtok(line, "\t\r\n"); t && nf < 7; t = strtok(nullptr, "\t\r\n")) fields[nf++] = t;
        if (nf < 7) continue;
        uint32_t mfid = static_cast<uint32_t>(strtoul(fields[0], nullptr, 10));
        ++items;
        auto it = comp.find(mfid);
        if (it != comp.end()) {
            // composite (armor / costume): label each FFNA sub-model hash (male + female)
            bool any = false;
            for (int fid : it->second) {
                auto ht = hash_type.find(fid);
                if (ht == hash_type.end() || ht->second != FFNA_Type2) continue;
                labels.set(Labels::hash_key(fid), make_label(fields));
                ++labeled; any = true;
            }
            if (!any) ++unresolved;
        } else {
            // no composite: a single-model item (weapon) whose model_file_id IS the DAT hash
            auto ht = hash_type.find(static_cast<int>(mfid));
            if (ht != hash_type.end() && ht->second == FFNA_Type2) {
                labels.set(Labels::hash_key(static_cast<int>(mfid)), make_label(fields));
                ++labeled;
            } else ++unresolved;
        }
    }
    fclose(af);
    if (!labels.save(labels_path)) { fprintf(stderr, "cannot write %s\n", labels_path); return 2; }
    printf("tag-armor: %d items (%d unresolved) -> %d hashes labeled (armor via composites, weapons direct) -> %s (%zu labels total)\n",
           items, unresolved, labeled, labels_path, labels.size());
    return 0;
}

static int cmd_label(const std::vector<std::string>& args, const char* labels_path) {
    if (args.size() < 3) { usage(); return 1; }
    std::string key = args[1];
    if (key.rfind("hash:", 0) != 0 && key.rfind("murmur:", 0) != 0) key = "hash:" + key;
    Label l; l.name = args[2]; l.source = "manual";
    for (size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--category" && i + 1 < args.size()) l.category = args[++i];
        else if (args[i] == "--tag" && i + 1 < args.size()) l.tags.push_back(args[++i]);
        else if (args[i] == "--source" && i + 1 < args.size()) l.source = args[++i];
        else if (args[i] == "--notes" && i + 1 < args.size()) l.notes = args[++i];
    }
    Labels labels; std::string err;
    if (!labels.load(labels_path, &err)) {
        if (file_exists(labels_path)) { fprintf(stderr, "labels load error: %s\n", err.c_str()); return 2; }
    }
    labels.set(key, l);
    if (!labels.save(labels_path)) { fprintf(stderr, "cannot write %s\n", labels_path); return 2; }
    printf("labeled %s = \"%s\"  (%zu labels total)\n", key.c_str(), l.name.c_str(), labels.size());
    return 0;
}

static int cmd_setup(const std::string& dat_hint, bool force) {
    std::string dat = resolve_dat(dat_hint);
    if (dat.empty()) { print_dat_help(dat_hint); return 2; }
    const std::string ddir = data_dir(dat);
    std::error_code ec; fs::create_directories(ddir, ec);
    const std::string cat = ddir + "/catalog.tsv";
    const std::string labels = ddir + "/labels.json";
    const std::string comps = ddir + "/composites.tsv";
    printf("datcore setup\n  Gw.dat   : %s\n  data dir : %s\n", dat.c_str(), ddir.c_str());

    if (force || !file_exists(cat)) {
        Dat d;
        if (!d.open(dat)) { fprintf(stderr, "error: cannot open %s: %s\n", dat.c_str(), d.error().c_str()); return 2; }
        printf("  [1/2] indexing the dat (scans everything, ~40s)...\n");
        cmd_index(d, cat.c_str(), 0);
    } else {
        printf("  [1/2] catalog exists: %s  (--force to rebuild)\n", cat.c_str());
    }

    // armors.tsv is provided by us; place it in the data dir so it's found + scp-able together.
    std::string armors = find_armors(ddir);
    if (!armors.empty() && !file_exists(ddir + "/armors.tsv")) {
        fs::copy_file(armors, ddir + "/armors.tsv", fs::copy_options::overwrite_existing, ec);
        armors = ddir + "/armors.tsv";
    }

    if (armors.empty()) {
        printf("  [2/2] labels SKIPPED: armors.tsv not found. Provide datcore/data/armors.tsv\n"
               "        (or set GUILDLITE_ARMORS) -- that's where names/professions/slots come from.\n");
    } else if (file_exists(comps)) {
        printf("  [2/2] labeling (armors x composites x catalog)...\n");
        cmd_tag_armor(armors.c_str(), comps.c_str(), cat.c_str(), labels.c_str());
    } else {
        printf("  [2/2] labels SKIPPED: %s/composites.tsv not found.\n"
               "        Composites (model->texture) live in GAME MEMORY, so this one step is in-game:\n"
               "          1. in Guildlite, run:  edit composites\n"
               "          2. scp  Documents\\guildlite\\composites.tsv  ->  %s/\n"
               "          3. re-run:  datcli setup\n", ddir.c_str(), ddir.c_str());
    }
    printf("done -> catalog=%s labels=%s.  try:  datcli search --name ranger\n",
           file_exists(cat) ? "ok" : "-", file_exists(labels) ? "ok" : "pending");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }

    std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help") { usage(); return 0; }

    std::unordered_set<std::string> cmds = {"setup", "search", "show", "label", "tag-armor", "info", "census", "scan", "texscan", "index", "extract", "obj", "objtex", "tex", "armor"};
    std::vector<std::string> args;
    std::string dat_arg, catalog_arg, labels_arg, composites_arg, armors_arg, out_arg;
    size_t limit_arg = 0; bool has_limit = false;
    bool force_arg = false, gray_arg = false;

    if (cmds.find(cmd) == cmds.end()) {
        dat_arg = cmd;
        cmd = "census";
        args.push_back("census");
    } else {
        args.push_back(cmd);
    }

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dat" && i + 1 < argc) dat_arg = argv[++i];
        else if (arg == "--catalog" && i + 1 < argc) catalog_arg = argv[++i];
        else if (arg == "--labels" && i + 1 < argc) labels_arg = argv[++i];
        else if (arg == "--composites" && i + 1 < argc) composites_arg = argv[++i];
        else if (arg == "--armors" && i + 1 < argc) armors_arg = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out_arg = argv[++i];
        else if (arg == "--limit" && i + 1 < argc) { limit_arg = strtoull(argv[++i], nullptr, 10); has_limit = true; }
        else if (arg == "--force") force_arg = true;
        else if (arg == "--gray") gray_arg = true;
        else args.push_back(arg);
    }

    if (cmd == "setup") {
        return cmd_setup(dat_arg, force_arg);
    }

    std::string datpath = resolve_dat(dat_arg);
    std::string ddir = data_dir(datpath);
    std::string def_cat = ddir + "/catalog.tsv";
    std::string def_labels = ddir + "/labels.json";
    std::string def_comps = ddir + "/composites.tsv";
    
    std::string cat_path = catalog_arg.empty() ? def_cat : catalog_arg;
    std::string labels_path = labels_arg.empty() ? def_labels : labels_arg;
    std::string comp_path = composites_arg.empty() ? def_comps : composites_arg;
    std::string armors_path = armors_arg.empty() ? find_armors(ddir) : armors_arg;

    if (cmd == "search") {
        size_t lim = has_limit ? limit_arg : 50;
        return cmd_search(cat_path.c_str(), args, lim, labels_path.c_str(), !labels_arg.empty());
    }
    if (cmd == "show") {
        if (args.size() < 2) { usage(); return 1; }
        return cmd_show(cat_path.c_str(), args[1].c_str(), labels_path.c_str(), !labels_arg.empty());
    }
    if (cmd == "label") {
        return cmd_label(args, labels_path.c_str());
    }
    if (cmd == "tag-armor") {
        if (armors_path.empty()) { fprintf(stderr, "armors.tsv not found\n"); return 1; }
        return cmd_tag_armor(armors_path.c_str(), comp_path.c_str(), cat_path.c_str(), labels_path.c_str());
    }

    if (datpath.empty()) { print_dat_help(dat_arg); return 1; }

    Dat dat;
    if (cmd != "index") fprintf(stderr, "opening %s ...\n", datpath.c_str());
    if (!dat.open(datpath)) { fprintf(stderr, "error: %s\n", dat.error().c_str()); return 1; }
    if (cmd != "index") fprintf(stderr, "opened: %zu MFT entries\n", dat.num_files());

    if (cmd == "info") return cmd_info(dat);
    if (cmd == "census") return cmd_census(dat, has_limit ? limit_arg : 20000);
    if (cmd == "scan") return cmd_scan(dat, has_limit ? limit_arg : 40000);
    if (cmd == "texscan") return cmd_texscan(dat, has_limit ? limit_arg : 40000);
    
    if (cmd == "index") {
        std::string out = out_arg.empty() ? def_cat : out_arg;
        return cmd_index(dat, out.c_str(), limit_arg);
    }
    if (cmd == "extract") {
        if (args.size() < 3) { usage(); return 1; }
        return cmd_extract(dat, resolve_index(dat, args[1].c_str()), args[2].c_str());
    }
    if (cmd == "obj") {
        if (args.size() < 3) { usage(); return 1; }
        return cmd_obj(dat, resolve_index(dat, args[1].c_str()), args[2].c_str());
    }
    if (cmd == "objtex") {
        if (args.size() < 3) { usage(); return 1; }
        return cmd_objtex(dat, resolve_index(dat, args[1].c_str()), args[2].c_str());
    }
    if (cmd == "tex") {
        if (args.size() < 3) { usage(); return 1; }
        return cmd_tex(dat, resolve_index(dat, args[1].c_str()), args[2].c_str());
    }
    if (cmd == "armor") {
        if (args.size() < 3) { usage(); return 1; }
        return cmd_armor(dat, comp_path.c_str(), static_cast<uint32_t>(strtoul(args[1].c_str(), nullptr, 0)), args[2].c_str(), gray_arg);
    }
    
    usage();
    return 1;
}
