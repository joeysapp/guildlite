#pragma once
#include <windows.h>

struct IDirect3DDevice9;

// The in-game overlay + engine host: hooks the live D3D9 device (EndScene/Reset) via
// MinHook, drives the ImGui frame, and drives the model exporter (its DrawIndexedPrimitive
// capture hook lives in Capture, installed once the device is known). Owns its own ImGui
// context (unlike the GWToolbox plugin, which was handed one).
//
// Two unload modes so the same engine serves both delivery shapes:
//   - selfUnload=true  (the Phase-1 monolith, direct-injected): a watchdog thread waits
//     for RequestUnload() and ends in FreeLibraryAndExitThread -- the DLL frees itself.
//   - selfUnload=false (the Phase-2 reloadable core): the host (stub) owns the module, so
//     there is no self-free; the stub calls Teardown() then FreeLibrary()s us.
namespace Overlay {
    void Install(HMODULE self, IDirect3DDevice9* device, bool selfUnload = true); // hooks the game's live device
    void RequestUnload();                                // signal teardown (monolith self-frees; hosted stops rendering)
    void Teardown();                                     // synchronous teardown for a host; does NOT FreeLibrary
    void Command(const char* verb);                      // route an SSH/stub command to the engine
}
