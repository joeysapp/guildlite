// guildlite-core.dll -- the Phase-2 reloadable core (INJECTOR.md Phase 2, "the dev loop").
//
// Same overlay + exporter engine as the monolith, but with NO self-bootstrapping DllMain
// thread and NO GWCA ownership: the thin stub injects once, owns GWCA, and calls these C
// exports to (re)load and drive us. Building a fresh copy of this DLL and writing 'reload'
// to the control file swaps the running overlay without ever re-injecting.
#include <windows.h>

#include "Game.h"
#include "Log.h"
#include "Overlay.h"

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

extern "C" {

// Called by the stub right after LoadLibrary, with this module's own handle. The stub has
// already brought GWCA up, so we only flag it ready and install the D3D9 overlay + exporter.
__declspec(dllexport) void GuildliteCoreStart(HMODULE self)
{
    GL_DLLLOG("core: GuildliteCoreStart");
    Game::MarkReadyHosted();
    IDirect3DDevice9* dev = Game::GetDevice();   // gwca.dll is already up (the stub owns it)
    if (!dev) { GL_DLLLOG("core: no D3D device -- no overlay"); return; }
    Overlay::Install(self, dev, /*selfUnload=*/false);
}

// Synchronous: returns once the hooks + ImGui are torn down, so the stub can safely
// FreeLibrary() us and load the freshly-built copy.
__declspec(dllexport) void GuildliteCoreStop()
{
    GL_DLLLOG("core: GuildliteCoreStop");
    Overlay::Teardown();
}

// Route a control-file verb (capture / capture-dry / screenshot / demo) to the engine.
__declspec(dllexport) void GuildliteCoreCommand(const char* verb)
{
    Overlay::Command(verb);
}

} // extern "C"
