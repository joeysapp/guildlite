#pragma once

#include <filesystem>

#include "GuildliteConfig.h"

// Config persistence glue -- the standalone replacement for ToolboxPlugin's
// LoadSettings/SaveSettings (INJECTOR.md seam 5). We own our settings dir
// (<Documents>\guildlite) and store the persisted subset of Config as JSON via
// glaze, which the exporter already links for its manifest.
namespace Settings {
    std::filesystem::path Dir();               // <Documents>\guildlite (created if missing)
    std::filesystem::path File();              // <dir>\settings.json
    void Load(Guildlite::Config& cfg);         // overlay persisted values onto cfg; missing keys keep defaults
    void Save(const Guildlite::Config& cfg);
}
