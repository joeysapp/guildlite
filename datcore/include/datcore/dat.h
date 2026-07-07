#pragma once
// Portable Guild Wars .dat archive reader.
// Ported from GuildWarsMapBrowser/GWDatBrowser (GWUnpacker), with the Win32 file
// I/O (CreateFile/SetFilePointerEx/ReadFile) replaced by 64-bit stdio so it builds
// and runs on macOS / Linux / Windows alike. No DirectX, no Win32, no MFC.
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace datcore {

// File type tags, in the same order/meaning as GWMB's FileType enum so ported
// downstream code (FFNA model parse, ATEX decode) can compare against these.
enum FileType {
    NONE,
    AMAT,
    AMP,
    ATEXDXT1, ATEXDXT2, ATEXDXT3, ATEXDXT4, ATEXDXT5, ATEXDXTN, ATEXDXTA, ATEXDXTL,
    ATTXDXT1, ATTXDXT3, ATTXDXT5, ATTXDXTN, ATTXDXTA, ATTXDXTL,
    DDS,
    FFNA_Type2,   // model
    FFNA_Type3,   // map
    FFNA_Unknown,
    MFTBASE,
    NOTREAD,
    SOUND,
    TEXT,
    UNKNOWN,
    FILETYPE_COUNT
};

const char* type_to_string(int type);
bool type_is_texture(int type);

// One master-file-table entry. The first 0x18 bytes (offset..crc) come straight
// from disk; the rest are filled in lazily by read_file().
struct MFTEntry {
    int64_t  offset = 0;             // byte offset of the (compressed) blob in the .dat
    int32_t  size = 0;               // compressed size on disk
    uint16_t a = 0;                  // != 0  => blob is compressed
    uint8_t  b = 0;                  // == 0  => base/placeholder entry (unreadable)
    uint8_t  c = 0;
    int32_t  id = 0;
    int32_t  crc = 0;
    int      type = NOTREAD;         // filled by read_file()
    int32_t  uncompressed_size = -1; // filled by read_file()
    int32_t  hash = 0;               // ANet file number (0 if none)
    uint32_t murmurhash3 = 0;        // content hash, filled by read_file()
};

class Dat {
public:
    Dat() = default;
    ~Dat();
    Dat(const Dat&) = delete;
    Dat& operator=(const Dat&) = delete;

    // Open the archive, validate the magic, and read the whole master file table.
    // Returns false (with an error string) on any problem.
    bool open(const std::string& path);
    bool is_open() const { return fp_ != nullptr; }
    const std::string& error() const { return error_; }

    size_t num_files() const { return mft_.size(); }
    const std::vector<MFTEntry>& mft() const { return mft_; }
    const MFTEntry& operator[](size_t n) const { return mft_[n]; }

    // Read entry n; if translate is true, decompress + classify (fills type,
    // uncompressed_size, murmurhash3). Returns the decompressed bytes, or an
    // empty vector for base/placeholder entries or read errors.
    std::vector<uint8_t> read_file(size_t n, bool translate = true);

    // ANet file number (hash) -> MFT index, or -1. Built during open().
    int index_for_hash(int hash) const;

    // Resolve a two-id cross-file reference (as stored in model/map files) to an
    // MFT index: decode_filename() -> hash -> index. Returns -1 if unknown.
    int index_for_fileref(int id0, int id1) const;

    int sector_size() const { return sector_size_; }

private:
    FILE*       fp_ = nullptr;
    std::string path_;
    std::string error_;
    int64_t     mft_offset_ = 0;
    int         sector_size_ = 0;
    std::vector<MFTEntry> mft_;
    std::unordered_map<int, int> hash_index_;
};

// (id0 - 0xff00ff) + (id1 * 0xff00)  — GW's model/map file cross-reference decode.
inline int decode_filename(int id0, int id1) { return (id0 - 0xff00ff) + (id1 * 0xff00); }

} // namespace datcore
