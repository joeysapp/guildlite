#include "Overlay.h"
#include "Screenshot.h"
#include "Log.h"

#include <d3d9.h>
#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <filesystem>

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
    volatile bool g_unload   = false;
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
        if (g_imguiReady) ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp);
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
        ImGui::SetNextWindowSize(ImVec2(360, 210), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Guildlite - standalone spike")) {
            ImGui::Text("In-game overlay is LIVE (own injector, own D3D9 hook).");
            ImGui::Text("frame %u   %.1f FPS", g_frame, ImGui::GetIO().Framerate);
            ImGui::Separator();
            ImGui::TextWrapped("F9 / button = screenshot the backbuffer to Documents\\guildlite\\guildlite-shot.png");
            ImGui::TextWrapped("Over SSH: drop a file 'Documents\\guildlite\\shot.request' and it fires within ~half a second.");
            ImGui::TextWrapped("INSERT = ImGui demo.   END = unload the overlay.");
            ImGui::Separator();
            if (ImGui::Button("Screenshot now")) Screenshot::Request();
            ImGui::SameLine();
            if (ImGui::Button("Toggle demo")) g_showDemo = !g_showDemo;
            ImGui::SameLine();
            if (ImGui::Button("Unload")) Overlay::RequestUnload();
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

            if (KeyPressed(VK_F9))     Screenshot::Request();
            if (KeyPressed(VK_INSERT)) g_showDemo = !g_showDemo;
            if (KeyPressed(VK_END))    Overlay::RequestUnload();

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
            DrawUI();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

            // Grab AFTER drawing so the shot includes our overlay.
            if (Screenshot::Consume())
                Screenshot::CaptureBackbuffer(dev, OutputDir() / L"guildlite-shot.png");

            ++g_frame;
        }
        return oEndScene(dev);
    }

    bool GrabVTable(void** endScene, void** reset)
    {
        // A throwaway device just to read the shared IDirect3DDevice9 vtable; hooking those
        // slots hooks the game's device too (the vtable pointer is per-implementation).
        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) { GL_DLLLOG("GrabVTable: Direct3DCreate9 returned null"); return false; }

        // Register a real, thread-owned top-level window. A bare "STATIC" top-level window made
        // CreateDevice return E_INVALIDARG (0x80070057) -- D3D9 wants a proper device window.
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GuildliteDummyWnd";
        RegisterClassExW(&wc);
        const HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                          0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = hwnd;

        IDirect3DDevice9* dev = nullptr;
        HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
        if (FAILED(hr)) {
            GL_DLLLOG("GrabVTable: HAL hr=0x%08lX hwnd=%p, retrying REF", hr, static_cast<void*>(hwnd));
            hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, hwnd,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
        }
        GL_DLLLOG("GrabVTable: CreateDevice hr=0x%08lX dev=%p hwnd=%p", hr, static_cast<void*>(dev), static_cast<void*>(hwnd));

        bool ok = false;
        if (SUCCEEDED(hr) && dev) {
            void** vtbl = *reinterpret_cast<void***>(dev);
            *reset    = vtbl[16];   // IDirect3DDevice9::Reset
            *endScene = vtbl[42];   // IDirect3DDevice9::EndScene
            ok = true;
            dev->Release();
        }
        if (hwnd) DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        d3d->Release();
        return ok;
    }

    DWORD WINAPI Watchdog(LPVOID)
    {
        // Teardown must run OFF the game's render thread (we can't exit that thread). Wait for
        // the unload signal; hkEndScene already stops rendering once g_unload is set.
        WaitForSingleObject(g_unloadEvent, INFINITE);
        Sleep(200);   // let any in-flight hkEndScene call drain
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (g_imguiReady) {
            if (g_window && g_origWndProc)
                SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        CloseHandle(g_unloadEvent);
        FreeLibraryAndExitThread(g_self, 0);
        return 0;   // unreached
    }
}

void Overlay::RequestUnload()
{
    g_unload = true;
    if (g_unloadEvent) SetEvent(g_unloadEvent);
}

void Overlay::Install(HMODULE self)
{
    GL_DLLLOG("Install: begin");
    g_self = self;
    void* pEndScene = nullptr;
    void* pReset = nullptr;
    if (!GrabVTable(&pEndScene, &pReset)) { GL_DLLLOG("Install: GrabVTable FAILED -- no overlay"); return; }
    GL_DLLLOG("Install: vtable EndScene=%p Reset=%p", pEndScene, pReset);
    const MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK) { GL_DLLLOG("Install: MH_Initialize FAILED (%d)", mi); return; }
    const MH_STATUS c1 = MH_CreateHook(pEndScene, &hkEndScene, reinterpret_cast<void**>(&oEndScene));
    const MH_STATUS c2 = MH_CreateHook(pReset,    &hkReset,    reinterpret_cast<void**>(&oReset));
    const MH_STATUS en = MH_EnableHook(MH_ALL_HOOKS);
    GL_DLLLOG("Install: hooks createEndScene=%d createReset=%d enable=%d (0=OK)", c1, c2, en);

    g_unloadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (const HANDLE t = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr)) CloseHandle(t);
    GL_DLLLOG("Install: done (waiting for EndScene)");
}
