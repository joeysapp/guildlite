#include "Overlay.h"
#include "Screenshot.h"
#include "Exporter.h"
#include "Game.h"
#include "Log.h"

#include <d3d9.h>
#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <filesystem>
#include <string>

// Declared by the Win32 backend (imgui_impl_win32.h) but not in its public prototypes on
// every version -- forward-declare so the WndProc subclass can forward input to ImGui.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
    using EndScene_t = HRESULT(APIENTRY*)(IDirect3DDevice9*);
    using Reset_t    = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

    EndScene_t oEndScene = nullptr;
    Reset_t    oReset    = nullptr;

    HMODULE  g_self          = nullptr;
    HWND     g_window        = nullptr;
    WNDPROC  g_origWndProc   = nullptr;
    bool     g_imguiReady    = false;
    bool     g_showDemo      = false;
    bool     g_selfUnload    = true;   // monolith frees itself; the hosted core lets the stub do it
    volatile bool g_unload   = false;
    volatile bool g_tornDown = false;
    HANDLE   g_unloadEvent   = nullptr;
    unsigned g_frame         = 0;

    std::filesystem::path OutputDir()
    {
        wchar_t home[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        auto dir = std::filesystem::path(home) / L"Documents" / L"guildlite";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    HWND GameWindow(IDirect3DDevice9* dev)
    {
        D3DDEVICE_CREATION_PARAMETERS cp{};
        if (SUCCEEDED(dev->GetCreationParameters(&cp)) && cp.hFocusWindow) return cp.hFocusWindow;
        IDirect3DSwapChain9* sc = nullptr;
        if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc) {
            D3DPRESENT_PARAMETERS pp{};
            sc->GetPresentParameters(&pp);
            sc->Release();
            if (pp.hDeviceWindow) return pp.hDeviceWindow;
        }
        return FindWindowW(L"ArenaNet_Dx_Window_Class", nullptr);
    }

    LRESULT CALLBACK HookWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (g_imguiReady) {
            ImGuiIO& io = ImGui::GetIO();
            switch (msg) {
            // Mouse BUTTONS + WHEEL: only involve ImGui when the cursor is over an overlay
            // window; otherwise route straight to the game WITHOUT touching ImGui. ImGui's
            // handler calls SetCapture() on button-down, and that stolen capture was breaking
            // GW's own mouse-capture interactions -- right-drag camera look and ArenaNet UI
            // header clicks. Swallowing when the overlay wants the mouse also stops the game
            // from double-processing a scroll (the "pane scrolls AND camera zooms" bug).
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
                if (io.WantCaptureMouse) {
                    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp);
                    return TRUE;   // consumed by the overlay -- do not forward to the game
                }
                break;             // over the game world -- fall through untouched (no SetCapture)
            // Keys: let ImGui observe, but swallow from the game only while a text field wants them.
            case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
                ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp);
                if (io.WantCaptureKeyboard) return TRUE;
                break;
            // Everything else (WM_MOUSEMOVE for hover, WM_SETCURSOR, ...): observe + always forward.
            default:
                ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp);
                break;
            }
        }
        return CallWindowProcW(g_origWndProc, hWnd, msg, wp, lp);
    }

    void EnsureImGui(IDirect3DDevice9* dev)
    {
        if (g_imguiReady) return;
        g_window = GameWindow(dev);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;   // don't scatter imgui.ini into the game folder
        ImGui_ImplWin32_Init(g_window);
        ImGui_ImplDX9_Init(dev);
        if (g_window)
            g_origWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc)));
        g_imguiReady = true;
        GL_DLLLOG("EnsureImGui: overlay live (game hwnd=%p)", static_cast<void*>(g_window));
    }

    bool KeyPressed(int vk)   // edge-detected: true only on the frame the key goes down
    {
        static bool down[256] = {};
        const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool pressed = now && !down[vk & 0xFF];
        down[vk & 0xFF] = now;
        return pressed;
    }

    void DrawUI()
    {
        ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Guildlite - overlay")) {
            ImGui::Text("In-game overlay is LIVE (own injector, own D3D9 hook).");
            ImGui::Text("frame %u   %.1f FPS", g_frame, ImGui::GetIO().Framerate);
            ImGui::Text("GWCA: %s", Game::Ready() ? "ready" : "initialising...");
            ImGui::Separator();
            ImGui::TextWrapped("The model exporter is the 'Guildlite - Model Exporter' window.");
            ImGui::TextWrapped("F9 / button = screenshot the backbuffer to Documents\\guildlite\\guildlite-shot.png");
            ImGui::TextWrapped("Over SSH: drop 'Documents\\guildlite\\shot.request' (screenshot) or write verbs\n"
                               "to 'Documents\\guildlite\\control' (capture / capture-dry / screenshot / demo).");
            if (g_selfUnload) ImGui::TextWrapped("INSERT = ImGui demo.   END = unload the overlay.");
            else ImGui::TextWrapped("INSERT = ImGui demo.   (hosted core: reload/unload via the stub's control file.)");
            ImGui::Separator();
            if (ImGui::Button("Screenshot now")) Screenshot::Request();
            ImGui::SameLine();
            if (ImGui::Button("Toggle demo")) g_showDemo = !g_showDemo;
            if (g_selfUnload) {
                ImGui::SameLine();
                if (ImGui::Button("Unload")) Overlay::RequestUnload();
            }
        }
        ImGui::End();
        if (g_showDemo) ImGui::ShowDemoWindow(&g_showDemo);
    }

    HRESULT APIENTRY hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
    {
        if (g_imguiReady) ImGui_ImplDX9_InvalidateDeviceObjects();
        const HRESULT hr = oReset(dev, pp);
        if (g_imguiReady) ImGui_ImplDX9_CreateDeviceObjects();
        return hr;
    }

    HRESULT APIENTRY hkEndScene(IDirect3DDevice9* dev)
    {
        static bool s_firstCall = true;
        if (s_firstCall) { s_firstCall = false; GL_DLLLOG("hkEndScene: FIRST call, device=%p", static_cast<void*>(dev)); }
        if (!g_unload) {
            EnsureImGui(dev);

            if (KeyPressed(VK_F9))                    Screenshot::Request();
            if (KeyPressed(VK_INSERT))                g_showDemo = !g_showDemo;
            if (g_selfUnload && KeyPressed(VK_END))   Overlay::RequestUnload();

            // SSH-drivable trigger: a 'shot.request' file (checked ~2x/sec so it's cheap).
            if ((g_frame % 30) == 0) {
                const auto req = OutputDir() / L"shot.request";
                std::error_code ec;
                if (std::filesystem::exists(req, ec)) {
                    Screenshot::Request();
                    std::filesystem::remove(req, ec);
                }
            }

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            DrawUI();             // overlay status strip
            Exporter::Draw(dev);  // model-exporter window + capture state machine (installs the DIP hook)
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

            // Grab AFTER drawing so the shot includes our overlay.
            if (Screenshot::Consume())
                Screenshot::CaptureBackbuffer(dev, OutputDir() / L"guildlite-shot.png");

            ++g_frame;
        }
        return oEndScene(dev);
    }

    // The actual teardown, shared by the monolith's watchdog and the hosted Teardown().
    // Idempotent: guarded by g_tornDown so a double signal can't double-free. Must run
    // OFF the render thread, after hkEndScene has stopped drawing (g_unload set + drained).
    void DoTeardown()
    {
        if (g_tornDown) return;
        g_tornDown = true;
        Exporter::Shutdown();          // save settings + remove the DIP capture hook + release texture refs
        MH_DisableHook(MH_ALL_HOOKS);  // EndScene/Reset (and DIP if Capture::Remove missed it)
        MH_Uninitialize();
        if (g_imguiReady) {
            if (g_window && g_origWndProc)
                SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        Game::Terminate();             // no-op unless THIS module owns GWCA (the monolith)
        GL_DLLLOG("DoTeardown: complete");
    }

    DWORD WINAPI Watchdog(LPVOID)
    {
        // Monolith only. Wait for the unload signal, drain, tear down, then free ourselves.
        WaitForSingleObject(g_unloadEvent, INFINITE);
        Sleep(200);   // let any in-flight hkEndScene call drain
        DoTeardown();
        CloseHandle(g_unloadEvent);
        g_unloadEvent = nullptr;
        FreeLibraryAndExitThread(g_self, 0);
        return 0;   // unreached
    }
}

void Overlay::RequestUnload()
{
    g_unload = true;
    if (g_selfUnload && g_unloadEvent) SetEvent(g_unloadEvent);
    // Hosted core: rendering stops here; the stub calls Teardown() then FreeLibrary()s us.
}

void Overlay::Teardown()
{
    // Synchronous teardown for a host (the Phase-2 stub). Runs on the caller's thread,
    // which is NOT the render thread, so we can drain and tear down in place.
    g_unload = true;
    Sleep(200);   // let any in-flight hkEndScene call drain
    DoTeardown();
}

void Overlay::Command(const char* verb)
{
    if (!verb) return;
    const std::string v = verb;
    if (v == "screenshot" || v == "shot")      Screenshot::Request();
    else if (v == "capture")                    Exporter::Command("capture");
    else if (v == "capture-dry" || v == "diag") Exporter::Command("capture-dry");
    else if (v == "demo")                       g_showDemo = !g_showDemo;
    else if (v == "unload") {
        if (g_selfUnload) RequestUnload();
        else GL_DLLLOG("Overlay::Command: 'unload' ignored in hosted mode (stub owns lifecycle)");
    }
    // Everything else (set/target/profile/...) is forwarded to the exporter, which
    // tokenises and handles it. Keeps new control verbs a one-file change in Exporter.
    else Exporter::Command(verb);
}

void Overlay::Install(HMODULE self, IDirect3DDevice9* device, bool selfUnload)
{
    GL_DLLLOG("Install: begin (selfUnload=%d device=%p)", static_cast<int>(selfUnload), static_cast<void*>(device));
    g_self = self;
    g_selfUnload = selfUnload;
    if (!device) { GL_DLLLOG("Install: null device -- no overlay"); return; }
    // Read the game's own IDirect3DDevice9 vtable and hook it directly. MinHook patches the
    // target functions' prologues, so hooking these slots hooks every caller (the game).
    void** vtbl = *reinterpret_cast<void***>(device);
    void* pReset    = vtbl[16];   // IDirect3DDevice9::Reset
    void* pEndScene = vtbl[42];   // IDirect3DDevice9::EndScene
    GL_DLLLOG("Install: vtable EndScene=%p Reset=%p", pEndScene, pReset);
    const MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) { GL_DLLLOG("Install: MH_Initialize FAILED (%d)", mi); return; }
    const MH_STATUS c1 = MH_CreateHook(pEndScene, &hkEndScene, reinterpret_cast<void**>(&oEndScene));
    const MH_STATUS c2 = MH_CreateHook(pReset,    &hkReset,    reinterpret_cast<void**>(&oReset));
    const MH_STATUS en = MH_EnableHook(MH_ALL_HOOKS);
    GL_DLLLOG("Install: hooks createEndScene=%d createReset=%d enable=%d (0=OK)", c1, c2, en);

    Exporter::Init();   // load persisted settings (engine self-init; GWCA is brought up by the entry)

    if (selfUnload) {
        g_unloadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (const HANDLE t = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr)) CloseHandle(t);
    }
    GL_DLLLOG("Install: done (waiting for EndScene)");
}
