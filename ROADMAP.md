# ROADMAP

- (!!) Propose a structure for lightweight injector systems structures to copmparse and contrast
 - (imo: less is more)
- (!!!!) Dev loop and hot reload with AI eval of both text and images
- (!!!!) Doing this on a LIVE gameserver with friends - ToS worth checking? Fairly certain the entire playerbase is doing a lot worse than what is planned here
- (!) Have not coded in clang in longtime; care about speed/defs/, low friction to introduce new globals and tools

## INJECTION ROADMAP in PHASES
- [x] Injector: Init, ImGui base and info (overlay), screenshots via backbuffers (dev superpower over ssh)
- [x] Exporter: Port the 6 files, write the 4 glue modules, wire your control panel. Exporter runs standalone. → `injector/src/{Capture,ObjWriter,TextureExport,GameState,GuildliteConfig}` ported; `{Game,Settings,Exporter}` + `Overlay` capture-hook wiring added; `guildlite.dll` monolith.
- [x] Dev Loop: stub + reloadable core + control file - iterate from the Mac over SSH with visual verification, no manual re-injection and restarts → `guildlite-stub.dll` + `guildlite-core.dll`, control file at `Documents\guildlite\control` (see INJECTOR.md).
- [ ] Fun: free camera, shared-state, Prop Hunt, weather, etc

## IMMEDIATE
- [ ] Clarify if below items are necessary oro have been invalidated by Injector pivot:
 - `./build.sh` - What is this used for now? Debug scripts
 - `./scripts/build_remote.ps1` - ? Still used to send build states back over SSH from Windows  ?
 - `./scripts/gwt.sh` - Expectation is this will be useful to control over ssh still? Could we rename to something more descriptive?
 - `./scripts/install_sshd_watchdog.jd1` - Admin-level Windows SSH timeout. Probably needed? Do we install/provide this to anything anywhere?
- [ ] Rendering complete build with dev loop (1. Screenshot Dev Loop, 2. Animation, Skeleton, Export)
- [x] `model-export`: Lifted out of the GWToolbox guildlite plugin, wired into the standalone injector
 - **DONE**: new layer over GWCA/ImGui — `injector/src/Exporter.{h,cpp}` drives the ported `Capture`/`ObjWriter`/`TextureExport`/`GameState` on our own D3D9 hook; settings via glaze JSON.
- [ ] UI and controls prototyping, settings and general setup/installs for others
* [~] MinHook on the D3D9 vtable
* [~] ImGui setup
 * [ ] Ensure is not intercepting mouse right clicks, cannot rotate camera after injection.. but after unload
* [~] Per-Frame Rendering
* [ ] SSH Setup
 * Complete and safe reload flow for live/hot reloading
 * GWCA - Controls, errors, logs, * all surfaced over SSH
 *  [~] tools/gwt.sh — Used as crutch - worth keeping? Rename to ssh? 
* [x] Unload injector, reloadable
* [~] Plan for config power items e.g.: commands, hotkeys, user templates, shortcuts, etc
* [ ] Remove GWToolboxpp references from own toolchains

---

# UI/UX
## Control and Settings
*Goal: Prototype out some ImGui items vs. a fully customizable theme for every button**

* [ ] Design and build with AI feedback via screenshots
* [ ] Settings saved to where?  What settings? When do we delete them?

---

# Rendering
## Completed build context into current and future tasks (bring model-exporter in ASAP)

### Basic Model Extraction — DONE
*Goal: Hook into the game state for plain, uncolored 3D models.*

* [x] Investigate GWCA for hooks into the rendering pipeline / memory state to read actively rendered geometry. → `DrawIndexedPrimitive` (device vtable[82]) via MinHook; `Capture.{h,cpp}`.
* [x] Write a basic data exporter to a standard `.obj` or `.stl` file. → `ObjWriter.{h,cpp}`, with true vertex-declaration/FVF decoding, 16/32-bit indices, dedupe, bounds checks.

### Advanced Model Extraction — DONE (live-render path); DAT path still future
*Goal: Complete the first model extraction feature with textures, components, animation and any model/skeleton information.*

* [x] Texture extractor to standard formats (PNG/**TGA**). → `TextureExport.{h,cpp}`, dependency-free: uncompressed convert + software **DXT1/3/5** decode + GPU `StretchRect` readback fallback. *Reads the live bound textures, not the DAT — the DAT texture-format research remains for the DAT Tool.*
* [~] Research character state, skeleton, animation structures. → animation/model **state ids** + equipment identity read via GWCA and logged to the manifest. *Skeleton/bone geometry is not in GWCA; true skeletal/animation-frame export needs the DAT/memory work (deferred, honestly noted in-manifest).*
* [x] Update the exporter to include UV coordinate mappings (`.mtl` generation) paired with extracted textures.
* [~] Map the armor and weapon systems. → per-slot equipment `model_file_id`s + dyes recorded to the manifest via `AgentLiving::equip`. *Per-slot geometry isolation / re-equipping is future DAT work; see Scope/Filter for isolating a worn item from a whole-body grab.*

### Render Issues to Remedy with Screenshots
*Goal: Addressing feedback of model export bugs*
* [x] Locality trimming via MAD, fix common up-axis rotated models, surface manifest v2 for audit and  debug
* [x] Up-axis remap, default Z->Y, head up: exports stand upright; kills the "~90°" rotation.
* [x] Texture UX: `.mtl` uses a white base + `map_d` (alpha cutouts); README documents the TGA→PNG / Blender workflow. (Extractor was already correct; macOS Quick Look just renders 32-bit TGA poorly.)
- [ ] *The real feature. GW skins in a vertex shader so all agents overlap at the origin; the isolation key is the agent world transform in the VS constant registers. 
* [ ] GWToolbox's `GameWorldCompositor` confirms GW's layout: view c0–c3, projection c4–c7 → the world/bone transform is beyond c7.*
* [x] Isolate (`Config::isolate_by_bone`, default off): the hook scans the palette registers and keeps a skinned draw only if some bone-triple's translation is within `isolate_tolerance` (~250u) of the Source agent's GWCA position. Validated offline: matches all of the target's draws, rejects a decoy 600u away. Scans a range, not a fixed offset, so it survives shader changes.
* [ ] Info surface - Config::probe_shader_constants: dumps `c0..c95` of the first skinned draws + their centers to the manifest.
* [ ] Skinned vs. bone renders - needs significant work after injection and model-export moving over
* [~] tools/obj_render.py — Used to verify rendering before AI shots - worth keeping?
- [ ] Armor / Weapons: recorded to manifest; per-slot geometry gating is future DAT work
- [ ] Animation: live pose captured + state ids logged; scalar/frame export is future

----

# FUTURE ROADMAP / IDEAS

### [NEW] Installation for friends - verify/instructions
### [NEW] /chest command
### [NEW] Investigate texmod difficulty or any red flags

### Client-Side Free Camera
*Goal: Break client renderer away from character / skeleton and allow camera flying around rendered map.*
*Concerns: Unclear - likely not a concern but being careful and checking is worth the effort. Have purchased a second test account for both personal safety and future character<->character testing and tooling.*

### Client-Side Scene Compositing
*Goal: Edit and add to the client's game world for content creation.*
*Inspiration: The [Creator Kit](https://github.com/ScreteMonge/creators-kit) for RuneLite (and several other tools like it) are used to great effect by the content creator community. Read through how its 'Anvil' compositor allows for creative in-game compositions with animations, actions and replayability as key features for content creators.*

* Implement a client-side API to copy the `model_id` and appearance of targeted players/NPCs.
* Build functionality to change the player's own `model_id`, skeleton, and animation state client-side.
* Develop a "Compositing Mode": The ability to spawn fake, client-side-only models or NPCs at specific coordinates (placing models in the world).

### Direct DAT Asset Loading
*Goal: Implement direct DAT file parsing to access assets without relying solely on active memory.*

* Port the DAT parsing concepts from `FULL-ENGINE.md` (Phase 1/2) into C++ within the plugin.
* Build a basic asset loader (`guildlite-assets` equivalent) capable of locating and reading `model_file_id` records directly from `Gw.dat`.
* Expand the basic exporter to output models extracted directly from the DAT file, independent of the active game state.

### Model Browser & Search UI
*Goal: Create an interactive library of all game models.*
*Limitations: Potentially bound by DAT/GWA structure. May act more as a 'seen objects' browser for MVP.*

* Build an ImGui-based Model Browser with search capabilities.
* Categorize the browsable DAT contents: Objects, Armor, Items, Animations, Models, and Terrain.
* Connect the UI to the asset loader and exporter so a user can search for a model (e.g., "Chaos Axe") and export it immediately.
* (Stretch) Implement basic wireframe preview or thumbnail generation within the ImGui window.

### Shared State & Mini-Games
*Goal: Network the plugin state between users for collaborative/fun features.*

* Implement a shared state protocol. Since we are not building a private server, explore using a lightweight external WebSocket server or encoding data in hidden party chat messages to share plugin state among friends.
* Develop the "Random `model_id` setter" feature.
* Combine shared state and random model IDs to build the "Prop Hunt" mini-game mode, where plugin users can see each other's disguised forms.

### Future Research
* Research Item: Investigating the new iOS/iPad app around Guild Wars for any possible entry points, e.g. injecting GWToolbox itself into the app.
