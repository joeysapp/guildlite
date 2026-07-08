#pragma once
// A persistent, greppable index of a Gw.dat: one record per MFT entry with its
// stable ids (hash + murmur content-hash), type, and per-model / per-texture
// metadata + references. Built once (slow), then searched (fast). Shared by the
// CLI and, later, the in-game browser.
#include <cstdint>
#include <string>
#include <vector>

namespace datcore {

class Dat;

struct CatalogEntry {
    uint32_t mft = 0;          // MFT index (a per-dat-version handle, NOT stable)
    int32_t  hash = 0;         // ANet file number — stable/semantic, 0 if none
    uint32_t murmur = 0;       // content hash — stable while the bytes are unchanged
    int      type = 0;         // FileType
    int32_t  usize = 0;        // uncompressed size
    int      w = 0, h = 0;     // texture dimensions (0 for non-textures)
    char     tex_fmt = 0;      // texture format char ('1'/'3'/'5'/'N'/'A'/'L'), 0 if n/a
    int      nsub = 0, nverts = 0, ntris = 0; // model geometry (0 for non-models)
    std::vector<int32_t> tex_refs;  // texture file-hashes this model references (0xFA5)
    int32_t  amat_ref = 0;          // material file-hash (0xFAD), 0 if none
};

// Build the catalog by scanning the dat (decompress + classify + extract model /
// texture metadata). limit==0 => all entries. progress_cb(done,total) is optional.
bool build_catalog(Dat& dat, std::vector<CatalogEntry>& out, size_t limit = 0,
                   void (*progress_cb)(size_t done, size_t total) = nullptr);

// Tab-separated, with a header comment line. Greppable (type names are strings;
// tex_refs are a comma-separated last column).
bool write_catalog_tsv(const std::vector<CatalogEntry>& cat, const std::string& path);
bool read_catalog_tsv(const std::string& path, std::vector<CatalogEntry>& out);

} // namespace datcore
