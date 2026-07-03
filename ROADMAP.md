## INJECTION ROADMAP in PHASES
- [x] Injector: Init, ImGui base and info (overlay), screenshots via backbuffers (dev superpower over ssh)
- [x] Exporter: Port the 6 files, write the 4 glue modules, wire your control panel. Exporter runs standalone. → `injector/src/{Capture,ObjWriter,TextureExport,GameState,GuildliteConfig}` ported; `{Game,Settings,Exporter}` + `Overlay` capture-hook wiring added; `guildlite.dll` monolith.
- [~] Dev Loop: stub + reloadable core + control file - iterate from the Mac over SSH with visual verification, no manual re-injection and restarts → `guildlite-stub.dll` + `guildlite-core.dll`, control file at `Documents\guildlite\control` (see INJECTOR.md). Applies to any dev concept with in-game verification. Potential Metal build for Gw.exe-less GUI work and remote control of other Guildlite clients over SSH (using stub and tools.)
- [ ] Fun: free camera, shared-state, Prop Hunt, weather, etc

---

# Rendering
## IMMEDIATE TODOS
- [ ] Investigate new ModelMod reference submodule to leapfrog/assist current state (manual/visual analysis of exports).
 - **NOTE**: The tool currently is built to work with GW2 and GW1 and uses DX11. The developers have an 'old' branch/previous-main system that uses DX9. It may be worth investigating/trawling for more relevant reference code.
- [ ] Use the human-sorted last batch of `dev-tool` results for meaningfully-classified and described renders - active goal ATM is reliably exporting a single, complete target with no noise.
 - [ ] Are we missing face normals in our calcs?

### Basic Model Extraction — DONE
*Goal: Hook into the game state for plain, uncolored 3D models.*

* [x] Investigate GWCA for hooks into the rendering pipeline / memory state to read actively rendered geometry. → `DrawIndexedPrimitive` (device vtable[82]) via MinHook; `Capture.{h,cpp}`.
* [x] Write a basic data exporter to a standard `.obj` or `.stl` file. → `ObjWriter.{h,cpp}`, with true vertex-declaration/FVF decoding, 16/32-bit indices, dedupe, bounds checks.

### Advanced Model Extraction — DONE (live-render path); DAT path still future
*Goal: Complete the first model extraction feature with textures, components, animation and any model/skeleton information.*
 - Recently discovered not all rendering systems are the same e.g.:
  - A significant amount of model 'activate layer' (on top of the character, an entity itself that may or may not be visible) Windows 3D App Viewer
* [~] Investigate common misses on renders and determine causors - entire torso and bodies missing, slices of clothing, faces, more
* [x] Texture extractor to standard formats (PNG/**TGA**). → `TextureExport.{h,cpp}`, dependency-free: uncompressed convert + software **DXT1/3/5** decode + GPU `StretchRect` readback fallback. *Reads the live bound textures, not the DAT — the DAT texture-format research remains for the DAT Tool.*
 GWCA; true skeletal/animation-frame export needs the DAT/memory work (deferred, honestly noted in-manifest).*
* [x] Update the exporter to include UV coordinate mappings (`.mtl` generation) paired with extracted textures.

### Character Vertex Colored Skins AND Animations,
* [~] Map the armor and weapon systems: per-slot equipment, model_file_ids, dyes recorded to the manifest via AgentLiving::equip 
* [~] Research character state, skeleton, animation structures. → animation/model **state ids** + equipment identity read via GWCA and logged to the manifest. *Skeleton/bone geometry is not in
- [ ] **Are GW character skins in a vertex shader so all agents overlap at the origin?**
* [ ] Rendering system seems _GOOD_ but bad about certain really important things, like:
 - The ability to press a button and get your target/scene reliably without having to double check
   - Some workaround potentially wildly move like intercepting/MiTM movement packets while trying to get a photo of yourself or placing relative
- [ ] Armor / Weapons: recorded to manifest; per-slot geometry gating is future DAT work
- [ ] Animation: live pose captured + state ids logged; scalar/frame export is future

----

# FUTURE ROADMAP / IDEAS

### /chest command
### Investigate TexMod Integration

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
