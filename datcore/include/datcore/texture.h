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

// Decode a decompressed ATEX/ATTX blob to RGBA. Returns false if it is not a
// decodable ATEX texture. On a decode that required a stubbed asm stage, returns
// true with needed_asm=true and (partial) pixels — callers should check ok().
bool decode_texture(const uint8_t* data, size_t size, Texture& out);

// Write RGBA to a PNG (via vendored stb_image_write). Returns false on error.
bool write_png(const Texture& t, const std::string& path);

} // namespace datcore
