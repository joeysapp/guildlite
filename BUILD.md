## BUILD
Complete the following two priority items that relate to the new Pick Mode in Model Exporter, requiring a number of UX/UI improvements to the model exporter Imgui panel as well as riogorous polish of the pick-mode selector list.

**STATUS — shipped 2026-07-06 (all four items + the real blockers underneath).** The pick snap
"outputs nothing" driver turned out to be two capture bugs the UX work exposed; both fixed. Details:

- [x] **Review — once-over Model Exporter coherence** — root-caused, not just papered over. The
  picker "reorder / can't deselect green / snap matches nothing" was **pointer-based `DrawKey`** (GW
  recycles the vertex buffer every frame) → now a content **`PickSig`** (prim/vert/stride). Cursor
  vs mark now distinct tints (**amber vs green**) so a green row can be deselected. Coherence banners
  + per-section "not used by a Snap" notes added. A/B tested throughout via SSH → Blender renders.
 - [x] **`clean-full` profile** — built, then A/B **corrected the recipe**: `require_skinned` was
  WRONG (it drops GW's non-skinned dress/armor → the "missing armor"); now `require_skinned=false`,
  size-gated scenery instead. Whole-state-reset profiles + dropdown/Apply/Save/Reload UI;
  `drop_effects`/`export_skin_weights` now **persist** (were reverting on reload); `settings` verb +
  gw-ctl/gw-loop docs for set-and-verify over SSH.
- [x] **Multi-frame (time-window) capture** — pick snap now **accumulates across frames** until every
  marked signature is grabbed (or times out); a single EndScene often lands on a minor pass and
  misses the world draws (that was the `hook_calls=7` symptom). Content-sig dedupe prevents ghosting.
 - [x] **Pick-mode polish** — running-window list (intermittent pieces stay markable), **Clear list**,
  **Mark all in view**, stable `#id` rows (no shuffle). Per-agent grouping still gated behind
  isolation self-calibration (unchanged; the hard isolation problem).

**Emergent (beyond the brief):** non-skinned pieces are baked into **world space** → new
`AlignWorldSpaceChunks` re-seats them onto the bind-pose body (`local = Rz(-facing)·(world-pos)`).
Gives a **complete, assembled character** for root-attached garments (dress). Multi-bone limb armor
(pauldrons/greaves, one draw across several bones, in the LIVE pose) only gets *near* — exact seating
needs per-bone posing → ROADMAP **#pose / Rig-pose**. Left as documented future work per decision.

## GOAL
The standing goal: **press one button, get a reliable, clean, complete export of the
target — and nothing else.** Everything below is ordered against that. This section was
rewritten 2026-07-06 to fold in the ModelMod-reference session (see `MODELMOD-FINDINGS.md`)
and what it proved live on the client.

## BACKGROUND
The new model export Pick Mode has entered into a configuration state where it does not output anything - likely a per-map probe issue or bone isolation issue - but cannot be diagnosed/debugged in the Model Exporter's current state.

The ModelMod tool was consulted to pivot the Model Exporter's filter-based export to selection-based for the MVP - clean solo target exports. Items are now reliably selected and exported using the Pick Mode but immediate issues have been identified with the initial build and need to be resolved before we can push the initial build to QA for testing.

Review the ./ROADMAP.md MODEL EXPORT immediate section for the immediate next-up build items. The following items are identified as likely quick resolvers to the issue:
 - Review how the Pick Mode interacts with the rest of the tool - is it idempotent, and if so in Pick Mode should disable the rest of the tool or not - believed that the rest of the tool settings _do influence_ the capture as we are currently unable to export anything (but the final Snapshot export works in a variety of settings to be expected)
 - Coherent reframing of Model Exporter filter/documentation against new ModelMod selection path. Knowing that the filtering logic is quite useful but needs full integration/parity and updated/relevant documentation with our new selection for single-export only. Full scene with object placement is a 100% future feature we will proide.
 - Profile dropdown save/load with full gw-ctl/ssh integration
 - Pick-mode polishing

## REFERENCE AND RULES
- **HARD RULE:** Use the screenshot and blender rendering to get a full-view (all sides) rendering of picked capture.
 - Delay between all steps of build->reload->load profile->select character->capture screenshot, on reload profile/settings are often not set and the game needs >1s to paint and populate its texture picker
 - In buiding the clean-full profile, approximate a closest-best version we can use to begin with and A/B test with. Do this with intuition and several ssh->blender full renders and review.
- ./README.md, ROADMAP.md, INJECTOR.md - Current state of beginning lightweight injector using GWCA and network protocols for macOS -> Windows dev

### FINDINGS
- **Bone-palette isolation is map/shader-specific** — great when calibrated (map 481), 0 on
  map 280. Needs self-calibration (#1) before it's dependable.
- **GW packs bone indices in a `D3DCOLOR` beta** (`XYZB1 + LASTBETA_D3DCOLOR`), rigid
  single-bone, no explicit weights — read in raw byte order (byte 0 = the bone).

### BELIEVED REMAINING BLOCKERS TO CLEAN SOLO EXPORT BUTTON
1. **Isolation — one agent out of the crowd**: `require_skinned` keeps *every*
   character in range, not just the subject; the bone-palette match (`isolate_by_bone`)
   returned **0 on map 280** because its register calibration was taken on map 481. Fix:
   take one Probe capture on 2–3 maps, solve which constant register block holds the
   per-agent translation + the world→render scale, then make isolation **self-calibrating**
   (scan for the register block whose per-agent translations actually separate, anchor to
   the GWCA target's position). Unblocks: "mark whole character", crowd isolation, and the
   single-button target export. This is the last hard piece of the original
   "press a button and get your target reliably" item.
2.  **Close the still-missing armor piece**: Depth-test-off inclusion
   **DID NOT** recover the robe/skirt. but a piece of armor is still absent.
   Diagnose: one capture with `log_draws` on + `exclude_2d off`, find the armor-sized
   skinned draw and read its `reason` — most likely `dedup` (a shared vb/ib/range signature
   colliding with another draw) or a distinct blend/stream state we drop. Unblocks: a
   geometrically complete character. Remove comments about depth-test fixing this problem
   in code and UI, and harden A/B renders that incorrectly signalled this issue being fixed.

## LATER MODEL EXPORT FEATURES
- [ ] **A pure "character" filter beyond `has_blend`**: GW vertex-animates foliage/water/terrain, so `has_blend` ("skinned") over-includes them (confirmed: the clean-solo render was full of flowers). Options: detect the character skinning **vertex shader** (distinct from the terrain/foliage shaders) — sturdier; or a **drop-huge-draws** size gate (terrain is 2048-tri / 2048² textured vs character pieces ≤~1200-tri / ≤1024²) — cheaper, partial.
- [ ] **Animation: rig/pose from the captured substrate**: (Research->Build, or document how to begin using the rigging in external tools such as blender for later automation and animation rigging) The binding is already exported (`#vbld` = per-vertex bone indices/weights) and the palette is dumpable (Probe → manifest `probe[]`). Remaining work is assembly: reconstruct bone transforms from the palette and apply/attach a skeleton. Nobody in the ModelMod reference got here; the raw material is captured, so we're closer than they were.
- [ ] **Non-skinned effects / spectral inclusion**: Auras/glows/some weapons aren't skinned (no bone palette), so agent-grouping can't collect them and they're easy to miss. Mark by draw-order adjacency to the agent, or leave to manual multi-select.

## AFTER MODEL EXPORT CLEAN SOLO PROFILE CONFIRMED WORKING
1. Freecam tool
