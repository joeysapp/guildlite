#include "TextureExport.h"

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace Guildlite {
namespace {

    // Write a top-left-origin, 32-bit uncompressed TGA from tightly-packed BGRA.
    bool WriteTGA32(const std::filesystem::path& path, int w, int h, const std::vector<uint8_t>& bgra)
    {
        if (w <= 0 || h <= 0 || bgra.size() < static_cast<size_t>(w) * static_cast<size_t>(h) * 4) {
            return false;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        uint8_t header[18] = {};
        header[2] = 2;                                    // uncompressed true-color
        header[12] = static_cast<uint8_t>(w & 0xFF);
        header[13] = static_cast<uint8_t>((w >> 8) & 0xFF);
        header[14] = static_cast<uint8_t>(h & 0xFF);
        header[15] = static_cast<uint8_t>((h >> 8) & 0xFF);
        header[16] = 32;                                  // bits per pixel
        header[17] = 0x28;                                // 8 alpha bits + top-left origin
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        out.write(reinterpret_cast<const char*>(bgra.data()),
                  static_cast<std::streamsize>(static_cast<size_t>(w) * static_cast<size_t>(h) * 4));
        return out.good();
    }

    inline void Unpack565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        r = static_cast<uint8_t>((((c >> 11) & 0x1F) * 255 + 15) / 31);
        g = static_cast<uint8_t>((((c >> 5) & 0x3F) * 255 + 31) / 63);
        b = static_cast<uint8_t>(((c & 0x1F) * 255 + 15) / 31);
    }

    // Decode a DXT1/DXT3/DXT5 surface (fourcc) to top-down BGRA. `blocksize` is 8
    // for DXT1, 16 for DXT3/DXT5; `has_explicit_alpha`/`has_interp_alpha` pick the mode.
    bool DecodeDXT(D3DFORMAT fmt, const uint8_t* src, int pitch, int w, int h, std::vector<uint8_t>& out)
    {
        const bool dxt1 = (fmt == D3DFMT_DXT1);
        const bool dxt3 = (fmt == D3DFMT_DXT2 || fmt == D3DFMT_DXT3);
        const bool dxt5 = (fmt == D3DFMT_DXT4 || fmt == D3DFMT_DXT5);
        if (!dxt1 && !dxt3 && !dxt5) {
            return false;
        }
        const int blocksize = dxt1 ? 8 : 16;
        out.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
        const int bw = (w + 3) / 4;
        const int bh = (h + 3) / 4;

        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx) {
                const uint8_t* block = src + static_cast<size_t>(by) * pitch + static_cast<size_t>(bx) * blocksize;
                const uint8_t* color_block = block + (dxt1 ? 0 : 8);

                // --- alpha ------------------------------------------------------
                uint8_t alpha[16];
                if (dxt3) {
                    for (int i = 0; i < 16; ++i) {
                        const uint8_t a = (block[i / 2] >> ((i & 1) * 4)) & 0x0F;
                        alpha[i] = static_cast<uint8_t>(a * 17);
                    }
                }
                else if (dxt5) {
                    uint8_t a0 = block[0], a1 = block[1];
                    uint8_t atab[8];
                    atab[0] = a0; atab[1] = a1;
                    if (a0 > a1) {
                        for (int i = 1; i < 7; ++i)
                            atab[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
                    }
                    else {
                        for (int i = 1; i < 5; ++i)
                            atab[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
                        atab[6] = 0; atab[7] = 255;
                    }
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                    for (int i = 0; i < 16; ++i) alpha[i] = atab[(bits >> (3 * i)) & 0x7];
                }
                else {
                    for (int i = 0; i < 16; ++i) alpha[i] = 255;
                }

                // --- color ------------------------------------------------------
                const uint16_t c0 = static_cast<uint16_t>(color_block[0] | (color_block[1] << 8));
                const uint16_t c1 = static_cast<uint16_t>(color_block[2] | (color_block[3] << 8));
                uint8_t r[4], g[4], b[4];
                Unpack565(c0, r[0], g[0], b[0]);
                Unpack565(c1, r[1], g[1], b[1]);
                const bool opaque = dxt1 ? (c0 > c1) : true;
                if (opaque) {
                    r[2] = static_cast<uint8_t>((2 * r[0] + r[1]) / 3);
                    g[2] = static_cast<uint8_t>((2 * g[0] + g[1]) / 3);
                    b[2] = static_cast<uint8_t>((2 * b[0] + b[1]) / 3);
                    r[3] = static_cast<uint8_t>((r[0] + 2 * r[1]) / 3);
                    g[3] = static_cast<uint8_t>((g[0] + 2 * g[1]) / 3);
                    b[3] = static_cast<uint8_t>((b[0] + 2 * b[1]) / 3);
                }
                else {
                    r[2] = static_cast<uint8_t>((r[0] + r[1]) / 2);
                    g[2] = static_cast<uint8_t>((g[0] + g[1]) / 2);
                    b[2] = static_cast<uint8_t>((b[0] + b[1]) / 2);
                    r[3] = g[3] = b[3] = 0; // transparent black in 1-bit-alpha mode
                }

                const uint32_t idx = static_cast<uint32_t>(color_block[4]) | (static_cast<uint32_t>(color_block[5]) << 8) |
                                     (static_cast<uint32_t>(color_block[6]) << 16) | (static_cast<uint32_t>(color_block[7]) << 24);
                for (int py = 0; py < 4; ++py) {
                    for (int px = 0; px < 4; ++px) {
                        const int x = bx * 4 + px;
                        const int y = by * 4 + py;
                        if (x >= w || y >= h) continue;
                        const int pixel = py * 4 + px;
                        const uint32_t sel = (idx >> (2 * pixel)) & 0x3;
                        const size_t o = (static_cast<size_t>(y) * w + x) * 4;
                        out[o + 0] = b[sel];
                        out[o + 1] = g[sel];
                        out[o + 2] = r[sel];
                        out[o + 3] = static_cast<uint8_t>((dxt1 && !opaque && sel == 3) ? 0 : alpha[pixel]);
                    }
                }
            }
        }
        return true;
    }

    // Convert one row of an uncompressed lockable format to BGRA.
    bool ConvertUncompressedRow(D3DFORMAT fmt, const uint8_t* row, int w, uint8_t* dst)
    {
        switch (fmt) {
            case D3DFMT_A8R8G8B8:
                std::memcpy(dst, row, static_cast<size_t>(w) * 4);
                return true;
            case D3DFMT_X8R8G8B8:
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = row[x * 4 + 0]; dst[x * 4 + 1] = row[x * 4 + 1];
                    dst[x * 4 + 2] = row[x * 4 + 2]; dst[x * 4 + 3] = 255;
                }
                return true;
            case D3DFMT_A8B8G8R8:
            case D3DFMT_X8B8G8R8:
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = row[x * 4 + 2]; dst[x * 4 + 1] = row[x * 4 + 1];
                    dst[x * 4 + 2] = row[x * 4 + 0];
                    dst[x * 4 + 3] = (fmt == D3DFMT_A8B8G8R8) ? row[x * 4 + 3] : 255;
                }
                return true;
            case D3DFMT_R8G8B8:
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = row[x * 3 + 0]; dst[x * 4 + 1] = row[x * 3 + 1];
                    dst[x * 4 + 2] = row[x * 3 + 2]; dst[x * 4 + 3] = 255;
                }
                return true;
            case D3DFMT_R5G6B5:
                for (int x = 0; x < w; ++x) {
                    const uint16_t c = reinterpret_cast<const uint16_t*>(row)[x];
                    uint8_t r, g, b; Unpack565(c, r, g, b);
                    dst[x * 4 + 0] = b; dst[x * 4 + 1] = g; dst[x * 4 + 2] = r; dst[x * 4 + 3] = 255;
                }
                return true;
            case D3DFMT_A1R5G5B5:
            case D3DFMT_X1R5G5B5:
                for (int x = 0; x < w; ++x) {
                    const uint16_t c = reinterpret_cast<const uint16_t*>(row)[x];
                    dst[x * 4 + 2] = static_cast<uint8_t>((((c >> 10) & 0x1F) * 255) / 31);
                    dst[x * 4 + 1] = static_cast<uint8_t>((((c >> 5) & 0x1F) * 255) / 31);
                    dst[x * 4 + 0] = static_cast<uint8_t>(((c & 0x1F) * 255) / 31);
                    dst[x * 4 + 3] = static_cast<uint8_t>((fmt == D3DFMT_A1R5G5B5 && (c & 0x8000)) ? 255 : (fmt == D3DFMT_X1R5G5B5 ? 255 : 0));
                }
                return true;
            case D3DFMT_A4R4G4B4:
                for (int x = 0; x < w; ++x) {
                    const uint16_t c = reinterpret_cast<const uint16_t*>(row)[x];
                    dst[x * 4 + 2] = static_cast<uint8_t>(((c >> 8) & 0xF) * 17);
                    dst[x * 4 + 1] = static_cast<uint8_t>(((c >> 4) & 0xF) * 17);
                    dst[x * 4 + 0] = static_cast<uint8_t>((c & 0xF) * 17);
                    dst[x * 4 + 3] = static_cast<uint8_t>(((c >> 12) & 0xF) * 17);
                }
                return true;
            default:
                return false;
        }
    }

    bool IsUncompressedSupported(D3DFORMAT f)
    {
        switch (f) {
            case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8:
            case D3DFMT_X8B8G8R8: case D3DFMT_R8G8B8: case D3DFMT_R5G6B5:
            case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: case D3DFMT_A4R4G4B4:
                return true;
            default:
                return false;
        }
    }

    bool IsDXT(D3DFORMAT f)
    {
        return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 ||
               f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
    }

    // Last resort: let the GPU convert to A8R8G8B8, then copy to system memory.
    bool SaveViaReadback(IDirect3DDevice9* device, IDirect3DTexture9* texture,
                         int w, int h, const std::filesystem::path& path)
    {
        IDirect3DSurface9* src = nullptr;
        if (FAILED(texture->GetSurfaceLevel(0, &src)) || !src) {
            return false;
        }
        bool ok = false;
        IDirect3DSurface9* rt = nullptr;
        IDirect3DSurface9* sys = nullptr;
        if (SUCCEEDED(device->CreateRenderTarget(static_cast<UINT>(w), static_cast<UINT>(h), D3DFMT_A8R8G8B8,
                D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) && rt &&
            SUCCEEDED(device->CreateOffscreenPlainSurface(static_cast<UINT>(w), static_cast<UINT>(h),
                D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)) && sys) {
            if (SUCCEEDED(device->StretchRect(src, nullptr, rt, nullptr, D3DTEXF_NONE)) &&
                SUCCEEDED(device->GetRenderTargetData(rt, sys))) {
                D3DLOCKED_RECT lr = {};
                if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                    std::vector<uint8_t> bgra(static_cast<size_t>(w) * h * 4);
                    const uint8_t* base = static_cast<const uint8_t*>(lr.pBits);
                    for (int y = 0; y < h; ++y) {
                        std::memcpy(&bgra[static_cast<size_t>(y) * w * 4], base + static_cast<size_t>(y) * lr.Pitch,
                                    static_cast<size_t>(w) * 4);
                    }
                    sys->UnlockRect();
                    ok = WriteTGA32(path, w, h, bgra);
                }
            }
        }
        if (sys) sys->Release();
        if (rt) rt->Release();
        src->Release();
        return ok;
    }

} // namespace

namespace TextureExport {

    bool SaveTGA(IDirect3DDevice9* device, IDirect3DTexture9* texture, const std::filesystem::path& path)
    {
        if (!device || !texture) {
            return false;
        }
        D3DSURFACE_DESC desc = {};
        if (FAILED(texture->GetLevelDesc(0, &desc))) {
            return false;
        }
        const int w = static_cast<int>(desc.Width);
        const int h = static_cast<int>(desc.Height);
        if (w <= 0 || h <= 0) {
            return false;
        }

        if (IsUncompressedSupported(desc.Format) || IsDXT(desc.Format)) {
            D3DLOCKED_RECT lr = {};
            if (SUCCEEDED(texture->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) {
                std::vector<uint8_t> bgra;
                bool converted = false;
                if (IsDXT(desc.Format)) {
                    converted = DecodeDXT(desc.Format, static_cast<const uint8_t*>(lr.pBits), lr.Pitch, w, h, bgra);
                }
                else {
                    bgra.assign(static_cast<size_t>(w) * h * 4, 0);
                    converted = true;
                    const uint8_t* base = static_cast<const uint8_t*>(lr.pBits);
                    for (int y = 0; y < h && converted; ++y) {
                        converted = ConvertUncompressedRow(desc.Format, base + static_cast<size_t>(y) * lr.Pitch, w,
                                                           &bgra[static_cast<size_t>(y) * w * 4]);
                    }
                }
                texture->UnlockRect(0);
                if (converted && WriteTGA32(path, w, h, bgra)) {
                    return true;
                }
            }
        }
        // Unlockable (default pool) or an exotic format: bounce through the GPU.
        return SaveViaReadback(device, texture, w, h, path);
    }

} // namespace TextureExport
} // namespace Guildlite
