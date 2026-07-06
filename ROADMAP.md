## INJECTION ROADMAP in PHASES
- [x] Injector: Init, ImGui base and info (overlay), screenshots via backbuffers (dev superpower over ssh)
- [x] Exporter: Port the 6 files, write the 4 glue modules, wire the control panel. Runs standalone. → `injector/src/{Capture,ObjWriter,TextureExport,GameState,GuildliteConfig}` + `{Game,Settings,Exporter,Overlay}`.
- [x] Dev Loop: stub + reloadable core + control file — iterate from the Mac over SSH with visual verification, no re-injection. → `guildlite-stub.dll` + `guildlite-core.dll`, control file at `Documents\guildlite\control`. **macOS Metal console shipped** (`gui/`, `./build.sh --macos`). See INJECTOR.md.
- [ ] Fun: free camera, shared-state, Prop Hunt, weather, etc.

---

# MODEL EXPORT

The standing goal: **press one button, get a reliable, clean, complete export of the
target — and nothing else.** Everything below is ordered against that. This section was
rewritten 2026-07-06 to fold in the ModelMod-reference session (see `MODELMOD-FINDINGS.md`)
and what it proved live on the client.

## IMMEDIATE — priority order

Effort key: **S** = hours, **M** = ~a day, **L** = multi-day / research.

### Blockers to a clean whole character
1. [ ] **Isolation — one agent out of the crowd** — **M**. `require_skinned` keeps *every*
   character in range, not just the subject; the bone-palette match (`isolate_by_bone`)
   returned **0 on map 280** because its register calibration was taken on map 481. Fix:
   take one Probe capture on 2–3 maps, solve which constant register block holds the
   per-agent translation + the world→render scale, then make isolation **self-calibrating**
   (scan for the register block whose per-agent translations actually separate, anchor to
   the GWCA target's position). Unblocks: "mark whole character", crowd isolation, and the
   single-button target export. This is the last hard piece of the original
   "press a button and get your target reliably" item.
2. [ ] **Close the still-missing armor piece** — **S–M**. Depth-test-off inclusion
   recovered the robe/skirt (big win — see Findings), but a piece of armor is still absent.
   Diagnose: one capture with `log_draws` on + `exclude_2d off`, find the armor-sized
   skinned draw and read its `reason` — most likely `dedup` (a shared vb/ib/range signature
   colliding with another draw) or a distinct blend/stream state we drop. Unblocks: a
   geometrically complete character.

### Quick wins
3. [ ] **`clean-full` profile** — **S**. `require_skinned + drop_effects + exclude_2d off`.
   `require_skinned` drops the HUD for free (HUD isn't skinned), `drop_effects` kills auras,
   `exclude_2d off` is what finally brings the armor. One click → a complete **solo**
   character today (crowd still needs #1). This is the pragmatic single-button for now.
4. [ ] **A pure "character" filter beyond `has_blend`** — **S–M**. GW vertex-animates
   foliage/water/terrain, so `has_blend` ("skinned") over-includes them (confirmed: the
   clean-solo render was full of flowers). Options: detect the character skinning **vertex
   shader** (distinct from the terrain/foliage shaders) — sturdier; or a **drop-huge-draws**
   size gate (terrain is 2048-tri / 2048² textured vs character pieces ≤~1200-tri / ≤1024²)
   — cheaper, partial.
5. [ ] **Pick-mode polish** (mostly after #1) — **S** each. Sort/group the pick list by agent
   so one character's pieces are contiguous (needs #1's per-agent key); hotkeys for
   cycle/mark; "mark all in filtered view".

### Bigger / deferred
6. [ ] **Multi-frame (time-window) capture** — **M**. Accumulate draws over N ms instead of
   one armed frame (ModelMod's `snap_ms`; comment: one begin/end "often misses data"). Catches
   geometry split across passes/frames and firms up whole-scene grabs. (The earlier ">1ms
   frame capture" note.)
7. [ ] **Animation: rig/pose from the captured substrate** — **L (research)**. The binding is
   already exported (`#vbld` = per-vertex bone indices/weights) and the palette is dumpable
   (Probe → manifest `probe[]`). Remaining work is assembly: reconstruct bone transforms from
   the palette and apply/attach a skeleton. Nobody in the ModelMod reference got here; the raw
   material is captured, so we're closer than they were.
8. [ ] **Non-skinned effects / spectral inclusion** — **M**. Auras/glows/some weapons aren't
   skinned (no bone palette), so agent-grouping can't collect them and they're easy to miss.
   Mark by draw-order adjacency to the agent, or leave to manual multi-select.

## Shipped this session (2026-07-06)
- [x] **ModelMod reference investigation** → `MODELMOD-FINDINGS.md`. Verdict: it uses our
  identical `DrawIndexedPrimitive` snapshot mechanism (no DAT path to leapfrog); the leverage
  is *select-a-draw vs filter-a-scene* + preserving the skinning substrate we were discarding.
- [x] **Skin-weight substrate** — capture + export GW's per-vertex bone indices (packed in a
  D3DCOLOR beta) as OBJ `#vbld` lines + manifest `weighted`. Verified: 30+ bones, real skeleton.
- [x] **Pick mode** — interactive select-one-draw with in-game **green highlight**; stable
  signature-keyed list; **skinned-only** and **include-depth-test-off** filters; **multi-select**
  (mark several → snap as one model); control verbs `pick on/off/next/prev/skinned/2d/mark/clear/target`, `snap`.
- [x] **Depth-test-off armor reach** — the fix that took a capture from body-only to a
  near-complete character (head/body/robe/legs/feet).
- [x] **Overlay panel bar + reopen-on-reload** — always-visible bottom-left toggles
  (Exporter/Overlay/Demo); exporter can't get stranded closed.
- [~] **Whole-character grouping (`pick target`)** — built, but bone-palette-calibration-bound
  (0 on map 280); gated behind #1.

## Findings that shape the above
- **The armor is drawn depth-test-off.** Census proved every draw is a triangle-list
  `DrawIndexedPrimitive` (no strips / `*UP` siblings), so both pick's z-gate and capture's
  `exclude_2d` were the cause. This is very likely the long-standing "entire torso/bodies
  missing" symptom.
- **Bone-palette isolation is map/shader-specific** — great when calibrated (map 481), 0 on
  map 280. Needs self-calibration (#1) before it's dependable.
- **`has_blend` ("skinned") ≠ "character"** in leafy zones — GW vertex-animates the
  environment. Needs a better signal (#4).
- **GW packs bone indices in a `D3DCOLOR` beta** (`XYZB1 + LASTBETA_D3DCOLOR`), rigid
  single-bone, no explicit weights — read in raw byte order (byte 0 = the bone).

---

# PRIOR EXTRACTION MILESTONES

### Basic Model Extraction — DONE
*Plain, uncoloured 3D models from live game state.*
- [x] Hook the render pipeline for active geometry → `DrawIndexedPrimitive` (vtable[82]) via MinHook; `Capture.{h,cpp}`.
- [x] Export to `.obj` / `.stl` → `ObjWriter.{h,cpp}`, true vertex-declaration/FVF decode, 16/32-bit indices, dedupe, bounds checks.

### Advanced Model Extraction — DONE (live-render path); DAT path still future
*Textures, components, animation/skeleton info.*
- [x] Texture extractor to PNG/**TGA** → `TextureExport.{h,cpp}`, dependency-free: software DXT1/3/5 + `StretchRect` readback. Reads live bound textures, not the DAT.
- [x] UV coordinates + `.mtl` generation paired with extracted textures.
- [~] Armor/weapon systems: per-slot equipment, `model_file_id`s, dyes → manifest (identity only; per-slot **geometry** gating is DAT work).
- [~] Character state / skeleton / animation structures: model/animation **state ids** + equipment identity via GWCA → manifest. Per-vertex bone binding now exported (`#vbld`); true skeletal/frame export is future (#7).

----

# FUTURE ROADMAP / IDEAS

### /chest command
### Investigate TexMod Integration

### Client-Side Free Camera
*Goal: Break the client renderer away from character/skeleton and fly the camera around the rendered map.*
*Concerns: likely fine, but check carefully. Second test account purchased for safety and future character↔character testing.*

### Client-Side Scene Compositing
*Goal: Edit and add to the client's game world for content creation.*
*Inspiration: RuneLite's [Creator Kit](https://github.com/ScreteMonge/creators-kit) 'Anvil' compositor — creative in-game compositions with animations, actions, replayability for content creators.*
* Client-side API to copy the `model_id` and appearance of targeted players/NPCs.
* Change the player's own `model_id`, skeleton, and animation state client-side.
* "Compositing Mode": spawn fake, client-side-only models/NPCs at coordinates (place models in the world). *(ModelMod's `MeshRelation` — weight a new mesh to the live skeleton without having the skeleton — is the reference trick here.)*

### Direct DAT Asset Loading
*Goal: Parse the DAT directly, so assets don't depend on active game state.*
* Port DAT parsing concepts from `FULL-ENGINE.md` (Phase 1/2) into C++.
* Basic asset loader (`guildlite-assets`) to locate/read `model_file_id` records from `Gw.dat`.
* Expand the exporter to output models extracted directly from the DAT.

### Model Browser & Search UI
*Goal: Interactive library of all game models.*
*Limitations: bound by DAT/GWA structure; MVP may be a 'seen objects' browser.*
* ImGui Model Browser with search; categorize DAT contents (Objects, Armor, Items, Animations, Models, Terrain); connect to the asset loader + exporter (search "Chaos Axe" → export). (Stretch) wireframe/thumbnail preview.

### Shared State & Mini-Games
*Goal: Network plugin state for collaborative/fun features.*
* Shared-state protocol (lightweight external WebSocket, or encode state in hidden party chat — no private server). Random `model_id` setter. Combine → "Prop Hunt" (users see each other's disguised forms).

### Future Research
* The new iOS/iPad Guild Wars app — possible entry points (e.g. injecting into the app).
</content>
