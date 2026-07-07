#pragma once

struct IDirect3DDevice9;

// The Model Editor control panel + client-side appearance application (ROADMAP "Model Editor").
// The third Guildlite tool, built to the same shape as Exporter/Freecam so the overlay hosts it
// identically: per-frame Draw() inside the overlay's ImGui frame, a panel-bar toggle, and a
// Command() surface for the control file / macOS console.
//
// Where the Exporter READS a character out of the game, the Editor WRITES to it: it edits the
// live appearance of the Source agent (player or target) via GWCA -- transmog to any NPC model,
// primary/secondary profession, sex, and per-slot equipment model + dye -- applied on the game
// thread (GW::GameThread::Enqueue) and reversible from a captured snapshot. Edits are CLIENT-SIDE
// only (nobody else sees them; the server re-syncs the real appearance on zone). On top of that
// it manages savable, named Character configs and a higher-level list of Global States (each with
// enable/priority/target) so a whole look -- or several at once -- can be recalled with one click.
namespace Editor {
    void Init();                          // load persisted characters + global states
    void Draw(IDirect3DDevice9* device);  // per-frame: drain queued verbs, draw the control window
    void Shutdown();                      // save characters + states (does NOT auto-revert live edits)
    void Command(const char* verb);       // SSH/stub-driven: "edit apply|revert|transmog <id>|save <name>|state ..."
    bool& WindowVisible();                // the editor window's open flag -- for the overlay's panel bar
}
