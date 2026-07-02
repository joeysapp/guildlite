#pragma once
#include <windows.h>

// The in-game overlay: hooks the live D3D9 device (EndScene/Reset) via MinHook and draws an
// ImGui window each frame. Owns its own ImGui context (unlike the GWToolbox plugin, which is
// handed one). Phase-0 spike: hello window + backbuffer screenshot + best-effort self-unload.
namespace Overlay {
    void Install(HMODULE self);   // runs on the DllMain worker thread
    void RequestUnload();         // tear down hooks + ImGui, then FreeLibraryAndExitThread
}
