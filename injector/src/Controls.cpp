#include "Controls.h"

#include <imgui.h>

// One place to edit as the tool grows: each control is a {label, description} row in a static
// table. Draw() renders them grouped; "Planned" rows are dimmed. The two honest caveats live at
// the bottom so every build carries them alongside the controls they qualify.
namespace {
    bool g_visible = false;

    struct Entry { const char* ctrl; const char* desc; };

    const Entry kFreecamKeys[] = {
        {"W / S",   "fly forward / back"},
        {"Q / E",   "strafe left / right"},
        {"Z / X",   "move down / up"},
        {"A / D",   "orbit left / right  (hold RMB = strafe)"},
        {"R",       "autorun (latched forward)"},
        {"Esc",     "stop autorun"},
    };
    const Entry kFreecamVerbs[] = {
        {"freecam on|off|toggle", "detach / reattach the camera"},
        {"cam speed <n>",         "fly speed, world units/sec"},
        {"cam dist <n>",          "max camera distance"},
        {"fov <n>",               "camera field-of-view  (see note 1)"},
        {"fog on|off",            "toggle world fog"},
        {"Save / Go (Viewpoints)","store / recall a framing (4 slots)"},
    };
    const Entry kExporter[] = {
        {"capture",            "export the current scene -> OBJ + MTL + textures"},
        {"capture-dry",        "diagnostics only, writes no file"},
        {"profile clean-*",    "capture recipe: clean-full / clean-self / clean-solo / drop_effects"},
        {"pick on|next|mark|target", "interactive select-a-draw (full verb set in the Exporter window)"},
        {"set <key> <value>",  "set any exporter config field"},
    };
    const Entry kSystem[] = {
        {"F9",         "screenshot the backbuffer -> Documents\\guildlite"},
        {"Insert",     "toggle the ImGui demo window"},
        {"End",        "unload the overlay (direct-inject build only)"},
        {"reload",     "hot-swap a freshly-built core (control file / macOS console)"},
        {"screenshot", "backbuffer -> PNG"},
        {"unload",     "stop + unload"},
        {"controls",   "toggle this window"},
    };
    const Entry kPlanned[] = {
        {"camera roll",             "roll the free camera"},
        {"orbit target",            "orbit a selected agent / NPC"},
        {"dolly / path record",     "keyframed flythrough capture + playback"},
        {"screenshot (freecam)",    "one-button shot from the freecam panel"},
        {"render-side FOV",         "a FOV that actually zooms the view (note 1)"},
        {"/chest",                  "Xunlai storage anywhere  (GWCA OpenXunlaiWindow)"},
        {"NPC dialogs",             "clickable outpost NPC list -> dialog windows"},
        {"multi-Gw.exe targeting",  "drive / reload independent clients"},
    };

    void Rows(const char* title, const Entry* e, int n, bool dim)
    {
        ImGui::SeparatorText(title);
        for (int i = 0; i < n; ++i) {
            if (dim) ImGui::TextDisabled("%s", e[i].ctrl);
            else     ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.f, 1.f), "%s", e[i].ctrl);
            ImGui::SameLine(210.f);
            if (dim) ImGui::TextDisabled("%s", e[i].desc);
            else     ImGui::TextWrapped("%s", e[i].desc);
        }
    }
}

void Controls::Draw()
{
    if (!g_visible) return;

    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Guildlite - Controls", &g_visible)) {
        ImGui::TextDisabled("Keys work in-game; verbs work from the panel, the control file, or the macOS console.");

        if (ImGui::CollapsingHeader("Available now", ImGuiTreeNodeFlags_DefaultOpen)) {
            Rows("Free camera - keys",  kFreecamKeys, IM_ARRAYSIZE(kFreecamKeys), false);
            Rows("Free camera - verbs", kFreecamVerbs, IM_ARRAYSIZE(kFreecamVerbs), false);
            Rows("Model exporter",      kExporter,    IM_ARRAYSIZE(kExporter),    false);
            Rows("System / dev loop",   kSystem,      IM_ARRAYSIZE(kSystem),      false);
        }
        if (ImGui::CollapsingHeader("Planned", ImGuiTreeNodeFlags_DefaultOpen)) {
            Rows("Coming", kPlanned, IM_ARRAYSIZE(kPlanned), true);
        }
        if (ImGui::CollapsingHeader("Honest notes", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("1. Free-camera FOV sets the camera struct value (the slider reflects it), "
                               "but GW renders FOV from GW::Render -- so 'fov' does not visibly zoom yet. "
                               "A render-side FOV is on the Planned list.");
            ImGui::Spacing();
            ImGui::TextWrapped("2. WASD flying can't be exercised over SSH (no key injection). Camera "
                               "unlock and the value verbs are verified live; actual motion is felt in-game.");
        }
    }
    ImGui::End();
}

bool& Controls::WindowVisible() { return g_visible; }
