#pragma once

// Self-documenting controls reference, surfaced in the overlay: every key + control-channel verb
// available today, a "Planned" list that tracks the roadmap, and the standing honest caveats.
// Pure UI (no game reads), toggled from the panel bar (or the `controls`/`help` verb) so future
// builds always carry an up-to-date map of what the tool can do. When a feature lands, move its
// row from kPlanned to the right section in Controls.cpp -- one line, one place.
namespace Controls {
    void Draw();            // per-frame: the controls/help window (when open)
    bool& WindowVisible();  // panel-bar toggle / `controls` verb
}
