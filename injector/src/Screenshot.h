#pragma once
#include <filesystem>

struct IDirect3DDevice9;

// Backbuffer -> PNG, straight out of the D3D9 present path -- no OS screenshot, so it works
// over an SSH/session-0 shell where GDI/desktop capture can't reach the game.
namespace Screenshot {
    void Request();                                                            // arm (hotkey / file / button)
    bool Consume();                                                            // true exactly once per Request
    bool CaptureBackbuffer(IDirect3DDevice9* dev, const std::filesystem::path& out);
}
