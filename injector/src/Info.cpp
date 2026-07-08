#include "Info.h"

#include "Game.h"
#include "Overlay.h"
#include "Screenshot.h"

#include <imgui.h>

// One place to edit as the tool grows: each control is a {label, description} row in a static
// table. Draw() renders the merged Info panel -- Status (was the standalone overlay window),
// Commands, then the grouped controls reference. "Planned" rows are dimmed; the two honest
// caveats live at the bottom so every build carries them alongside the controls they qualify.
namespace {
    bool g_visible = false;

    struct Entry { const char* ctrl; const char* desc; };

    const Entry kEditor[] = {
        {"edit apply / revert",     "push / undo the current edit on the Source (self or target)"},
        {"edit transmog <npc_id>",  "whole-model transmog into an NPC (0 clears)"},
        {"edit scale <percent>",    "rescale the Source (100 = normal)"},
        {"edit slot <n> dye a b c d", "recolour equipment slot n (dye values 0..13)"},
        {"edit save/load <name>",   "store / recall a named character look"},
        {"edit states",             "apply all enabled global states"},
        {"edit composites",         "dump the model_file_id->DAT file-ids table -> Documents\\guildlite\\composites.tsv"},
    };
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
        {"select self|none|nearest|<id>", "set the in-game target headlessly (auto-refreshes status.json)"},
        {"status | confirm",   "write status.json -- player/target/pick/last-capture for SSH readback"},
        {"set <key> <value>",  "set any exporter config field"},
    };
    const Entry kSystem[] = {
        {"F9",         "screenshot the backbuffer -> Documents\\guildlite"},
        {"Insert",     "toggle the ImGui demo window"},
        {"End",        "unload the overlay (direct-inject build only)"},
        {"reload",     "hot-swap a freshly-built core (control file / macOS console)"},
        {"screenshot", "backbuffer -> PNG"},
        {"unload",     "stop + unload"},
        {"info",       "toggle this window"},
    };
    const Entry kPlanned[] = {
        {"edit NPC names",          "decode the encoded NPC names in the transmog picker"},
        {"edit auto-reapply",       "re-push a look after zoning (server resets appearance on map change)"},
        {"edit weapons/costumes",   "weapon redraw + costume/festival-hat tables (signature-scanned)"},
        {"camera roll / orbit",     "roll the free camera; orbit a selected agent"},
        {"dolly / path record",     "keyframed flythrough capture + playback"},
        {"/chest",                  "Xunlai storage anywhere  (GWCA OpenXunlaiWindow)"},
        {"multi-Gw.exe targeting",  "drive / reload independent clients"},
    };

    void Rows(const char* title, const Entry* e, int n, bool dim)
    {
        ImGui::SeparatorText(title);
        for (int i = 0; i < n; ++i) {
            if (dim) ImGui::TextDisabled("%s", e[i].ctrl);
            else     ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.f, 1.f), "%s", e[i].ctrl);
            ImGui::SameLine(230.f);
            if (dim) ImGui::TextDisabled("%s", e[i].desc);
            else     ImGui::TextWrapped("%s", e[i].desc);
        }
    }

    // The former standalone "Guildlite - overlay" status window, now the top of Info.
    void DrawStatus()
    {
        ImGui::Text("In-game overlay is LIVE (own injector, own D3D9 hook).  %.1f FPS", ImGui::GetIO().Framerate);
        ImGui::Text("GWCA: %s", Game::Ready() ? "ready" : "initialising...");
        ImGui::Spacing();
        if (ImGui::Button("Screenshot now")) Screenshot::Request();
        ImGui::SameLine();
        if (ImGui::Button("Toggle demo")) Overlay::Command("demo");
        ImGui::SameLine();
        if (ImGui::Button("Unload overlay")) Overlay::Command("unload");
        ImGui::TextDisabled("F9 = screenshot . Insert = demo . End = unload (direct-inject build).\n"
                            "Over SSH: write verbs to Documents\\guildlite\\control (reload / capture / edit ... / screenshot).");
    }
}

void Info::Draw()
{
    if (!g_visible) return;

    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Guildlite - Info", &g_visible)) {
        if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawStatus();
        }
        ImGui::TextDisabled("Keys work in-game; verbs work from the panel, the control file, or the macOS console.");

        if (ImGui::CollapsingHeader("Available now", ImGuiTreeNodeFlags_DefaultOpen)) {
            Rows("Model editor",        kEditor,      IM_ARRAYSIZE(kEditor),      false);
            Rows("Model exporter",      kExporter,    IM_ARRAYSIZE(kExporter),    false);
            Rows("Free camera - keys",  kFreecamKeys, IM_ARRAYSIZE(kFreecamKeys), false);
            Rows("Free camera - verbs", kFreecamVerbs, IM_ARRAYSIZE(kFreecamVerbs), false);
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
            ImGui::TextWrapped("2. Editor edits are client-side and reset on zone; WASD flying and live "
                               "appearance changes can't be exercised over SSH (no key injection / no view). "
                               "The `status` verb closes the loop the other way: it writes status.json "
                               "(player/target/pick/last-capture) so an SSH/agent loop can CONFIRM the result "
                               "of target/select/pick/capture without a view -- scp it back and parse it.");
        }
    }
    ImGui::End();
}

bool& Info::WindowVisible() { return g_visible; }
