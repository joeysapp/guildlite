#include "Editor.h"

#include "AppearanceApply.h"
#include "EditorConfig.h"
#include "GuildliteConfig.h" // TargetSource
#include "Game.h"
#include "Log.h"
#include "Settings.h"        // Settings::Dir() -- share the guildlite settings folder

#include <imgui.h>

#include <glaze/glaze.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Guildlite;

// The Model Editor: composes an Appearance in a live edit buffer, applies it to the Source agent
// via AppearanceApply (which owns every GWCA/game-thread write), and manages saved Character
// looks + toggleable Global States, persisted to Documents\guildlite\editor.json. This file is
// deliberately GWCA-free -- all game reads/writes go through AppearanceApply -- so it stays pure
// UI + state, the same split as GameState(read)/AppearanceApply(write).
namespace {

    const char* kSlotNames[9] = {"Weapon", "Offhand", "Chest", "Legs", "Head",
                                 "Feet", "Hands", "Costume body", "Costume head"};
    const char* kProfNames[11] = {"None", "Warrior", "Ranger", "Monk", "Necromancer", "Mesmer",
                                  "Elementalist", "Assassin", "Ritualist", "Paragon", "Dervish"};
    // Indexed by DyeColor value 0..13 (value 1 is unused in GW's enum).
    const char* kDyeNames[14] = {"None", "(unused)", "Blue", "Green", "Purple", "Red", "Yellow",
                                 "Brown", "Orange", "Silver", "Black", "Gray", "White", "Pink"};
    const char* kStateTargetNames[4] = {"Self", "Target", "All players", "All NPCs"};

    bool         g_visible = true;
    TargetSource g_source = TargetSource::Player;   // the editor's Source (mirrors the Exporter's)
    Appearance   g_edit;                            // the live edit buffer (slots kept at size 9)
    std::vector<CharacterConfig> g_characters;      // saved named looks
    std::vector<GlobalState>     g_states;          // higher-level toggleable states
    std::string  g_status = "Ready.";

    // NPC transmog picker -- snapshot on demand (not per-frame).
    std::vector<AppearanceApply::NpcRow> g_npc_rows;
    size_t g_npc_total = 0;
    int    g_npc_min_id = 0;

    char g_save_name[64] = {};   // name field for saving a character / new state

    // Deferred to the render thread from Command() (the stub poll thread must not read GWCA).
    volatile bool g_read_req = false;
    volatile bool g_npcdump_req = false;   // log a window of the NPC table (SSH transmog A/B)
    volatile int  g_npcdump_min = 0;
    volatile bool g_composite_dump_req = false; // dump composite-model table -> composites.tsv (armor bridge)

    std::filesystem::path EditorFile() { return Settings::Dir() / L"editor.json"; }

    // Keep an appearance's slot vector at exactly 9 (index == slot) so the render thread never
    // races a reallocation and slot writes always land in-bounds.
    void EnsureSlots(Appearance& a)
    {
        if (a.slots.size() != 9) {
            std::vector<SlotEdit> old = a.slots;
            a.slots.assign(9, SlotEdit{});
            for (int i = 0; i < 9; ++i) {
                if (i < static_cast<int>(old.size())) a.slots[i] = old[i];
                a.slots[i].slot = i;
            }
        }
    }

    // Copy an appearance into `dst` WITHOUT reallocating dst.slots (copies the 9 slots in place),
    // so loading a saved look can't invalidate a slot the render thread is drawing.
    void CopyAppearance(Appearance& dst, const Appearance& src)
    {
        dst.use_transmog = src.use_transmog;
        dst.transmog_npc_id = src.transmog_npc_id;
        dst.use_scale = src.use_scale;
        dst.scale_percent = src.scale_percent;
        dst.use_professions = src.use_professions;
        dst.primary = src.primary;
        dst.secondary = src.secondary;
        dst.use_sex = src.use_sex;
        dst.female = src.female;
        dst.use_equipment = src.use_equipment;
        EnsureSlots(dst);
        for (int i = 0; i < 9; ++i) {
            dst.slots[i] = (i < static_cast<int>(src.slots.size())) ? src.slots[i] : SlotEdit{};
            dst.slots[i].slot = i;
        }
    }

    void LoadDoc()
    {
        std::ifstream in(EditorFile(), std::ios::binary);
        if (!in) return;
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string buf = ss.str();
        if (buf.empty()) return;
        EditorSave doc;
        if (glz::read_json(doc, buf)) {
            GL_DLLLOG("Editor: editor.json parse error -- keeping empty library");
            return;
        }
        g_characters = std::move(doc.characters);
        g_states = std::move(doc.states);
        for (auto& c : g_characters) EnsureSlots(c.appearance);
        for (auto& s : g_states) EnsureSlots(s.appearance);
        GL_DLLLOG("Editor: loaded %zu characters, %zu states", g_characters.size(), g_states.size());
    }

    void SaveDoc()
    {
        EditorSave doc;
        doc.characters = g_characters;
        doc.states = g_states;
        const std::string json = glz::write<glz::opts{.prettify = true}>(doc).value_or(std::string{});
        if (json.empty()) {
            GL_DLLLOG("Editor: editor.json serialise failed");
            return;
        }
        std::ofstream out(EditorFile(), std::ios::binary | std::ios::trunc);
        out << json;
    }

    CharacterConfig* FindCharacter(const std::string& name)
    {
        for (auto& c : g_characters) if (c.name == name) return &c;
        return nullptr;
    }

    // Save the live edit buffer as a named character (overwrite if the name exists).
    void SaveCurrentAs(const std::string& name)
    {
        if (name.empty()) { g_status = "Name a character before saving."; return; }
        CharacterConfig cfg;
        cfg.name = name;
        cfg.appearance = g_edit;
        if (CharacterConfig* existing = FindCharacter(name)) *existing = cfg;
        else g_characters.push_back(cfg);
        SaveDoc();
        g_status = "Saved character '" + name + "'.";
    }

    void LoadCharacter(const std::string& name)
    {
        if (CharacterConfig* c = FindCharacter(name)) {
            CopyAppearance(g_edit, c->appearance);
            g_status = "Loaded '" + name + "' into the editor (press Apply to push it).";
        } else {
            g_status = "No saved character named '" + name + "'.";
        }
    }

    void DeleteCharacter(const std::string& name)
    {
        for (size_t i = 0; i < g_characters.size(); ++i) {
            if (g_characters[i].name == name) {
                g_characters.erase(g_characters.begin() + i);
                SaveDoc();
                g_status = "Deleted character '" + name + "'.";
                return;
            }
        }
    }

    // ---------------------------------------------------------------- UI sections

    void DrawSource()
    {
        const char* items[] = {"Player (self)", "Target (selection)"};
        int v = static_cast<int>(g_source);
        if (ImGui::Combo("Source", &v, items, 2)) g_source = static_cast<TargetSource>(v);

        if (ImGui::Button("Read from source")) {
            if (AppearanceApply::ReadSource(g_source, g_edit)) {
                EnsureSlots(g_edit);
                g_status = "Loaded the source's current appearance (toggle which axes to apply).";
            } else {
                g_status = "No source agent to read (choose Player, or target one).";
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("fills the fields below from the live character; nothing is applied yet");

        // Live read-out (cheap reads; refreshed each frame while the panel is open).
        Appearance live;
        if (AppearanceApply::ReadSource(g_source, live)) {
            const int nslots = static_cast<int>(live.slots.size());
            int equipped = 0;
            for (int i = 0; i < nslots; ++i) if (live.slots[i].model_file_id) ++equipped;
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.f, 1.f),
                               "Live: %s/%s  %s  transmog=%s  equipped slots=%d",
                               kProfNames[live.primary % 11], kProfNames[live.secondary % 11],
                               live.female ? "female" : "male",
                               live.transmog_npc_id ? std::to_string(live.transmog_npc_id).c_str() : "none",
                               equipped);
        } else {
            ImGui::TextDisabled("Live: no %s selected.", g_source == TargetSource::Player ? "player" : "target");
        }
    }

    void DrawNpcPicker()
    {
        if (ImGui::Button("Refresh NPC list")) {
            g_npc_total = AppearanceApply::NpcListSnapshot(g_npc_rows, 4096);
            g_status = "NPC list: " + std::to_string(g_npc_total) + " defined (showing up to 4096).";
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt("min id", &g_npc_min_id);
        if (g_npc_min_id < 0) g_npc_min_id = 0;
        ImGui::TextDisabled("The client's NPC table. Click a row to transmog into it. (Names are "
                            "encoded in GW; decoding them is a follow-up -- rows show id + model + prof.)");
        ImGui::BeginChild("npc_list", ImVec2(0, 140), true);
        for (const auto& r : g_npc_rows) {
            if (static_cast<int>(r.id) < g_npc_min_id) continue;
            char label[96];
            _snprintf_s(label, sizeof(label), _TRUNCATE, "#%u  model %u  %s##npc%u",
                        r.id, r.model_file_id, kProfNames[r.primary % 11], r.id);
            if (ImGui::Selectable(label, g_edit.transmog_npc_id == r.id)) {
                g_edit.use_transmog = true;
                g_edit.transmog_npc_id = r.id;
                g_status = "Transmog set to NPC #" + std::to_string(r.id) + " (press Apply).";
            }
        }
        ImGui::EndChild();
    }

    void DrawTransmog()
    {
        ImGui::Checkbox("Transmog into an NPC model", &g_edit.use_transmog);
        ImGui::BeginDisabled(!g_edit.use_transmog);
        ImGui::SetNextItemWidth(160.f);
        ImGui::InputScalar("NPC id (0 = clear)", ImGuiDataType_U32, &g_edit.transmog_npc_id);
        ImGui::TextDisabled("Replaces the WHOLE rendered model with this NPC's (an emulated model "
                            "packet, the GWToolbox /transmo mechanism). 0 clears it. Client-side only.");
        DrawNpcPicker();
        ImGui::EndDisabled();
    }

    void DrawScale()
    {
        ImGui::Checkbox("Rescale the agent", &g_edit.use_scale);
        ImGui::BeginDisabled(!g_edit.use_scale);
        ImGui::SetNextItemWidth(220.f);
        ImGui::SliderInt("Scale %", &g_edit.scale_percent, 1, 255, "%d%%");
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f),
                           "Experimental: the AgentScale packet sets the field but GW only re-applies "
                           "scale on a model (re)load, so scale ALONE may not visibly resize -- it takes "
                           "effect paired with a Transmog. (100 = normal; reverts to 100%%.)");
        ImGui::EndDisabled();
    }

    void DrawIdentity()
    {
        ImGui::Checkbox("Set professions", &g_edit.use_professions);
        ImGui::BeginDisabled(!g_edit.use_professions);
        ImGui::SetNextItemWidth(150.f);
        ImGui::Combo("Primary", &g_edit.primary, kProfNames, 11);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.f);
        ImGui::Combo("Secondary", &g_edit.secondary, kProfNames, 11);
        ImGui::EndDisabled();

        ImGui::Checkbox("Set sex", &g_edit.use_sex);
        ImGui::BeginDisabled(!g_edit.use_sex);
        ImGui::SameLine();
        int sex = g_edit.female ? 1 : 0;
        if (ImGui::RadioButton("Male", sex == 0)) g_edit.female = false;
        ImGui::SameLine();
        if (ImGui::RadioButton("Female", sex == 1)) g_edit.female = true;
        ImGui::EndDisabled();

        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f),
                           "Experimental: professions/sex are direct field writes -- many models do NOT "
                           "re-skin live from them. For a reliable look change use Transmog or Equipment.");
    }

    void DrawDyeCombos(SlotEdit& s)
    {
        int* dyes[4] = {&s.dye0, &s.dye1, &s.dye2, &s.dye3};
        const char* labels[4] = {"Dye 1", "Dye 2", "Dye 3", "Dye 4"};
        for (int d = 0; d < 4; ++d) {
            ImGui::SetNextItemWidth(96.f);
            ImGui::Combo(labels[d], dyes[d], kDyeNames, 14);
            if (d != 3) ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(96.f);
        ImGui::InputInt("Tint", &s.dye_tint);
    }

    void DrawEquipment()
    {
        ImGui::Checkbox("Edit equipment (model + dye)", &g_edit.use_equipment);
        ImGui::TextDisabled("Dye is robust (recolour real gear). Model swap is experimental: it reuses "
                            "the slot's real item type, so equip a piece there first. Weapons don't reliably "
                            "redraw (a known GW quirk).");
        ImGui::BeginDisabled(!g_edit.use_equipment);
        ImGui::BeginChild("equip_list", ImVec2(0, 240), true);
        for (int slot = 0; slot < 9; ++slot) {
            SlotEdit& s = g_edit.slots[slot];
            s.slot = slot;
            ImGui::PushID(slot);
            ImGui::Checkbox("##en", &s.enabled);
            ImGui::SameLine();
            ImGui::TextUnformatted(kSlotNames[slot]);
            if (s.enabled) {
                ImGui::Indent();
                ImGui::Checkbox("Set model", &s.set_model);
                ImGui::SameLine();
                ImGui::BeginDisabled(!s.set_model);
                ImGui::SetNextItemWidth(140.f);
                ImGui::InputScalar("model_file_id", ImGuiDataType_U32, &s.model_file_id);
                ImGui::EndDisabled();
                ImGui::Checkbox("Set dye", &s.set_dye);
                if (s.set_dye) DrawDyeCombos(s);
                ImGui::Unindent();
                ImGui::Separator();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
    }

    void DrawApplyBar()
    {
        const bool ready = Game::Ready();
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("Apply to source", ImVec2(-1, 0)))
            AppearanceApply::ApplyToSource(g_source, g_edit);
        if (ImGui::Button("Revert source")) AppearanceApply::RevertSource(g_source);
        ImGui::SameLine();
        if (ImGui::Button("Revert ALL edits")) AppearanceApply::RevertAll();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("edited agents: %d", AppearanceApply::EditedCount());
        if (!ready) ImGui::TextDisabled("Waiting for GWCA...");
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.3f, 1.f),
                           "All edits are CLIENT-SIDE (only you see them) and reset when you zone -- re-Apply after a map change.");
    }

    void DrawSavedCharacters()
    {
        ImGui::SetNextItemWidth(180.f);
        ImGui::InputTextWithHint("##charname", "character name", g_save_name, sizeof(g_save_name));
        ImGui::SameLine();
        if (ImGui::Button("Save current")) SaveCurrentAs(g_save_name);
        ImGui::TextDisabled("Saves the whole edit buffer above as a named look (Documents\\guildlite\\editor.json).");

        if (g_characters.empty()) {
            ImGui::TextDisabled("No saved characters yet.");
            return;
        }
        ImGui::BeginChild("char_list", ImVec2(0, 120), true);
        for (size_t i = 0; i < g_characters.size(); ++i) {
            const std::string name = g_characters[i].name; // copy: Delete may erase mid-loop
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine(200.f);
            if (ImGui::Button("Load")) LoadCharacter(name);
            ImGui::SameLine();
            if (ImGui::Button("Apply")) {
                CopyAppearance(g_edit, g_characters[i].appearance);
                AppearanceApply::ApplyToSource(g_source, g_edit);
                g_status = "Applied '" + name + "' to " + (g_source == TargetSource::Player ? "player" : "target") + ".";
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete")) { DeleteCharacter(name); ImGui::PopID(); break; }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void DrawGlobalStates()
    {
        ImGui::TextWrapped("A Global State binds a saved look to a target set with a priority. Enable "
                           "several, then Apply -- low priority first so the highest wins a shared field.");
        if (ImGui::Button("New state from current edit")) {
            GlobalState st;
            st.name = g_save_name[0] ? g_save_name : ("state " + std::to_string(g_states.size() + 1));
            st.appearance = g_edit;
            g_states.push_back(st);
            SaveDoc();
            g_status = "Added global state '" + st.name + "'.";
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!Game::Ready());
        if (ImGui::Button("Apply enabled states")) {
            AppearanceApply::ApplyStates(g_states);
            g_status = "Applied all enabled global states.";
        }
        ImGui::EndDisabled();

        if (g_states.empty()) {
            ImGui::TextDisabled("No global states yet.");
            return;
        }
        ImGui::BeginChild("state_list", ImVec2(0, 160), true);
        for (size_t i = 0; i < g_states.size(); ++i) {
            GlobalState& st = g_states[i];
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Checkbox("##en", &st.enabled)) SaveDoc();
            ImGui::SameLine();
            ImGui::TextUnformatted(st.name.c_str());
            ImGui::SameLine(180.f);
            ImGui::SetNextItemWidth(80.f);
            if (ImGui::InputInt("prio", &st.priority)) SaveDoc();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.f);
            if (ImGui::Combo("##tgt", &st.target, kStateTargetNames, 4)) SaveDoc();
            ImGui::SameLine();
            if (ImGui::Button("Edit")) {
                CopyAppearance(g_edit, st.appearance);
                g_status = "Loaded state '" + st.name + "' into the editor.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Del")) {
                g_states.erase(g_states.begin() + i);
                SaveDoc();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void DrawPanel()
    {
        ImGui::TextWrapped("Edit the live appearance of the player or your target -- transmog, scale, "
                           "profession/sex, and per-slot equipment + dye. Save looks and toggle whole states.");
        if (!Game::Ready())
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.f), "Waiting for GWCA to initialise...");
        ImGui::Separator();

        DrawSource();
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Transmog (whole model)", ImGuiTreeNodeFlags_DefaultOpen)) DrawTransmog();
        if (ImGui::CollapsingHeader("Scale")) DrawScale();
        if (ImGui::CollapsingHeader("Identity (profession / sex)")) DrawIdentity();
        if (ImGui::CollapsingHeader("Equipment (armor + dye)")) DrawEquipment();

        ImGui::Separator();
        DrawApplyBar();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Saved characters", ImGuiTreeNodeFlags_DefaultOpen)) DrawSavedCharacters();
        if (ImGui::CollapsingHeader("Global states")) DrawGlobalStates();

        ImGui::Separator();
        ImGui::TextWrapped("%s", g_status.c_str());
    }

    // --- command parsing helpers ------------------------------------------------
    bool ParseSlotVerb(const std::vector<std::string>& t)
    {
        // slot <n> model <id> | slot <n> dye <c0> <c1> <c2> <c3> [tint] | slot <n> off
        if (t.size() < 3) return false;
        const int n = std::atoi(t[1].c_str());
        if (n < 0 || n > 8) { g_status = "slot must be 0..8"; return true; }
        EnsureSlots(g_edit);
        SlotEdit& s = g_edit.slots[n];
        s.slot = n;
        if (t[2] == "off") { s.enabled = false; g_status = "slot disabled"; return true; }
        g_edit.use_equipment = true;
        s.enabled = true;
        if (t[2] == "model" && t.size() >= 4) {
            s.set_model = true;
            s.model_file_id = static_cast<uint32_t>(strtoul(t[3].c_str(), nullptr, 0));
        } else if (t[2] == "dye" && t.size() >= 7) {
            s.set_dye = true;
            s.dye0 = std::atoi(t[3].c_str());
            s.dye1 = std::atoi(t[4].c_str());
            s.dye2 = std::atoi(t[5].c_str());
            s.dye3 = std::atoi(t[6].c_str());
            if (t.size() >= 8) s.dye_tint = std::atoi(t[7].c_str());
        }
        g_status = "slot " + t[1] + " set (press/queue apply)";
        return true;
    }

} // namespace

namespace Editor {

    void Init()
    {
        EnsureSlots(g_edit);
        g_visible = true;   // never let a persisted "closed" strand the panel (Gw.exe has no menu)
        LoadDoc();
        GL_DLLLOG("Editor::Init: ready (%zu characters, %zu states)", g_characters.size(), g_states.size());
    }

    bool& WindowVisible() { return g_visible; }

    void Draw(IDirect3DDevice9* /*device*/)
    {
        if (g_read_req) {
            g_read_req = false;
            if (AppearanceApply::ReadSource(g_source, g_edit)) { EnsureSlots(g_edit); g_status = "Read source appearance."; }
        }
        if (g_npcdump_req) {
            g_npcdump_req = false;
            const size_t total = AppearanceApply::NpcListSnapshot(g_npc_rows, 4096);
            GL_DLLLOG("Editor: NPC table has %zu defined; window from id %d:", total, g_npcdump_min);
            int shown = 0;
            for (const auto& r : g_npc_rows) {
                if (static_cast<int>(r.id) < g_npcdump_min) continue;
                GL_DLLLOG("  npc #%u  model %u  prof %d", r.id, r.model_file_id, r.primary);
                if (++shown >= 24) break;
            }
        }
        if (g_composite_dump_req) {
            g_composite_dump_req = false;
            std::vector<AppearanceApply::CompositeRow> rows;
            const size_t total = AppearanceApply::CompositeSnapshot(rows, 65536);
            std::ofstream f(Settings::Dir() / L"composites.tsv", std::ios::binary | std::ios::trunc);
            if (f) {
                f << "#model_file_id\tclass_flags\tf0\tf1\tf2\tf3\tf4\tf5\tf6\tf7\tf8\tf9\tf10\n";
                for (const auto& r : rows) {
                    f << r.model_file_id << '\t' << r.class_flags;
                    for (int k = 0; k < 11; ++k) f << '\t' << r.file_ids[k];
                    f << '\n';
                }
            }
            GL_DLLLOG("Editor: dumped %zu composites -> composites.tsv (%zu total non-empty)", rows.size(), total);
        }
        if (!g_visible) return;

        ImGui::SetNextWindowSize(ImVec2(460, 640), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(80, 60), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Guildlite - Model Editor", &g_visible)) {
            DrawPanel();
        }
        ImGui::End();
    }

    void Shutdown()
    {
        SaveDoc();
        // Deliberately do NOT auto-revert live edits here: a hot-reload would flicker the
        // character every reload. The server resets appearance on zone anyway; "Revert" is manual.
        GL_DLLLOG("Editor::Shutdown: saved library");
    }

    void Command(const char* verb)
    {
        if (!verb) return;
        std::vector<std::string> t;
        {
            std::istringstream is(verb);
            std::string s;
            while (is >> s) t.push_back(s);
        }
        if (t.empty()) return;
        const std::string& cmd = t[0];

        if (cmd == "apply")       { AppearanceApply::ApplyToSource(g_source, g_edit); g_status = "Applied."; return; }
        if (cmd == "revert")      { AppearanceApply::RevertSource(g_source); g_status = "Reverted source."; return; }
        if (cmd == "revert-all" || cmd == "revertall") { AppearanceApply::RevertAll(); g_status = "Reverted all."; return; }
        if (cmd == "read")        { g_read_req = true; return; }   // deferred to the render thread
        if (cmd == "npcs")        { g_npcdump_min = (t.size() >= 2) ? std::atoi(t[1].c_str()) : 0; g_npcdump_req = true; return; }
        if (cmd == "composites")  { g_composite_dump_req = true; return; }  // -> Documents\guildlite\composites.tsv
        if (cmd == "source" && t.size() >= 2) {
            g_source = (t[1] == "target" || t[1] == "1") ? TargetSource::Target : TargetSource::Player;
            return;
        }
        if (cmd == "transmog" && t.size() >= 2) {
            g_edit.use_transmog = true;
            g_edit.transmog_npc_id = static_cast<uint32_t>(strtoul(t[1].c_str(), nullptr, 0));
            g_status = "transmog npc " + t[1] + " (queue apply)";
            return;
        }
        if (cmd == "scale" && t.size() >= 2) {
            g_edit.use_scale = true;
            g_edit.scale_percent = std::atoi(t[1].c_str());
            return;
        }
        if (cmd == "prof" && t.size() >= 2) {
            g_edit.use_professions = true;
            g_edit.primary = std::atoi(t[1].c_str());
            if (t.size() >= 3) g_edit.secondary = std::atoi(t[2].c_str());
            return;
        }
        if (cmd == "sex" && t.size() >= 2) {
            g_edit.use_sex = true;
            g_edit.female = (t[1] == "female" || t[1] == "f" || t[1] == "1");
            return;
        }
        if (cmd == "slot") { ParseSlotVerb(t); return; }
        if (cmd == "save" && t.size() >= 2)   { SaveCurrentAs(t[1]); return; }
        if (cmd == "load" && t.size() >= 2)   { LoadCharacter(t[1]); return; }
        if (cmd == "delete" && t.size() >= 2) { DeleteCharacter(t[1]); return; }
        if (cmd == "states") { AppearanceApply::ApplyStates(g_states); g_status = "Applied enabled states."; return; }
        if (cmd == "state" && t.size() >= 3) {
            for (auto& s : g_states) if (s.name == t[1]) { s.enabled = (t[2] == "on" || t[2] == "1"); SaveDoc(); }
            return;
        }
        if (cmd == "list") {
            GL_DLLLOG("Editor: %zu characters, %zu states", g_characters.size(), g_states.size());
            for (const auto& c : g_characters) GL_DLLLOG("  char '%s'", c.name.c_str());
            for (const auto& s : g_states) GL_DLLLOG("  state '%s' %s prio %d", s.name.c_str(), s.enabled ? "on" : "off", s.priority);
            return;
        }
        GL_DLLLOG("Editor::Command: unknown verb '%s'", verb);
    }

} // namespace Editor
