#include "datcore/texture.h"
#include "datcore/texture_atex.h"
#include "datcore/atex_asm.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstring>

namespace datcore {

bool texture_info(const uint8_t* data, size_t size, int& width, int& height, char& fmt) {
    if (size < 12) return false;
    uint32_t id2;
    std::memcpy(&id2, data + 4, 4);
    if ((id2 & 0xffffff) != 0x545844u) return false; // "DXT" — matches ProcessImageFile
    uint16_t w, h;
    std::memcpy(&w, data + 8, 2);
    std::memcpy(&h, data + 10, 2);
    width = w;
    height = h;
    fmt = static_cast<char>(id2 >> 24);
    return true;
}

bool decode_texture(const uint8_t* data, size_t size, Texture& out) {
    if (size < 12) return false;
    // ProcessImageFile wants a mutable buffer; give it a private copy.
    std::vector<unsigned char> buf(data, data + size);

    atex_reset_asm_flag();
    DatTexture t = ProcessImageFile(buf.data(), static_cast<int>(size));
    out.needed_asm = atex_needed_asm();

    if (t.width <= 0 || t.height <= 0) return false;
    const size_t px = static_cast<size_t>(t.width) * static_cast<size_t>(t.height);
    if (t.rgba_data.size() < px) return false;

    out.width = t.width;
    out.height = t.height;
    out.rgba.resize(px * 4);
    std::memcpy(out.rgba.data(), t.rgba_data.data(), px * 4); // RGBA union is r,g,b,a
    return true;
}

void tint_gray(Texture& t) {
    for (size_t i = 0; i + 3 < t.rgba.size(); i += 4) {
        // Rec.601 luminance; alpha (i+3) is left untouched.
        uint32_t lum = (t.rgba[i] * 77u + t.rgba[i + 1] * 150u + t.rgba[i + 2] * 29u) >> 8;
        uint8_t g = static_cast<uint8_t>(lum > 255 ? 255 : lum);
        t.rgba[i] = t.rgba[i + 1] = t.rgba[i + 2] = g;
    }
}

bool write_png(const Texture& t, const std::string& path) {
    if (t.width <= 0 || t.height <= 0 || t.rgba.size() < static_cast<size_t>(t.width) * t.height * 4)
        return false;
    return stbi_write_png(path.c_str(), t.width, t.height, 4, t.rgba.data(), t.width * 4) != 0;
}

} // namespace datcore
