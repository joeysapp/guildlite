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
- [x] **Pick mode** — interactive select-one-draw with in-game **green highlight**; stable
  signature-keyed list; **skinned-only** and **include-depth-test-off** filters; **multi-select**
  (mark several → snap as one model); control verbs `pick on/off/next/prev/skinned/2d/mark/clear/target`, `snap`.
- [~] **Depth-test-off armor reach** — the fix that took a capture from body-only to a
  near-complete character (head/body/robe/legs/feet). **CONFIRMED NOT WORKING. DOCUMENT AND REVIEW, DO NOT LEAVE OLD CODE/COMMENTS THAT ARE MISLEADING. THE ARMOR OF PLAYERS IS STILL NOT CAPTURED RELIABLY.**
- [~] **Whole-character grouping (`pick target`)** — built, but bone-palette-calibration-bound
  (0 on map 280); gated behind #1.
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
