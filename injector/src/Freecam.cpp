#include "Freecam.h"
#include "Game.h"
#include "Log.h"

#include <GWCA/GameEntities/Camera.h>
#include <GWCA/Managers/CameraMgr.h>

#include <imgui.h>

#include <Windows.h>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// GW keeps the camera a fixed distance behind look_at_target along yaw/pitch; you fly by
// translating that target and rotating yaw, then UpdateCameraPos() recomputes the eye. This is
// the CameraUnlockModule model verbatim -- proven on this client -- with GW::Chat / ToolboxModule
// / SettingsRegistry stripped out. Input is polled with GetAsyncKeyState so it works whether or
// not an ImGui window holds focus (we only stand down while a text field is being typed into).
namespace {
    constexpr float kDefaultSpeed = 1000.f;                 // world units / sec
    constexpr float kRotSpeed     = 3.14159265358979f / 3.f; // ~6 s per full turn

    bool  g_visible     = true;
    float g_speed       = kDefaultSpeed;
    float g_maxDist     = 900.f;
    float g_fov         = 0.f;    // lazy-seeded from GW the first frame a camera exists
    bool  g_fovInit     = false;
    bool  g_keepAlt     = false;  // false = fly along pitch (up into the sky); true = hold altitude
    bool  g_autorun     = false;  // R latches forward; S / Esc release it
    bool  g_fog         = true;

    // Client-side saved framings -- the first "grows with us" feature riding on the panel. Store
    // the target/orientation (not the eye) so a recall reproduces the shot via UpdateCameraPos.
    struct Viewpoint { bool set = false; GW::Vec3f look{}; float yaw = 0.f, pitch = 0.f, dist = 0.f; };
    Viewpoint g_views[4];

    bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    void ForwardMove(GW::Camera* cam, float amount, bool along_pitch)
    {
        if (amount == 0.f) return;
        if (along_pitch) {
            const float px = sqrtf(1.f - cam->pitch * cam->pitch);
            cam->look_at_target.x += amount * px * cosf(cam->yaw);
            cam->look_at_target.y += amount * px * sinf(cam->yaw);
            cam->look_at_target.z += amount * cam->pitch;
        } else {
            cam->look_at_target.x += amount * cosf(cam->yaw);
            cam->look_at_target.y += amount * sinf(cam->yaw);
        }
    }

    void SideMove(GW::Camera* cam, float amount)
    {
        if (amount == 0.f) return;
        cam->look_at_target.x += amount * -sinf(cam->yaw);
        cam->look_at_target.y += amount *  cosf(cam->yaw);
    }

    void RotateYaw(GW::Camera* cam, float angle)
    {
        if (angle == 0.f) return;
        const float px = cam->look_at_target.x - cam->position.x;
        const float py = cam->look_at_target.y - cam->position.y;
        GW::Vec3f np;
        np.x = cam->position.x + (cosf(angle) * px - sinf(angle) * py);
        np.y = cam->position.y + (sinf(angle) * px + cosf(angle) * py);
        np.z = cam->look_at_target.z;
        cam->SetYaw(cam->yaw + angle);
        cam->look_at_target = np;
    }

    void FlyStep(GW::Camera* cam, float delta)
    {
        float forward = 0.f, side = 0.f, vertical = 0.f, rotate = 0.f;
        if (KeyDown('W')) forward  += 1.f;
        if (KeyDown('S')) { forward -= 1.f; g_autorun = false; }
        if (KeyDown('Q')) side     += 1.f;
        if (KeyDown('E')) side     -= 1.f;
        if (KeyDown('Z')) vertical -= 1.f;
        if (KeyDown('X')) vertical += 1.f;
        if (KeyDown('A')) rotate   += 1.f;
        if (KeyDown('D')) rotate   -= 1.f;
        if (KeyDown('R')) g_autorun = true;
        if (KeyDown(VK_ESCAPE)) g_autorun = false;
        if (g_autorun) forward += 1.f;

        // Hold RMB to strafe with A/D instead of orbiting -- matches the reference feel.
        if (KeyDown(VK_RBUTTON) && rotate != 0.f) { side = rotate; rotate = 0.f; }

        ForwardMove(cam, forward * delta * g_speed, !g_keepAlt);
        SideMove(cam, side * delta * g_speed);
        cam->look_at_target.z += vertical * delta * g_speed;
        RotateYaw(cam, rotate * delta * kRotSpeed);
        GW::CameraMgr::UpdateCameraPos();
    }

    void RecallView(GW::Camera* cam, const Viewpoint& v)
    {
        if (!v.set) return;
        GW::CameraMgr::UnlockCam(true);   // recall only lands while detached; GW would fight a locked cam
        cam->look_at_target = v.look;
        cam->SetYaw(v.yaw);
        cam->pitch = v.pitch;
        if (v.dist > 0.f) cam->distance = v.dist;
        GW::CameraMgr::UpdateCameraPos();
    }

    // A section that reads as a first-class group and leaves obvious room for more controls.
    void DrawMovement()
    {
        ImGui::SeparatorText("Movement");
        ImGui::SliderFloat("Speed", &g_speed, 50.f, 6000.f, "%.0f u/s");
        ImGui::Checkbox("Hold altitude on W/S", &g_keepAlt);
        ImGui::SameLine();
        ImGui::TextDisabled(g_autorun ? "(autorun on)" : "");
        ImGui::TextWrapped("W/S forward  .  Q/E strafe  .  Z/X down/up  .  A/D orbit "
                           "(hold RMB = strafe)  .  R autorun  .  Esc stop");
    }

    void DrawLens()
    {
        ImGui::SeparatorText("Lens");
        if (ImGui::SliderFloat("FOV", &g_fov, 0.20f, 2.50f, "%.2f rad"))
            GW::CameraMgr::SetFieldOfView(g_fov);
        if (ImGui::SliderFloat("Max distance", &g_maxDist, 25.f, 5000.f, "%.0f"))
            GW::CameraMgr::SetMaxDist(g_maxDist);
        if (ImGui::Checkbox("Fog", &g_fog))
            GW::CameraMgr::SetFog(g_fog);
    }

    void DrawViewpoints(GW::Camera* cam)
    {
        ImGui::SeparatorText("Viewpoints");
        for (int i = 0; i < IM_ARRAYSIZE(g_views); ++i) {
            ImGui::PushID(i);
            if (ImGui::Button("Save")) {
                g_views[i] = { true, cam->look_at_target, cam->yaw, cam->pitch, cam->distance };
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!g_views[i].set);
            if (ImGui::Button("Go")) RecallView(cam, g_views[i]);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("Slot %d %s", i + 1, g_views[i].set ? "" : "(empty)");
            ImGui::PopID();
        }
    }
}

void Freecam::Init()
{
    // Settings live in-memory for now (speed / fov / distance / viewpoints). Persisting them
    // through Settings/GuildliteConfig is a follow-up once the control set settles.
}

void Freecam::Draw(IDirect3DDevice9*)
{
    const bool ready = Game::Ready();
    GW::Camera* cam = ready ? GW::CameraMgr::GetCamera() : nullptr;

    // Fly whenever freecam is engaged, panel open or not.
    if (cam) {
        if (!g_fovInit) { g_fov = GW::CameraMgr::GetFieldOfView(); g_fovInit = true; }
        const ImGuiIO& io = ImGui::GetIO();
        if (GW::CameraMgr::GetCameraUnlock() && !io.WantTextInput)
            FlyStep(cam, io.DeltaTime);
    }

    if (!g_visible) return;

    ImGui::SetNextWindowSize(ImVec2(340, 380), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(420, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Guildlite - Free Camera", &g_visible)) {
        if (!ready) {
            ImGui::TextUnformatted("Waiting for GWCA...");
        } else if (!cam) {
            ImGui::TextUnformatted("No camera -- load an outpost or explorable.");
        } else {
            bool unlocked = GW::CameraMgr::GetCameraUnlock();
            if (ImGui::Checkbox("Free camera", &unlocked))
                GW::CameraMgr::UnlockCam(unlocked);
            ImGui::SameLine();
            ImGui::TextColored(unlocked ? ImVec4(0.4f, 1.f, 0.4f, 1.f) : ImVec4(0.7f, 0.7f, 0.7f, 1.f),
                               unlocked ? "flying" : "locked");

            DrawMovement();
            DrawLens();
            DrawViewpoints(cam);

            ImGui::SeparatorText("Camera");
            ImGui::Text("eye  %.0f  %.0f  %.0f", cam->position.x, cam->position.y, cam->position.z);
            ImGui::Text("look %.0f  %.0f  %.0f", cam->look_at_target.x, cam->look_at_target.y, cam->look_at_target.z);
            ImGui::Text("yaw %.2f   pitch %.2f", cam->yaw, cam->pitch);
        }
    }
    ImGui::End();
}

void Freecam::Shutdown()
{
    // Never strand the player in a detached camera across an unload / hot-reload.
    if (Game::Ready() && GW::CameraMgr::GetCameraUnlock())
        GW::CameraMgr::UnlockCam(false);
}

void Freecam::Command(const char* verb)
{
    if (!verb) return;
    std::vector<std::string> tok;
    for (std::string cur; const char c : std::string(verb) + ' ') {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (tok.empty()) return;

    const bool ready = Game::Ready();
    const std::string& head = tok[0];
    auto arg = [&](size_t i) { return i < tok.size() ? tok[i] : std::string(); };
    auto engage = [&](bool on) { if (ready) GW::CameraMgr::UnlockCam(on); if (on) g_visible = true; };

    if (head == "fov") {
        if (tok.size() > 1 && ready) { g_fov = strtof(arg(1).c_str(), nullptr); GW::CameraMgr::SetFieldOfView(g_fov); }
        return;
    }
    if (head == "fog") { if (ready) { g_fog = arg(1) != "off"; GW::CameraMgr::SetFog(g_fog); } return; }

    // head is freecam / cam / camera
    const std::string sub = arg(1);
    if (sub.empty() || sub == "toggle")      engage(!(ready && GW::CameraMgr::GetCameraUnlock()));
    else if (sub == "on"  || sub == "unlock") engage(true);
    else if (sub == "off" || sub == "lock")   engage(false);
    else if (sub == "speed") { if (tok.size() > 2) g_speed = strtof(arg(2).c_str(), nullptr); }
    else if (sub == "fov")   { if (tok.size() > 2 && ready) { g_fov = strtof(arg(2).c_str(), nullptr); GW::CameraMgr::SetFieldOfView(g_fov); } }
    else if (sub == "dist" || sub == "distance") {
        if (tok.size() > 2) { g_maxDist = strtof(arg(2).c_str(), nullptr); if (ready) GW::CameraMgr::SetMaxDist(g_maxDist); }
    }
    else GL_DLLLOG("Freecam::Command: unknown '%s'", verb);
}

bool& Freecam::WindowVisible() { return g_visible; }
