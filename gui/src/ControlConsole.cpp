#include "ControlConsole.h"

#include "imgui.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace gl {

ControlConsole::ControlConsole()
{
    const Target def;   // seed the editable fields from the injector defaults
    std::snprintf(host_,        sizeof(host_),        "%s", def.host.c_str());
    std::snprintf(controlPath_, sizeof(controlPath_), "%s", def.controlPath.c_str());
    std::snprintf(logCommand_,  sizeof(logCommand_),  "%s", def.logCommand.c_str());
    timeout_ = def.connectTimeout;
    Log("ready. mode=mock -- flip to ssh to drive a live client.");
}

ControlConsole::~ControlConsole()
{
    // A detached ssh worker holds `this`; wait for it to finish before our members die.
    // Bounded so a wedged connection (up to ConnectTimeout) can't hang quit forever.
    for (int i = 0; i < 1000 && pending_.load() > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

Target ControlConsole::CurrentTarget() const
{
    Target t;
    t.host           = host_;
    t.controlPath    = controlPath_;
    t.logCommand     = logCommand_;
    t.connectTimeout = timeout_;
    return t;
}

void ControlConsole::Log(const std::string& line)
{
    std::lock_guard<std::mutex> lk(logMx_);
    logLines_.push_back(line);
    while (logLines_.size() > 400) logLines_.pop_front();
}

// Build the payload for a single verb, honouring the "also send capture" habit, then dispatch.
void ControlConsole::Emit(const std::string& verb)
{
    std::string payload = verb + "\n";
    if (chainCapture_ && verb != "capture" && verb != "capture-dry")
        payload += "capture\n";
    Send(payload);
}

void ControlConsole::Send(const std::string& payload)
{
    if (mode_ == 0) {                       // mock: instant, no thread
        Log(MakeMockTransport()->send(payload).detail);
        return;
    }
    // ssh: run on a worker so a dead/unreachable host can't stall the render loop.
    if (busy_.exchange(true)) { Log("busy: a transport op is already in flight"); return; }
    ++pending_;
    Target t = CurrentTarget();
    std::thread([this, payload, t]() {
        auto r = MakeSshTransport(t)->send(payload);
        Log(r.detail);
        --pending_;
        busy_ = false;
    }).detach();
}

void ControlConsole::FetchLog()
{
    if (mode_ == 0) { Log(MakeMockTransport()->fetchLog().detail); return; }
    if (busy_.exchange(true)) { Log("busy: a transport op is already in flight"); return; }
    ++pending_;
    Target t = CurrentTarget();
    std::thread([this, t]() {
        auto r = MakeSshTransport(t)->fetchLog();
        Log("--- remote log ---");
        Log(r.detail);
        --pending_;
        busy_ = false;
    }).detach();
}

void ControlConsole::Draw()
{
    ImGui::SetNextWindowSize(ImVec2(680, 740), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Guildlite - Control")) { ImGui::End(); return; }

    const bool busy = busy_.load();

    // --- Connection --------------------------------------------------------------
    if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::RadioButton("mock (offline / UI dev)", &mode_, 0); ImGui::SameLine();
        ImGui::RadioButton("ssh (drive a live client)", &mode_, 1);
        ImGui::InputText("host", host_, sizeof(host_));
        ImGui::InputText("control path", controlPath_, sizeof(controlPath_));
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt("ssh timeout (s)", &timeout_);
        if (timeout_ < 1) timeout_ = 1;
        if (busy) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.9f,0.7f,0.3f,1.f), "  working..."); }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Verbs are written to the control file; the stub polls it ~2 Hz (INJECTOR.md).");
    ImGui::Checkbox("also send 'capture' after lifecycle verbs", &chainCapture_);

    // --- Lifecycle ---------------------------------------------------------------
    ImGui::SeparatorText("Lifecycle");
    if (ImGui::Button("reload"))  Emit("reload");   ImGui::SameLine();
    if (ImGui::Button("reboot"))  Emit("reboot");   ImGui::SameLine();
    if (ImGui::Button("off"))     Emit("off");      ImGui::SameLine();
    if (ImGui::Button("on"))      Emit("on");       ImGui::SameLine();
    if (ImGui::Button("unload"))  Emit("unload");
    ImGui::TextDisabled("reload/unload are stub-native; reboot/off/on forward to the core verbatim.");

    // --- Actions -----------------------------------------------------------------
    ImGui::SeparatorText("Actions");
    if (ImGui::Button("capture"))      Emit("capture");      ImGui::SameLine();
    if (ImGui::Button("capture-dry"))  Emit("capture-dry");  ImGui::SameLine();
    if (ImGui::Button("screenshot"))   Emit("screenshot");   ImGui::SameLine();
    if (ImGui::Button("demo (remote)"))Emit("demo");

    // --- Exporter profiles -------------------------------------------------------
    ImGui::SeparatorText("Exporter profiles");
    if (ImGui::Button("clean-self"))         Emit("profile clean-self");         ImGui::SameLine();
    if (ImGui::Button("clean-solo"))         Emit("profile clean-solo");         ImGui::SameLine();
    if (ImGui::Button("clean-solo-target"))  Emit("profile clean-solo-target");  ImGui::SameLine();
    if (ImGui::Button("raw"))                Emit("profile raw");

    // --- Free-text verb ----------------------------------------------------------
    ImGui::SeparatorText("Custom verb");
    const bool entered = ImGui::InputText("##verb", verb_, sizeof(verb_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool sendClicked = ImGui::Button("Send");
    if ((entered || sendClicked) && verb_[0]) { Send(std::string(verb_) + "\n"); verb_[0] = 0; }
    ImGui::TextDisabled("e.g.  set up_axis 2   |   target target   |   set export_normals 1");

    // --- Log ---------------------------------------------------------------------
    ImGui::SeparatorText("Log");
    if (ImGui::Button("Fetch remote log")) FetchLog(); ImGui::SameLine();
    if (ImGui::Button("Clear")) { std::lock_guard<std::mutex> lk(logMx_); logLines_.clear(); }
    ImGui::SameLine(); ImGui::Checkbox("auto-scroll", &autoScroll_);

    ImGui::BeginChild("log", ImVec2(0, 200), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(logMx_);
        for (const auto& line : logLines_) ImGui::TextUnformatted(line.c_str());
    }
    if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // --- Sandbox (pure ImGui dev) ------------------------------------------------
    ImGui::SeparatorText("ImGui sandbox");
    ImGui::Checkbox("Demo", &showDemo);       ImGui::SameLine();
    ImGui::Checkbox("Metrics", &showMetrics); ImGui::SameLine();
    ImGui::Checkbox("Style editor", &showStyle);
    ImGui::SameLine();
    ImGui::TextDisabled("| %.1f FPS", ImGui::GetIO().Framerate);

    ImGui::End();
}

} // namespace gl
