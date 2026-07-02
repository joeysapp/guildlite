#pragma once

struct IDirect3DDevice9;

// The model-exporter control panel + capture orchestration: the standalone port of
// GuildlitePlugin's Draw / DrawControlPanel / BeginCapture / FlushCapture, with the
// ToolboxPlugin base peeled off (INJECTOR.md seams 2 & 5). Owns the Config (persisted
// via Settings) and the one-frame capture state machine. Draw() is called each frame
// from the overlay, inside its ImGui frame; the capture hook itself lives in Capture.
namespace Exporter {
    void Init();                          // load persisted settings
    void Draw(IDirect3DDevice9* device);  // per-frame: install-if-needed, advance capture, draw window
    void Shutdown();                      // save settings + remove the capture hook
    void Command(const char* verb);       // SSH/stub-driven: "capture" | "capture-dry"
}
