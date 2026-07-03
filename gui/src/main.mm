// Guildlite - Control (macOS, SDL2 + Metal)
//
// A native ImGui app that runs WITHOUT a Guild Wars client: it drives injected Windows
// clients by producing control-file verbs and shipping them over SSH (see Transport), and
// doubles as an ImGui/Metal sandbox for UI development on the go. The window/render layer is
// SDL2 + Metal precisely because that stack also runs on iOS -- the eventual iPhone build
// reuses this same C++ console, swapping only the Transport (iOS can't spawn ssh).
//
//   Guildlite --selftest   headless: create a hidden window, render a few frames, exit 0.
//                          Lets the build pipeline verify the whole path without a display.

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_metal.h"

#include "ControlConsole.h"

#include <SDL.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    bool selftest = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;

    // --- Dear ImGui context ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // don't scatter imgui.ini into the CWD (matches the injector overlay)
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // docking is handy; viewports left off (simpler)
    ImGui::StyleColorsDark();

    // --- SDL + Metal ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    Uint32 winFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (selftest) winFlags |= SDL_WINDOW_HIDDEN;   // don't steal focus during a headless verify
    SDL_Window* window = SDL_CreateWindow(
        "Guildlite - Control (Metal)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 720, 780, winFlags);
    if (!window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 2; }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 3; }

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_RenderGetMetalLayer(renderer);
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    ImGui_ImplMetal_Init(layer.device);
    ImGui_ImplSDL2_InitForMetal(window);

    id<MTLCommandQueue> commandQueue = [layer.device newCommandQueue];
    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor new];

    gl::ControlConsole console;

    bool done = false;
    int  rendered = 0;    // frames actually drawn (selftest success needs >= 1)
    int  guard    = 0;    // selftest watchdog if drawables never arrive
    while (!done) {
        @autoreleasepool {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT) done = true;
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                    event.window.windowID == SDL_GetWindowID(window))
                    done = true;
            }

            int w = 0, h = 0;
            SDL_GetRendererOutputSize(renderer, &w, &h);
            layer.drawableSize = CGSizeMake(w, h);
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable) {                                  // window minimised / 0-sized
                if (selftest && ++guard > 120) done = true;
                SDL_Delay(5);
                continue;
            }

            id<MTLCommandBuffer> cmd = [commandQueue commandBuffer];
            rpd.colorAttachments[0].clearColor  = MTLClearColorMake(0.08, 0.09, 0.11, 1.0);
            rpd.colorAttachments[0].texture     = drawable.texture;
            rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

            ImGui_ImplMetal_NewFrame(rpd);
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            console.Draw();
            if (console.showDemo)    ImGui::ShowDemoWindow(&console.showDemo);
            if (console.showMetrics) ImGui::ShowMetricsWindow(&console.showMetrics);
            if (console.showStyle)   { ImGui::Begin("Style", &console.showStyle);
                                       ImGui::ShowStyleEditor(); ImGui::End(); }

            ImGui::Render();
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);
            [enc endEncoding];
            [cmd presentDrawable:drawable];
            [cmd commit];

            ++rendered;
            if (selftest && rendered >= 3) done = true;
        }
    }

    ImGui_ImplMetal_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (selftest) {
        if (rendered >= 1) { std::printf("GUILDLITE-GUI SELFTEST OK (%d frames)\n", rendered); return 0; }
        std::fprintf(stderr, "GUILDLITE-GUI SELFTEST FAIL (no frames rendered)\n");
        return 5;
    }
    return 0;
}
