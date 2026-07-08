#include "datcore/dat.h"
#include "datcore/xentax.h"
#include "datcore/murmur3.h"

#include <algorithm>
#include <cstring>

namespace datcore {

// ---- little-endian byte readers (no reliance on struct packing) -------------
namespace {

inline uint16_t rd_u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline int32_t rd_i32(const uint8_t* p) { return int32_t(rd_u32(p)); }
inline int64_t rd_i64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return int64_t(v);
}

// Build the same integer a multi-char literal 'ABCD' produces, so the type
// switch reads identically to GWMB while staying warning-clean on clang.
constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
           (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d));
}

// Portable 64-bit seek (the .dat is > 4 GB, so 32-bit offsets overflow).
inline int seek64(FILE* fp, int64_t off) {
#if defined(_WIN32)
    return _fseeki64(fp, off, SEEK_SET);
#else
    return fseeko(fp, static_cast<off_t>(off), SEEK_SET);
#endif
}

void parse_entry(const uint8_t* buf, MFTEntry& e) {
    e.offset = rd_i64(buf + 0);
    e.size   = rd_i32(buf + 8);
    e.a      = rd_u16(buf + 12);
    e.b      = buf[14];
    e.c      = buf[15];
    e.id     = rd_i32(buf + 16);
    e.crc    = rd_i32(buf + 20);
    e.type   = NOTREAD;
    e.uncompressed_size = -1;
    e.hash   = 0;
}

// Classify a decompressed blob by its leading dwords — a faithful port of the
// GWUnpacker::readFile type switch.
int detect_type(const uint8_t* d, size_t n) {
    if (n < 8) return UNKNOWN;
    const uint32_t i  = rd_u32(d);
    const uint32_t k  = rd_u32(d + 4);
    const uint8_t  sub = d[4];
    const uint32_t i2 = i & 0xffff;
    const uint32_t i3 = i & 0xffffff;

    int type = UNKNOWN;
    if (i == fourcc('X', 'T', 'T', 'A')) {          // "ATTX"
        if      (k == fourcc('1', 'T', 'X', 'D')) type = ATTXDXT1;
        else if (k == fourcc('3', 'T', 'X', 'D')) type = ATTXDXT3;
        else if (k == fourcc('5', 'T', 'X', 'D')) type = ATTXDXT5;
        else if (k == fourcc('N', 'T', 'X', 'D')) type = ATTXDXTN;
        else if (k == fourcc('A', 'T', 'X', 'D')) type = ATTXDXTA;
        else if (k == fourcc('L', 'T', 'X', 'D')) type = ATTXDXTL;
    } else if (i == fourcc('X', 'E', 'T', 'A')) {    // "ATEX"
        if      (k == fourcc('1', 'T', 'X', 'D')) type = ATEXDXT1;
        else if (k == fourcc('2', 'T', 'X', 'D')) type = ATEXDXT2;
        else if (k == fourcc('3', 'T', 'X', 'D')) type = ATEXDXT3;
        else if (k == fourcc('4', 'T', 'X', 'D')) type = ATEXDXT4;
        else if (k == fourcc('5', 'T', 'X', 'D')) type = ATEXDXT5;
        else if (k == fourcc('N', 'T', 'X', 'D')) type = ATEXDXTN;
        else if (k == fourcc('A', 'T', 'X', 'D')) type = ATEXDXTA;
        else if (k == fourcc('L', 'T', 'X', 'D')) type = ATEXDXTL;
    } else if (i == fourcc('=', '=', '=', ';') || i == fourcc('*', '*', '*', ';')) {
        type = TEXT;
    } else if (i == fourcc('a', 'n', 'f', 'f')) {    // "ffna"
        type = (sub == 2) ? FFNA_Type2 : (sub == 3) ? FFNA_Type3 : FFNA_Unknown;
    } else if (i == fourcc(' ', 'S', 'D', 'D')) {    // "DDS "
        type = DDS;
    } else if (i == fourcc('T', 'A', 'M', 'A')) {    // "AMAT"
        type = AMAT;
    }

    if (i2 == 0xFAFF || i2 == 0xFBFF) type = SOUND;   // MPEG frame sync
    if (i3 == 0x504D41)      type = AMP;              // "AMP"
    else if (i3 == 0x334449) type = SOUND;           // "ID3"

    return type;
}

} // namespace

const char* type_to_string(int type) {
    switch (type) {
    case NONE: return " ";
    case AMAT: return "AMAT";
    case AMP: return "Amp";
    case ATEXDXT1: return "ATEXDXT1";
    case ATEXDXT2: return "ATEXDXT2";
    case ATEXDXT3: return "ATEXDXT3";
    case ATEXDXT4: return "ATEXDXT4";
    case ATEXDXT5: return "ATEXDXT5";
    case ATEXDXTN: return "ATEXDXTN";
    case ATEXDXTA: return "ATEXDXTA";
    case ATEXDXTL: return "ATEXDXTL";
    case ATTXDXT1: return "ATTXDXT1";
    case ATTXDXT3: return "ATTXDXT3";
    case ATTXDXT5: return "ATTXDXT5";
    case ATTXDXTN: return "ATTXDXTN";
    case ATTXDXTA: return "ATTXDXTA";
    case ATTXDXTL: return "ATTXDXTL";
    case DDS: return "DDS";
    case FFNA_Type2: return "FFNA-Model";
    case FFNA_Type3: return "FFNA-Map";
    case FFNA_Unknown: return "FFNA-Unknown";
    case MFTBASE: return "MFTBase";
    case NOTREAD: return "NotRead";
    case SOUND: return "Sound";
    case TEXT: return "Text";
    default: return "Unknown";
    }
}

int type_from_string(const char* s) {
    if (!s) return UNKNOWN;
    for (int t = 0; t < FILETYPE_COUNT; ++t)
        if (std::strcmp(type_to_string(t), s) == 0) return t;
    return UNKNOWN;
}

bool type_is_texture(int type) {
    return (type >= ATEXDXT1 && type <= ATTXDXTL) || type == DDS;
}

Dat::~Dat() {
    if (fp_) fclose(fp_);
}

bool Dat::open(const std::string& path) {
    path_ = path;
    error_.clear();
    fp_ = fopen(path.c_str(), "rb");
    if (!fp_) { error_ = "cannot open file: " + path; return false; }

    // --- main header (first 32 bytes) ---
    uint8_t hdr[32];
    if (fread(hdr, 1, sizeof(hdr), fp_) != sizeof(hdr)) {
        error_ = "file too small for header";
        fclose(fp_); fp_ = nullptr; return false;
    }
    if (!(hdr[0] == 0x33 && hdr[1] == 0x41 && hdr[2] == 0x4e && hdr[3] == 0x1a)) {
        error_ = "not a Guild Wars .dat (bad magic 3AN\\x1a)";
        fclose(fp_); fp_ = nullptr; return false;
    }
    sector_size_ = rd_i32(hdr + 8);
    mft_offset_  = rd_i64(hdr + 16);

    // --- MFT header (24 bytes) then 15 reserved entries (0x18 each) ---
    if (seek64(fp_, mft_offset_) != 0) { error_ = "seek to MFT failed"; fclose(fp_); fp_ = nullptr; return false; }
    uint8_t mfth[24];
    if (fread(mfth, 1, sizeof(mfth), fp_) != sizeof(mfth)) {
        error_ = "cannot read MFT header"; fclose(fp_); fp_ = nullptr; return false;
    }
    const int32_t entry_count = rd_i32(mfth + 12);

    mft_.reserve(entry_count > 0 ? entry_count : 0);
    for (int x = 0; x < 15; ++x) {
        uint8_t buf[0x18];
        if (fread(buf, 1, sizeof(buf), fp_) != sizeof(buf)) {
            error_ = "cannot read reserved MFT entries"; fclose(fp_); fp_ = nullptr; return false;
        }
        MFTEntry e; parse_entry(buf, e);
        mft_.push_back(e);
    }

    // --- hash / expansion list, stored in the blob referenced by MFT[1] ---
    struct Expansion { int32_t file_number; int32_t file_offset; };
    std::vector<Expansion> mftx;
    if (mft_.size() > 1 && mft_[1].size >= 8) {
        if (seek64(fp_, mft_[1].offset) != 0) { error_ = "seek to hashlist failed"; fclose(fp_); fp_ = nullptr; return false; }
        const int count = mft_[1].size / 8;
        mftx.resize(count);
        for (int x = 0; x < count; ++x) {
            uint8_t buf[8];
            if (fread(buf, 1, 8, fp_) != 8) { error_ = "cannot read hashlist"; fclose(fp_); fp_ = nullptr; return false; }
            mftx[x].file_number = rd_i32(buf + 0);
            mftx[x].file_offset = rd_i32(buf + 4);
        }
        std::sort(mftx.begin(), mftx.end(),
                  [](const Expansion& a, const Expansion& b) { return a.file_offset < b.file_offset; });
    }

    // --- the real file entries: indices 16 .. entry_count-2 ---
    size_t hashcounter = 0;
    while (hashcounter < mftx.size() && mftx[hashcounter].file_offset < 16) ++hashcounter;

    if (seek64(fp_, mft_offset_ + 24 * 16) != 0) { error_ = "seek to MFT body failed"; fclose(fp_); fp_ = nullptr; return false; }
    for (int x = 16; x < entry_count - 1; ++x) {
        uint8_t buf[0x18];
        if (fread(buf, 1, sizeof(buf), fp_) != sizeof(buf)) break; // tolerate a short tail
        MFTEntry e; parse_entry(buf, e);

        if (hashcounter < mftx.size() && x == mftx[hashcounter].file_offset) {
            e.hash = mftx[hashcounter].file_number;
            mft_.push_back(e);
            // multiple hashes can share one file offset
            while (hashcounter + 1 < mftx.size() &&
                   mftx[hashcounter].file_offset == mftx[hashcounter + 1].file_offset) {
                ++hashcounter;
                e.hash = mftx[hashcounter].file_number;
                mft_.push_back(e);
            }
            ++hashcounter;
        } else {
            e.hash = 0;
            mft_.push_back(e);
        }
    }

    // hash -> index map for cross-file reference resolution
    for (int idx = 0; idx < static_cast<int>(mft_.size()); ++idx) {
        if (mft_[idx].hash != 0) hash_index_[mft_[idx].hash] = idx;
    }

    return true;
}

int Dat::index_for_hash(int hash) const {
    auto it = hash_index_.find(hash);
    return it == hash_index_.end() ? -1 : it->second;
}

int Dat::index_for_fileref(int id0, int id1) const {
    return index_for_hash(decode_filename(id0, id1));
}

std::vector<uint8_t> Dat::read_file(size_t n, bool translate) {
    if (n >= mft_.size() || !fp_) return {};
    MFTEntry& m = mft_[n];

    if (!m.b) { m.type = MFTBASE; m.uncompressed_size = 0; return {}; }
    if (m.type != NOTREAD && !translate) return {};
    if (m.size <= 0) { m.type = UNKNOWN; return {}; }

    std::vector<uint8_t> input(static_cast<size_t>(m.size));
    if (seek64(fp_, m.offset) != 0) return {};
    if (fread(input.data(), 1, input.size(), fp_) != input.size()) return {};

    std::vector<uint8_t> output;
    if (m.a && m.size >= 4) {
        unsigned char* out = nullptr;
        int out_size = 0;
        UnpackGWDat(input.data(), m.size, out, out_size);
        if (out) {
            if (out_size > 0) output.assign(out, out + out_size);
            delete[] out;
        }
    } else {
        output = std::move(input);
    }

    if (!output.empty()) {
        MurmurHash3_x86_32(output.data(), static_cast<int>(output.size()), 0, &m.murmurhash3);
        if (m.type == NOTREAD) {
            m.type = detect_type(output.data(), output.size());
            m.uncompressed_size = static_cast<int32_t>(output.size());
        }
    }
    return output;
}

} // namespace datcore
