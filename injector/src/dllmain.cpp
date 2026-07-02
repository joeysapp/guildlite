// guildlite.dll -- the payload. DllMain must stay tiny (loader lock): it only spins up a
// worker thread that installs the D3D9 hook + overlay. All real work happens off-loader-lock.
#include <windows.h>

#include "Log.h"
#include "Overlay.h"

static DWORD WINAPI Bootstrap(LPVOID module)
{
    GL_DLLLOG("Bootstrap: thread start");
    Overlay::Install(static_cast<HMODULE>(module));
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
