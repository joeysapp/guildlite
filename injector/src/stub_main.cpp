// guildlite-stub.dll -- the Phase-2 hot-reload stub (INJECTOR.md Phase 2, "the dev loop").
//
// Injected ONCE into Gw.exe (in the interactive session). It owns the process's single
// gwca.dll instance, then loads guildlite-core.dll and watches a control file. Over SSH you
// write verbs to <Documents>\guildlite\control:
//
//     reload        -> stop the running core, FreeLibrary it, load the freshly-built copy
//     unload        -> stop the core, terminate GWCA, unload the stub itself
//     capture       -> forward to the core (arm a model export)
//     capture-dry   -> forward to the core (diagnostics, writes no file)
//     screenshot    -> forward to the core (backbuffer -> PNG)
//     demo          -> forward to the core (toggle the ImGui demo)
//     <anything>    -> forwarded verbatim to the core (so new verbs need no stub rebuild)
//
// The core is loaded from a private COPY so the next build can overwrite the canonical
// guildlite-core.dll while a core is live -- the classic hot-reload trick. This gives
// hot-reload + full remote control over SSH without ever re-injecting.
#include <windows.h>

#include <filesystem>
#include <string>

#include "Game.h"
#include "Log.h"

namespace {

    using StartFn   = void(*)(HMODULE);
    using StopFn    = void(*)();
    using CommandFn = void(*)(const char*);

    HMODULE   g_core       = nullptr;
    StartFn   g_start      = nullptr;
    StopFn    g_stop       = nullptr;
    CommandFn g_command    = nullptr;
    std::filesystem::path g_live_path;   // the loaded private copy (to delete on unload/reload)
    unsigned  g_generation = 0;
    volatile bool g_quit   = false;

    std::filesystem::path Dir()          { return gl::LogDir(); }                      // <Documents>\guildlite
    std::filesystem::path CanonicalCore() { return Dir() / L"guildlite-core.dll"; }
    std::filesystem::path ControlFile()  { return Dir() / L"control"; }
    std::filesystem::path LiveDir()      { return Dir() / L"core-live"; }

    void DeleteLive()
    {
        if (g_live_path.empty()) return;
        std::error_code ec;
        for (int i = 0; i < 10 && !std::filesystem::remove(g_live_path, ec); ++i) {
            Sleep(50);   // the module may still be unmapping right after FreeLibrary
        }
        g_live_path.clear();
    }

    void StopCore()
    {
        if (!g_core) return;
        if (g_stop) {
            g_stop();   // synchronous: returns once the core's hooks + ImGui are down
        }
        FreeLibrary(g_core);
        g_core = nullptr;
        g_start = nullptr;
        g_stop = nullptr;
        g_command = nullptr;
        DeleteLive();
        GL_DLLLOG("stub: core stopped + unloaded");
    }

    // Copy the canonical core to a private, uniquely-named file and load THAT, leaving the
    // canonical path free for the next build to overwrite.
    bool LoadCore()
    {
        const auto canonical = CanonicalCore();
        std::error_code ec;
        if (!std::filesystem::exists(canonical, ec)) {
            GL_DLLLOG("stub: %ls not found -- build the core, then write 'reload'", canonical.c_str());
            return false;
        }
        std::filesystem::create_directories(LiveDir(), ec);
        const auto live = LiveDir() / (L"guildlite-core-" + std::to_wstring(++g_generation) + L".dll");
        if (!CopyFileW(canonical.c_str(), live.c_str(), FALSE)) {
            GL_DLLLOG("stub: CopyFile to live copy failed (%lu)", GetLastError());
            return false;
        }

        const HMODULE h = LoadLibraryW(live.c_str());
        if (!h) {
            GL_DLLLOG("stub: LoadLibrary(core copy) failed (%lu)", GetLastError());
            std::filesystem::remove(live, ec);
            return false;
        }
        const auto start   = reinterpret_cast<StartFn>(GetProcAddress(h, "GuildliteCoreStart"));
        const auto stop    = reinterpret_cast<StopFn>(GetProcAddress(h, "GuildliteCoreStop"));
        const auto command = reinterpret_cast<CommandFn>(GetProcAddress(h, "GuildliteCoreCommand"));
        if (!start || !stop || !command) {
            GL_DLLLOG("stub: core is missing an export (start=%p stop=%p command=%p)",
                      reinterpret_cast<void*>(start), reinterpret_cast<void*>(stop), reinterpret_cast<void*>(command));
            FreeLibrary(h);
            std::filesystem::remove(live, ec);
            return false;
        }

        g_core = h;
        g_start = start;
        g_stop = stop;
        g_command = command;
        g_live_path = live;
        g_start(h);
        GL_DLLLOG("stub: core loaded (gen %u, %ls)", g_generation, live.filename().c_str());
        return true;
    }

    void ReloadCore()
    {
        GL_DLLLOG("stub: reload requested");
        StopCore();
        LoadCore();
    }

    std::string Trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
        return s.substr(a, b - a);
    }

    void HandleVerb(const std::string& verb)
    {
        if (verb.empty()) return;
        GL_DLLLOG("stub: verb '%s'", verb.c_str());
        if (verb == "reload") {
            ReloadCore();
        }
        else if (verb == "unload") {
            g_quit = true;
        }
        else if (g_command) {
            g_command(verb.c_str());   // capture / capture-dry / screenshot / demo / future verbs
        }
        else {
            GL_DLLLOG("stub: '%s' dropped -- no core loaded (write 'reload' first)", verb.c_str());
        }
    }

    // Read the whole control file, delete it, then run each line. Read-then-delete keeps it
    // cheap; SSH should write atomically (control.tmp -> rename control) to avoid a torn read.
    void PollControl()
    {
        const auto path = ControlFile();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;

        std::string body;
        {
            const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return;
            char buf[1024];
            DWORD got = 0;
            while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got > 0) {
                body.append(buf, got);
            }
            CloseHandle(f);
        }
        std::filesystem::remove(path, ec);

        size_t start = 0;
        while (start <= body.size()) {
            const size_t nl = body.find('\n', start);
            const std::string line = Trim(body.substr(start, (nl == std::string::npos ? body.size() : nl) - start));
            HandleVerb(line);
            if (g_quit) return;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }

    DWORD WINAPI StubMain(LPVOID module)
    {
        GL_DLLLOG("stub: worker start (pid=%lu)", GetCurrentProcessId());
        Game::Initialize();   // the stub owns GWCA for the whole reload lifetime
        LoadCore();           // best-effort: if the core isn't built yet, a later 'reload' loads it

        std::error_code ec;
        std::filesystem::remove(ControlFile(), ec);   // clear any stale command from a previous run
        while (!g_quit) {
            PollControl();
            Sleep(500);       // ~2 Hz: cheap, and fast enough for an interactive SSH loop
        }

        StopCore();
        Game::Terminate();
        GL_DLLLOG("stub: worker exit -- unloading stub");
        FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
        return 0;   // unreached
    }

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        GL_DLLLOG("stub: DllMain attach");
        DisableThreadLibraryCalls(hinst);
        if (const HANDLE t = CreateThread(nullptr, 0, StubMain, hinst, 0, nullptr)) CloseHandle(t);
        else GL_DLLLOG("stub: CreateThread FAILED (%lu)", GetLastError());
    }
    return TRUE;
}
