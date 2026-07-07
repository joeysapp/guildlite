#include "datcore/texture.h"
#include "datcore/texture_atex.h"
#include "datcore/atex_asm.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstring>

namespace datcore {

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

bool write_png(const Texture& t, const std::string& path) {
    if (t.width <= 0 || t.height <= 0 || t.rgba.size() < static_cast<size_t>(t.width) * t.height * 4)
        return false;
    return stbi_write_png(path.c_str(), t.width, t.height, 4, t.rgba.data(), t.width * 4) != 0;
}

} // namespace datcore
