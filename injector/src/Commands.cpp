#include "Commands.h"

#include "stl.h" // prelude: <Windows.h> + <functional> the GWCA managers assume, BEFORE any GWCA header

#include "Game.h"
#include "Log.h"

#include <GWCA/Utilities/Hook.h>          // GW::HookEntry / GW::HookStatus (full defs for the chat hook)
#include <GWCA/Constants/Constants.h>     // GW::Constants::InstanceType
#include <GWCA/Managers/ChatMgr.h>        // GW::Chat: CreateCommand / DeleteCommand / WriteChat
#include <GWCA/Managers/ItemMgr.h>        // GW::Items: OpenXunlaiWindow / CanAccessXunlaiChest
#include <GWCA/Managers/GameThreadMgr.h>  // GW::GameThread::Enqueue
#include <GWCA/Managers/MapMgr.h>         // GW::Map::GetInstanceType
#include <GWCA/Managers/UIMgr.h>          // GW::UI frame lookup for the open/close toggle

#include <atomic>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

    std::atomic<bool> g_registered{false};
    // Persistent hook entry that owns our in-game /command registrations. Static storage so its
    // address stays valid for GWCA's lifetime; DeleteCommand on Shutdown detaches it so a hot
    // core-reload never leaves GWCA calling a freed callback.
    GW::HookEntry g_chatEntry;

    // ---- individual command actions (all run on the game thread) --------------------------

    void ChestOnGameThread()
    {
        if (!GW::Items::CanAccessXunlaiChest()) {
            // Xunlai only opens where the server allows it (towns/outposts). Tell the player
            // rather than silently doing nothing. (A cached read-only "saved chest" view for
            // other maps is a future feature -- see ROADMAP account-inventory notes.)
            GW::Chat::WriteChat(GW::Chat::CHANNEL_GWCA1,
                                L"Xunlai storage can only be opened in a town or outpost.",
                                L"Guildlite", true);
            return;
        }
        // Toggle like GWToolbox: if the storage frame is already up, close it; else open it.
        if (auto* frame = GW::UI::GetFrameByLabel(L"InvAccount"))
            GW::UI::DestroyUIComponent(frame);
        else
            GW::Items::OpenXunlaiWindow();
    }

    void CmdChest()
    {
        if (!Game::Ready()) {
            GL_DLLLOG("Commands: /chest ignored -- GWCA not ready");
            return;
        }
        GW::GameThread::Enqueue([]() { ChestOnGameThread(); });
    }

    // ---- command table (control-verb + in-game slash + action) ----------------------------

    struct Command {
        const char*    verb;   // control-file / SSH verb and primary name
        const wchar_t* slash;  // in-game slash command (registered with GW::Chat)
        void (*run)();         // the action
    };
    const Command kCommands[] = {
        { "chest",  L"chest",  &CmdChest },
        { "xunlai", L"xunlai", &CmdChest },
    };

    const Commands::Doc kDocs[] = {
        { "/chest  (alias /xunlai)", "open Xunlai storage in a town/outpost -- in-game, Info panel, control file, or SSH" },
    };

    // Single in-game chat callback; dispatches by the command name GWCA passes back.
    void __cdecl OnSlashCommand(GW::HookStatus*, const wchar_t* cmd, int, const LPWSTR*)
    {
        if (!cmd) return;
        for (const auto& c : kCommands) {
            if (std::wcscmp(cmd, c.slash) == 0) { c.run(); return; }
        }
    }

} // namespace

namespace Commands {

    void Init()
    {
        g_registered.store(false);
    }

    void Tick()
    {
        // Register the in-game /commands exactly once, on the game thread, after GWCA + its
        // hooks are live. Cheap: a single relaxed load per frame after that.
        if (g_registered.load(std::memory_order_relaxed)) return;
        if (!Game::Ready()) return;
        g_registered.store(true, std::memory_order_relaxed);
        GW::GameThread::Enqueue([]() {
            for (const auto& c : kCommands)
                GW::Chat::CreateCommand(&g_chatEntry, c.slash, OnSlashCommand);
            GL_DLLLOG("Commands: registered in-game slash commands (/chest, /xunlai)");
        });
    }

    void Shutdown()
    {
        if (!g_registered.exchange(false)) return;
        // Detach the chat hook so a freed callback is never invoked after a core hot-reload.
        // GWCA is still resident here (teardown runs before GW::Terminate), so this is safe.
        if (Game::Ready())
            GW::Chat::DeleteCommand(&g_chatEntry);
    }

    bool Dispatch(const char* verb)
    {
        if (!verb) return false;
        std::string head(verb);
        if (const size_t sp = head.find(' '); sp != std::string::npos)
            head.resize(sp);                       // first token only
        if (!head.empty() && head.front() == '/')
            head.erase(0, 1);                      // tolerate a leading slash from SSH
        for (const auto& c : kCommands) {
            if (head == c.verb) { c.run(); return true; }
        }
        return false;
    }

    const Doc* Table(int* count)
    {
        if (count) *count = static_cast<int>(sizeof(kDocs) / sizeof(kDocs[0]));
        return kDocs;
    }

} // namespace Commands
