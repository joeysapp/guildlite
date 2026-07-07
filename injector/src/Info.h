#pragma once

// The unified "Info" panel (ROADMAP: unify Controls + Overlay). One window that carries, top to
// bottom: the live overlay STATUS (overlay/GWCA health, dev-loop actions), a COMMANDS crib of the
// control-channel verbs, and the self-documenting CONTROLS reference (keys + verbs today, a
// "Planned" list tracking the roadmap, and the standing honest caveats). Toggled from the panel
// bar's [Info] button (or the `info`/`controls`/`help` verb). When a feature lands, move its row
// from kPlanned to the right section in Info.cpp -- one line, one place.
namespace Info {
    void Draw();            // per-frame: the Info window (when open)
    bool& WindowVisible();  // panel-bar toggle / `info` verb
}
