#pragma once
#include "Transport.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

namespace gl {

// The Guildlite control console: an ImGui panel that produces control-file verbs and ships
// them to an injected client through a Transport. Deliberately portable C++/ImGui with no
// platform code, so the same panel compiles unchanged for an eventual iOS build -- only the
// Transport (and the windowing host) differ there.
class ControlConsole {
public:
    ControlConsole();
    ~ControlConsole();   // drains any in-flight async ssh op so it can't touch us post-destruction

    // Call once per frame between ImGui::NewFrame() and ImGui::Render().
    void Draw();

    // The Metal host reads these to show the built-in ImGui dev windows (the "sandbox").
    bool showDemo    = false;
    bool showMetrics = false;
    bool showStyle   = false;

private:
    void Emit(const std::string& verb);          // one verb (+ optional chained 'capture')
    void Send(const std::string& payload);        // dispatch through the current transport
    void FetchLog();
    void Log(const std::string& line);
    Target CurrentTarget() const;

    // --- editable connection fields (char buffers so we need no imgui_stdlib) ---
    char host_[128];
    char controlPath_[256];
    char logCommand_[512];
    int  timeout_ = 6;
    int  mode_    = 0;            // 0 = mock, 1 = ssh

    bool chainCapture_ = true;    // mirror the `reboot\ncapture` habit from the one-liners
    char verb_[256] = {0};        // free-text verb entry
    bool autoScroll_ = true;

    std::mutex logMx_;
    std::deque<std::string> logLines_;
    std::atomic<bool> busy_{false};   // an ssh op is in flight (async)
    std::atomic<unsigned> pending_{0};
};

} // namespace gl
