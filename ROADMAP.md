## INJECTION ROADMAP in PHASES
- [x] Injector: Init, ImGui base and info (overlay), screenshots via backbuffers (dev superpower over ssh)
- [x] Exporter: Port the 6 files, write the 4 glue modules, wire the control panel. Runs standalone. → `injector/src/{Capture,ObjWriter,TextureExport,GameState,GuildliteConfig}` + `{Game,Settings,Exporter,Overlay}`.
- [x] Dev Loop: stub + reloadable core + control file — iterate from the Mac over SSH with visual verification, no re-injection. → `guildlite-stub.dll` + `guildlite-core.dll`, control file at `Documents\guildlite\control`. **macOS Metal console shipped** (`gui/`, `./build.sh --macos`). See INJECTOR.md.
- [ ] Fun: free camera, shared-state, Prop Hunt, weather, etc.

# MODEL EXTRACTION MILESTONES

### Basic Model Extraction — DONE
*Plain, uncoloured 3D models from live game state.*
- [x] Hook the render pipeline for active geometry → `DrawIndexedPrimitive` (vtable[82]) via MinHook; `Capture.{h,cpp}`.
- [x] Export to `.obj` / `.stl` → `ObjWriter.{h,cpp}`, true vertex-declaration/FVF decode, 16/32-bit indices, dedupe, bounds checks.

### Advanced Model Extraction — DONE (live-render path); DAT path still future
*Textures, components, animation/skeleton info.*
- [x] Texture extractor to PNG/**TGA** → `TextureExport.{h,cpp}`, dependency-free: software DXT1/3/5 + `StretchRect` readback. Reads live bound textures, not the DAT.
- [x] UV coordinates + `.mtl` generation paired with extracted textures.
- [x] **ModelMod reference investigation** → `MODELMOD-FINDINGS.md`. Verdict: it uses our
  identical `DrawIndexedPrimitive` snapshot mechanism (no DAT path to leapfrog); the leverage
  is *select-a-draw vs filter-a-scene* + preserving the skinning substrate we were discarding.
- [x] **Skin-weight substrate** — capture + export GW's per-vertex bone indices (packed in a
  D3DCOLOR beta) as OBJ `#vbld` lines + manifest `weighted`. Verified: 30+ bones, real skeleton.
- [x] **Pick mode** — interactive select-one-draw with in-game highlight. Hardened 2026-07-06:
  **content-keyed** stable list (`PickSig`=prim/vert/stride — survives GW recycling the vertex
  buffer every frame, which was why snaps "matched nothing" and the list bloated 8→54);
  **multi-frame accumulate** on snap (a single EndScene often lands on a minor pass and misses the
  world draws); **running-window** list; distinct **green=marked / amber=cursor** tints (so a green
  row can actually be de-selected); mark-all-in-view / clear-list; verbs
  `pick on/off/next/prev/skinned/2d/mark/markall/clearlist/clear/target`, `snap`, `settings`.
- [x] **Missing armor/robe — ROOT-CAUSED + fixed (it was NEVER a depth-test issue).** GW draws the
  dress/skirt and some armor as meshes it does **not flag as skinned**, so `require_skinned` dropped
  them and pick "Skinned only" hid them — the long-standing "coherent character, missing armor"
  symptom. Fix: `clean-full` no longer uses `require_skinned` (it size-gates scenery instead);
  "Skinned only" now warns it hides non-skinned pieces. Misleading depth-test "armor fix" claims removed.
- [x] **Non-skinned world-space re-seating** — those non-skinned pieces are baked into **world
  space** (skinned meshes are bind-pose-local at the origin), so a raw grab scattered them ~7000u
  from the body and the export rendered as a speck. Now re-seated onto the body via
  `local = Rz(-facing)·(world - agent_pos)` from the GWCA snapshot (`Exporter::AlignWorldSpaceChunks`).
  Result: a **complete, correctly-oriented** character. Caveat below (#pose).
- [~] **Pose reconstruction (`pose_to_live`) — prototyped + baked in.** Closes the "close, not
  exact" gap: poses the bind body forward into the LIVE frame from the **bone palette** so GW's
  live-pose non-skinned armor lines up. **Palette layout solved + self-calibrating**: per-draw,
  bone `k` → VS registers `c(base+3k)` as `[3x3|t]`; `base` found by scanning for the bone whose
  translation == the agent's GWCA pos (`Exporter::PoseChunks`). Rigid single-bone: `world_v =
  BoneMatrix[bone]·bind_v`. Verified: reconstructs a coherent live-pose body. Remaining: confirm
  every skinned chunk poses once all draws are probed (`kProbeMaxSamples` now 64); minor residuals
  (hair sat low). See FUTURE → **Rig/pose** for the full-skeleton/animation extension.
- [~] **Whole-character grouping (`pick target`)** — built, but bone-palette-calibration-bound
  (0 on map 280); gated behind isolation self-calibration.
- [~] Armor/weapon systems: per-slot equipment, `model_file_id`s, dyes → manifest (identity only; per-slot **geometry** gating is DAT work).
- [~] Character state / skeleton / animation structures: model/animation **state ids** + equipment identity via GWCA → manifest. Per-vertex bone binding now exported (`#vbld`); true skeletal/frame export is future.

----

# FUTURE ROADMAP / IDEAS

### Rig/pose the captured character — exact assembly + animation (#pose)
*Goal: make the re-seated non-skinned pieces line up EXACTLY, and unlock posed/animated export.*
The one gap left in the "one button → complete character" path: we export the skinned body in
**bind pose** while GW's non-skinned pieces (dress/skirt/armor) are baked in the **live** pose,
so the rigid re-seating (`AlignWorldSpaceChunks`) gets root + facing right but limb-level pose is
"close, not exact". Two regimes seen 2026-07-06:
* **Root-attached single garment (dress/skirt)** — the rigid re-seat seats it well; symmetric, so a
  residual facing error is invisible. This is the "close" MVP working as intended.
* **Multi-bone limb armor (pauldrons/greaves)** — GW packs plates for *several bones* (both shoulders,
  arms, legs) into ONE non-skinned draw, each baked at its own limb's live position. A single rigid
  transform brings the chunk *near* the body but CANNOT seat each plate — confirmed: no whole-chunk
  rotation seats them. This one genuinely needs per-bone placement (below), not a rotation tweak.
The raw material to close it is already captured:
* `#vbld` — per-vertex bone indices + weights (the mesh→bone binding), exported today.
* The bone **palette** (per-agent bone transforms) — dumpable to the manifest via Probe (VS constants).
Remaining work is assembly: reconstruct the bone transforms from the palette, then either (a) **pose
the bind-pose body forward** into the live frame so it matches the non-skinned pieces, or (b) build a
real skeleton + skinning so the export can be re-posed/animated in a DCC tool. Nobody in the ModelMod
reference got here; we're better positioned (GWCA gives the world transform). Cheap near-term step:
document how to hand-rig `#vbld` + palette in Blender for a one-off posed render.

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
