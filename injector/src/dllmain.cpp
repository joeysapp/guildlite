// guildlite.dll -- the Phase-1 monolith payload. DllMain must stay tiny (loader lock): it only
// spins up a worker thread that brings up GWCA and installs the D3D9 hook + overlay + exporter.
// All real work happens off-loader-lock.
#include <windows.h>

#include "Game.h"
#include "Log.h"
#include "Overlay.h"

static DWORD WINAPI Bootstrap(LPVOID module)
{
    GL_DLLLOG("Bootstrap: thread start");
    if (!Game::Initialize()) {                       // this DLL owns GWCA (retries internally)
        GL_DLLLOG("Bootstrap: GWCA init failed -- no overlay");
        return 0;
    }
    IDirect3DDevice9* dev = Game::GetDevice();        // the game's real device (not a throwaway)
    if (!dev) { GL_DLLLOG("Bootstrap: no D3D device -- no overlay"); return 0; }
    Overlay::Install(static_cast<HMODULE>(module), dev); // selfUnload=true: the monolith frees itself
    GL_DLLLOG("Bootstrap: Install returned");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        GL_DLLLOG("DllMain: attach (pid=%lu)", GetCurrentProcessId());
        DisableThreadLibraryCalls(hinst);
        if (const HANDLE t = CreateThread(nullptr, 0, Bootstrap, hinst, 0, nullptr)) CloseHandle(t);
        else GL_DLLLOG("DllMain: CreateThread FAILED (%lu)", GetLastError());
    }
    return TRUE;
}
