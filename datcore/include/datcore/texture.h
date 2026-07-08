#pragma once
// Portable ATEX/ATTX texture decode -> RGBA + PNG export.
#include <cstdint>
#include <string>
#include <vector>

namespace datcore {

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;   // width*height*4, byte order R,G,B,A
    bool needed_asm = false;     // decode hit a stubbed asm stage (pixels partial)
    bool ok() const { return width > 0 && height > 0 && !needed_asm; }
};

// Read just the ATEX/ATTX dimensions + format char ('1'/'3'/'5'/'N'/'A'/'L')
// from the header — no decode, so it's fast enough to index every texture.
// Returns false if the blob isn't an ATEX/ATTX texture.
bool texture_info(const uint8_t* data, size_t size, int& width, int& height, char& fmt);

// Decode a decompressed ATEX/ATTX blob to RGBA. Returns false if it is not a
// decodable ATEX texture. On a decode that required a stubbed asm stage, returns
// true with needed_asm=true and (partial) pixels — callers should check ok().
bool decode_texture(const uint8_t* data, size_t size, Texture& out);

// Desaturate to grayscale in place — a stand-in "gray dye" for dyeable armor, so an
// item's detailed texture is stored as a neutral gray (real dye colors are runtime).
void tint_gray(Texture& t);

// Write RGBA to a PNG (via vendored stb_image_write). Returns false on error.
bool write_png(const Texture& t, const std::string& path);

} // namespace datcore
