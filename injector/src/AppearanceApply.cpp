#include "AppearanceApply.h"

#include "stl.h" // prelude: <Windows.h> + <functional> the GWCA managers assume, BEFORE any GWCA header

#include "Game.h"
#include "Log.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Item.h>
#include <GWCA/GameEntities/NPC.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Packets/StoC.h>

#include <algorithm>
#include <atomic>
#include <map>

using namespace Guildlite;

namespace {

    // The sex bit inside AgentLiving::type_map (matches GW::AgentLiving::GetIsFemale()).
    constexpr uint32_t kFemaleBit = 0x200;
    // GW's model-type flag OR'd into transmog_npc_id; we strip it when reading one back.
    constexpr uint32_t kTransmogFlag = 0x20000000;

    // One agent's pre-edit state, captured the first time we touch it so Revert restores the
    // real look and never bakes a spoof in as the "original". Keyed by agent_id, which the
    // server rerolls on zone -- so a stale entry is simply never matched again (harmless).
    struct Original {
        GW::Constants::ProfessionByte primary{};
        GW::Constants::ProfessionByte secondary{};
        uint32_t type_map = 0;
        bool has_equip = false;
        GW::ItemData items[9]{};
    };

    std::map<uint32_t, Original> g_orig;   // touched only inside game-thread lambdas
    std::atomic<int> g_edited{0};          // mirror of g_orig.size() for a race-free status read

    int Clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // Snapshot the agent's current state once. Runs on the game thread (called from the lambdas).
    void EnsureOriginal(GW::AgentLiving* a)
    {
        if (g_orig.find(a->agent_id) != g_orig.end()) return;
        Original o;
        o.primary = a->primary;
        o.secondary = a->secondary;
        o.type_map = a->type_map;
        if (a->equip && *a->equip) {
            o.has_equip = true;
            for (int i = 0; i < 9; ++i) o.items[i] = (*a->equip)->items[i];
        }
        g_orig.emplace(a->agent_id, o);
        g_edited.store(static_cast<int>(g_orig.size()));
    }

    // --- transmog + scale: emulate the StoC packets the server would send (GWToolbox's path). --
    // Must run on the game thread. npc_id 0 => AgentModel resets the agent to its real model.
    void DoTransmog(uint32_t agent_id, uint32_t npc_id)
    {
        if (npc_id != 0) {
            // Synthesize the NPC definition first (as GWToolbox does) so an NPC whose model
            // isn't resident still resolves. Best-effort: skip if the client has no such npc.
            if (GW::NPC* npc = GW::Agents::GetNPCByID(npc_id)) {
                GW::Packet::StoC::NpcGeneralStats s{};
                s.npc_id = npc_id;
                s.file_id = npc->model_file_id;
                s.scale = *reinterpret_cast<const uint32_t*>(&npc->visual_adjustment);
                s.flags = npc->npc_flags;
                s.profession = static_cast<uint32_t>(npc->primary);
                GW::StoC::EmulatePacket(&s);
                if (npc->model_files && npc->files_count > 0) {
                    GW::Packet::StoC::NPCModelFile m{};
                    m.npc_id = npc_id;
                    m.count = 1;
                    m.data[0] = npc->model_files[0];
                    GW::StoC::EmulatePacket(&m);
                }
            }
        }
        GW::Packet::StoC::AgentModel am{};
        am.agent_id = agent_id;
        am.model_id = npc_id;   // 0 resets to the real server model
        GW::StoC::EmulatePacket(&am);
    }

    void DoScale(uint32_t agent_id, int percent)
    {
        GW::Packet::StoC::AgentScale p{};
        p.agent_id = agent_id;
        p.scale = (static_cast<uint32_t>(Clampi(percent, 1, 255)) & 0xFF) << 24; // GW packs percent<<24
        GW::StoC::EmulatePacket(&p);
    }

    // --- equipment + dye: spoof equip->items[slot], then vtable-redraw (GWToolbox's Armory). --
    // Starts from the CURRENT ItemData so type/interaction/value stay valid; only the model and/
    // or dye the user set are overwritten. Runs on the game thread.
    void DoEquipment(GW::NPCEquipment* eq, const Appearance& cfg)
    {
        for (const SlotEdit& s : cfg.slots) {
            if (!s.enabled) continue;
            const int slot = s.slot;
            if (slot < 0 || slot > 8) continue;
            GW::ItemData d = eq->items[slot]; // preserve type/interaction/value from the real gear
            if (s.set_model) d.model_file_id = s.model_file_id;
            if (s.set_dye) {
                d.dye.dye_tint = static_cast<uint8_t>(s.dye_tint & 0xFF);
                d.dye.dye1 = static_cast<GW::DyeColor>(Clampi(s.dye0, 0, 13));
                d.dye.dye2 = static_cast<GW::DyeColor>(Clampi(s.dye1, 0, 13));
                d.dye.dye3 = static_cast<GW::DyeColor>(Clampi(s.dye2, 0, 13));
                d.dye.dye4 = static_cast<GW::DyeColor>(Clampi(s.dye3, 0, 13));
            }
            if (d.model_file_id == 0) continue; // nothing to draw in this slot
            const uint32_t cur = eq->items[slot].model_file_id;
            if (cur) eq->vtable->RemoveItem(eq, 0, static_cast<uint32_t>(slot)); // undraw only a live slot
            eq->items[slot] = d;                                        // spoof the slot
            eq->vtable->EquipItem(eq, 0, static_cast<uint32_t>(slot));  // redraw from the spoof
        }
    }

    // Resolve the id of the source agent (player or current target). Simple reads, any thread.
    uint32_t ResolveSourceId(TargetSource src)
    {
        if (!Game::Ready()) return 0;
        return (src == TargetSource::Player) ? GW::Agents::GetControlledCharacterId()
                                             : GW::Agents::GetTargetId();
    }

    GW::AgentLiving* ResolveLiving(uint32_t agent_id)
    {
        GW::Agent* base = GW::Agents::GetAgentByID(agent_id);
        return base ? base->GetAsAgentLiving() : nullptr;
    }

    // The one place every write to an agent goes through, always on the game thread.
    void ApplyOnGameThread(uint32_t agent_id, const Appearance& cfg)
    {
        GW::AgentLiving* a = ResolveLiving(agent_id);
        if (!a) return;
        EnsureOriginal(a);
        if (cfg.use_transmog) DoTransmog(agent_id, cfg.transmog_npc_id);
        if (cfg.use_scale)    DoScale(agent_id, cfg.scale_percent);
        if (cfg.use_professions) {
            a->primary = static_cast<GW::Constants::ProfessionByte>(Clampi(cfg.primary, 0, 10));
            a->secondary = static_cast<GW::Constants::ProfessionByte>(Clampi(cfg.secondary, 0, 10));
        }
        if (cfg.use_sex) {
            if (cfg.female) a->type_map |= kFemaleBit;
            else            a->type_map &= ~kFemaleBit;
        }
        if (cfg.use_equipment && a->equip && *a->equip) DoEquipment(*a->equip, cfg);
    }

    void RevertOnGameThread(uint32_t agent_id)
    {
        const auto it = g_orig.find(agent_id);
        if (it == g_orig.end()) return;
        const Original o = it->second;
        GW::AgentLiving* a = ResolveLiving(agent_id);
        if (a) {
            DoTransmog(agent_id, 0);   // back to the real model
            DoScale(agent_id, 100);    // back to normal size
            a->primary = o.primary;
            a->secondary = o.secondary;
            a->type_map = o.type_map;
            if (o.has_equip && a->equip && *a->equip) {
                GW::NPCEquipment* eq = *a->equip;
                for (int i = 0; i < 9; ++i) {
                    if (eq->items[i].model_file_id)
                        eq->vtable->RemoveItem(eq, 0, static_cast<uint32_t>(i)); // clear whatever is drawn
                    eq->items[i] = o.items[i];                                   // restore real gear
                    if (o.items[i].model_file_id)
                        eq->vtable->EquipItem(eq, 0, static_cast<uint32_t>(i));
                }
            }
        }
        g_orig.erase(it);
        g_edited.store(static_cast<int>(g_orig.size()));
    }

} // namespace

namespace Guildlite {
namespace AppearanceApply {

    void ApplyToAgent(uint32_t agent_id, const Appearance& ap)
    {
        if (!Game::Ready() || agent_id == 0) return;
        const Appearance cfg = ap; // own a copy for the deferred lambda
        GW::GameThread::Enqueue([agent_id, cfg]() { ApplyOnGameThread(agent_id, cfg); }, false);
    }

    void RevertAgent(uint32_t agent_id)
    {
        if (!Game::Ready() || agent_id == 0) return;
        GW::GameThread::Enqueue([agent_id]() { RevertOnGameThread(agent_id); }, false);
    }

    void ApplyToSource(TargetSource source, const Appearance& ap)
    {
        const uint32_t id = ResolveSourceId(source);
        if (id == 0) { GL_DLLLOG("AppearanceApply: no %s to edit", source == TargetSource::Player ? "player" : "target"); return; }
        ApplyToAgent(id, ap);
    }

    void RevertSource(TargetSource source)
    {
        const uint32_t id = ResolveSourceId(source);
        if (id) RevertAgent(id);
    }

    void RevertAll()
    {
        if (!Game::Ready()) return;
        GW::GameThread::Enqueue([]() {
            // Copy the ids first; RevertOnGameThread erases as it goes.
            std::vector<uint32_t> ids;
            ids.reserve(g_orig.size());
            for (const auto& kv : g_orig) ids.push_back(kv.first);
            for (uint32_t id : ids) RevertOnGameThread(id);
        }, false);
    }

    void ApplyStates(const std::vector<GlobalState>& states)
    {
        if (!Game::Ready()) return;
        // Enabled only, low priority first so the highest-priority state wins a contested field.
        std::vector<const GlobalState*> active;
        for (const auto& s : states) if (s.enabled) active.push_back(&s);
        std::stable_sort(active.begin(), active.end(),
                         [](const GlobalState* a, const GlobalState* b) { return a->priority < b->priority; });
        for (const GlobalState* s : active) {
            const StateTarget tgt = static_cast<StateTarget>(s->target);
            const Appearance ap = s->appearance;
            if (tgt == StateTarget::Self) {
                ApplyToSource(TargetSource::Player, ap);
            }
            else if (tgt == StateTarget::Target) {
                ApplyToSource(TargetSource::Target, ap);
            }
            else {
                // All players / all NPCs currently in the instance: resolve the id set now, on
                // the game thread, then apply to each (one-shot -- new spawns aren't auto-styled).
                const bool want_players = (tgt == StateTarget::AllPlayers);
                GW::GameThread::Enqueue([want_players, ap]() {
                    GW::AgentArray* arr = GW::Agents::GetAgentArray();
                    if (!arr) return;
                    for (GW::Agent* base : *arr) {
                        if (!base) continue;
                        GW::AgentLiving* a = base->GetAsAgentLiving();
                        if (!a) continue;
                        if (want_players ? a->IsPlayer() : a->IsNPC())
                            ApplyOnGameThread(a->agent_id, ap);
                    }
                }, false);
            }
        }
    }

    bool ReadSource(TargetSource source, Appearance& out)
    {
        if (!Game::Ready()) return false;
        GW::AgentLiving* a = (source == TargetSource::Player)
                                 ? GW::Agents::GetControlledCharacter()
                                 : GW::Agents::GetTargetAsAgentLiving();
        if (!a) return false;
        out.transmog_npc_id = a->transmog_npc_id ? (a->transmog_npc_id & ~kTransmogFlag) : 0;
        out.primary = static_cast<int>(a->primary);
        out.secondary = static_cast<int>(a->secondary);
        out.female = a->GetIsFemale();
        out.slots.assign(9, SlotEdit{});
        for (int i = 0; i < 9; ++i) out.slots[i].slot = i;
        if (a->equip && *a->equip) {
            for (int i = 0; i < 9; ++i) {
                const GW::ItemData& d = (*a->equip)->items[i];
                out.slots[i].model_file_id = d.model_file_id;
                out.slots[i].dye0 = static_cast<int>(d.dye.dye1);
                out.slots[i].dye1 = static_cast<int>(d.dye.dye2);
                out.slots[i].dye2 = static_cast<int>(d.dye.dye3);
                out.slots[i].dye3 = static_cast<int>(d.dye.dye4);
                out.slots[i].dye_tint = d.dye.dye_tint;
            }
        }
        return true;
    }

    int EditedCount() { return g_edited.load(); }

    size_t NpcListSnapshot(std::vector<NpcRow>& out, uint32_t max_rows)
    {
        out.clear();
        if (!Game::Ready()) return 0;
        GW::NPCArray* arr = GW::Agents::GetNPCArray();
        if (!arr) return 0;
        size_t total = 0;
        const uint32_t n = arr->size();
        for (uint32_t i = 0; i < n; ++i) {
            const GW::NPC& npc = (*arr)[i];
            if (npc.model_file_id == 0 && (npc.model_files == nullptr || npc.files_count == 0))
                continue; // empty definition slot
            ++total;
            if (out.size() < max_rows)
                out.push_back({i, npc.model_file_id, static_cast<int>(npc.primary)});
        }
        return total;
    }

} // namespace AppearanceApply
} // namespace Guildlite
