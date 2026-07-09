#pragma once

// Guildlite command interface / chat interceptor (ROADMAP "Commands").
//
// One dispatch surface for slash-style commands that work identically three ways:
//   - typed in the in-game chat box  ("/chest")      -> GW::Chat::CreateCommand hook
//   - written to Documents\guildlite\control (SSH)   -> Overlay::Command -> Commands::Dispatch
//   - a button/row surfaced in the overlay Info panel
//
// Every command runs its GW work on the game thread (GW::GameThread::Enqueue) and is gated on
// Game::Ready(). Adding a command is a single row in the table in Commands.cpp; it then lights
// up in all three surfaces at once. The first command is `/chest` (Xunlai storage anywhere it
// is allowed -- towns/outposts -- via GWCA OpenXunlaiWindow).
namespace Commands {
    void Init();                      // called from Overlay::Install (defers all GWCA work)
    void Tick();                      // per-frame: lazily registers the in-game /commands once GWCA is up
    void Shutdown();                  // unregister the in-game commands (safe for the hot-reload core swap)
    bool Dispatch(const char* verb);  // control/SSH path: returns true if a command consumed the verb

    // Rows describing every command, for the Info panel's "Commands" section.
    struct Doc { const char* usage; const char* desc; };
    const Doc* Table(int* count);
}
