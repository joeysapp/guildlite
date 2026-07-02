#include "Game.h"
#include "Log.h"

#include <windows.h>

#include <GWCA/GWCA.h>
#include <GWCA/Managers/RenderMgr.h>

#include <atomic>

namespace {
    std::atomic<bool> g_ready{false};
    std::atomic<bool> g_owned{false}; // this module called GW::Initialize -> it must Terminate
}

bool Game::Initialize()
{
    if (g_ready.load()) {
        return true;
    }
    // We inject into an already-running Gw.exe, so GWCA's memory scan normally lands
    // on the first try; retry a few times in case we beat a late zone load.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (GW::Initialize()) {
            GW::EnableHooks();
            g_owned.store(true);
            g_ready.store(true);
            GL_DLLLOG("Game::Initialize: GWCA up (attempt %d)", attempt);
            return true;
        }
        GL_DLLLOG("Game::Initialize: GW::Initialize failed (attempt %d) -- retrying", attempt);
        Sleep(250);
    }
    GL_DLLLOG("Game::Initialize: GWCA FAILED after retries -- exporter game-state reads disabled");
    return false;
}

void Game::Terminate()
{
    if (!g_owned.exchange(false)) {
        g_ready.store(false); // hosted core: leave the stub's GWCA alone
        return;
    }
    GW::DisableHooks();
    GW::Terminate();
    g_ready.store(false);
    GL_DLLLOG("Game::Terminate: GWCA down");
}

void Game::MarkReadyHosted()
{
    g_ready.store(true);
}

bool Game::Ready()
{
    return g_ready.load();
}

IDirect3DDevice9* Game::GetDevice()
{
    // The device exists from game start; GWCA's scan resolves it during Initialize. Retry
    // briefly in case we injected a hair before the pointer is populated.
    for (int i = 0; i < 50; ++i) {
        if (IDirect3DDevice9* d = GW::Render::GetDevice()) {
            GL_DLLLOG("Game::GetDevice: device=%p (try %d)", static_cast<void*>(d), i);
            return d;
        }
        Sleep(100);
    }
    GL_DLLLOG("Game::GetDevice: GW::Render::GetDevice stayed null");
    return nullptr;
}
