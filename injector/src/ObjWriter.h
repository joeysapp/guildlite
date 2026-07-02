#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Capture.h"
#include "GuildliteConfig.h"

// ==============================================================================
// ObjWriter -- serialise captured MeshChunks to disk. OBJ carries per-object
// groups + optional UVs/normals + an .mtl referencing the exported textures
// (Advanced); STL is a geometry-only binary solid (Base convenience). Textures
// are exported first by the caller, which fills MeshChunk::texture_file.
// ==============================================================================

namespace Guildlite {
    struct WriteResult {
        bool ok = false;
        std::filesystem::path main_file;
        uint64_t bytes = 0;
        std::string error;
    };

    namespace ObjWriter {
        WriteResult WriteObj(const std::vector<MeshChunk>& chunks, const Config& cfg,
                             const std::filesystem::path& dir, const std::wstring& stem);
        WriteResult WriteStl(const std::vector<MeshChunk>& chunks, const Config& cfg,
                             const std::filesystem::path& dir, const std::wstring& stem);
    }
}
