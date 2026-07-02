#pragma once

#include <filesystem>

struct IDirect3DDevice9;
struct IDirect3DTexture9;

// ==============================================================================
// TextureExport -- read a bound D3D9 texture back to a 32-bit TGA on disk, with
// no external image dependency. Three strategies, tried in order of reliability
// for GW's (managed, usually DXT-compressed) textures:
//   1. uncompressed + lockable -> lock level 0, convert to BGRA
//   2. DXT1/DXT3/DXT5          -> lock level 0, decode blocks in software
//   3. anything else / unlockable -> StretchRect into an A8R8G8B8 render target,
//      GetRenderTargetData into system memory, then read that back
// TGA (uncompressed BGRA) is chosen for zero-dependency writes; every DCC tool
// reads it. PNG would need a compressor and buys nothing for this pipeline.
// ==============================================================================

namespace Guildlite {
    namespace TextureExport {
        bool SaveTGA(IDirect3DDevice9* device, IDirect3DTexture9* texture,
                     const std::filesystem::path& path);
    }
}
