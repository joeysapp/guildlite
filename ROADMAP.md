# Comprehensive Build Plan & Sprints
Based on the strategic pivot outlined, this project transitions from building a full standalone Rust-based cross-platform client (as described in `FULL-ENGINE.md`) to creating a powerful C++ plugin for GWToolbox++ (GWTB) using the Guild Wars Client API (GWCA). 

The primary goal is to fill a specific tooling void: in-game model/texture extraction, manipulation, and scene compositing.

## High-Level Architecture (The Plugin Approach)
Instead of a clean-room implementation of the game client, we will leverage the existing active GW client.
* Core Engine & Renderer: Handled entirely by the official Guild Wars client and GWCA.
* Asset Pipeline: A hybrid approach. We will use GWCA to read active memory states (e.g., intercepting active rendered models) and combine this with direct `Gw.dat` parsing for a comprehensive model browser.
* UI System: ImGui (which GWToolbox already uses extensively).

## Priorities
- **Addressed in [3a71746](https://github.com/joeysapp/guildlite/commit/3a71746d2ff4545f6913393ec4730aced6bc8dfa):** A smooth workflow between the macOS host and the W10 target environment.

- The current three states (model extraction, DAT extraction) require a high level of human-or-visual verification. Claude has a build verify-forge harness but other agents and network-based checks do not. While we do not want to be tightly coupled, the active `topic-get` serer running at foxy.local:8080 (requires token or localhost) has full rendering capabilities. Consider the reality of [construct mesh+texture map] -> [render object to 2D canvas with forge API] -> [post to image classifier/judgement].

- State of the Game Check: Before deep-diving into DAT parsing, the prior sprint serves as a validation check to see if intercepting the active renderer is easier than DAT decryption

- Work and treat objects and meshes as primitive as possible - there is a likely possibility where we begin to import other objects to use from other games, etc. if possible!

---

# COMPLETED

Below is an ordered list detailing what has been built, what is currently being built, immediate next steps and future projected builds.

### Development Environment & Foundation - Complete
*Goal: Establish a reliable, repeatable local development pipeline for GWToolbox plugins.*

* [x] Clone the `GWToolboxpp` submodule and configure the C++ build environment (Visual Studio on Windows, or evaluate cross-compilation if developing on macOS).
* [x] Establish a deployment pipeline to compile and hot-load (or easily inject) the plugin into a running GW client for rapid iteration.
* [x] Build a "Hello World" plugin with a basic ImGui window to validate the pipeline.
* [x] Prototype a basic actionable interface (e.g., a button that prints the player's current `model_id` and position to the console using GWCA) to serve as an A/B test baseline.

### Windows, macOS and Linux Interactivity - Complete
*Goal: Compose a macOS-to-Windows-and-back set of tools for use in compiling, triaging errors and future test verifications dev to prod.*

* [x] Install Visual Studio Community 2026, Desktop Development for C++, reliably get base GWToolbox built and running.
* [x] Strengthen Windows remote accessibility. Nearly inaccessible at times, especially in PowerShell itself.
* [x] A comprehensive build script that functionally builds a working plugin, that can be dropped into any given GWToolbox/plugin directory. 

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

## BUILD

### UI/UX - Control and Settings — DONE
*Goal: Provide a comprehensive control/settings panel with upcoming NEXT/ROADMAP entries planned for.*

* [x] Replace the Snapshot button with a first-class floating control panel.
* [x] Build an interface for model extraction of current player OR selected object:
  - [x] Base: Source (player/target), export path (defaults to `<GWToolbox settings>/guildlite/`)
  - [x] Detail: Base (no textures) / Advanced (textures + UVs + manifest)
  - [~] Armor / Weapons: recorded to manifest; per-slot geometry gating is future DAT work
  - [~] Animation: live pose captured + state ids logged; scalar/frame export is future
  - [x] Export button + live status/stats + Scope/Filter for isolating a single model

### Verification tooling — DONE (new, per ROADMAP's strong recommendation)
* [x] `tools/obj_render.py` — stdlib-only OBJ→PNG software renderer for stepwise visual/AI verification. `--up {none,x,y,z}` reorients a raw grab (use `--up z` for GW Z-up).
* [x] `tools/gwt.sh` — SSH harness onto the live client: `state`/`ls`/`fetch`/`shot`/`cmd`/`render`/`loop`.

### MVP hardening — DONE (this sprint: exports are now viewable & upright)
*Diagnosed from real exports: the "invisible" grab was a compact character at the origin plus 3 stray 8-vertex effect quads at ~(18694,11381,-2715) that blew the bounds to 18711×11402×2729 — a per-chunk extent filter cannot catch them (small but far). And a raw grab is Z-up, so DCC/OS viewers (Y-up) show it lying on its side.*
* [x] **Locality trim** (`Config::trim_outliers`, MAD-based, default on): drops far-placed fliers → box collapses to a human silhouette (~33×37×88). Adaptive, so terrain grabs are barely touched.
* [x] **Up-axis remap** (`Config::up_axis`, default Z→Y, head up): exports stand upright; kills the "~90°" rotation.
* [x] **Manifest v2** (`0.3.0`): echoes the full effective settings + per-chunk centers + a `probe[]` block; now written for Base too. This is the "JSON of settings + output" idea — it carries counts/AABB/centers, not a duplicate of the vertices.
* [x] **Texture UX**: `.mtl` uses a white base + `map_d` (alpha cutouts); README documents the TGA→PNG / Blender workflow. (Extractor was already correct; macOS Quick Look just renders 32-bit TGA poorly.)

### Per-agent isolation — CALIBRATED & IMPLEMENTED (needs in-game multi-agent confirm)
*The real feature. GW skins in a vertex shader so all agents overlap at the origin; the isolation key is the agent world transform in the VS constant registers. GWToolbox's `GameWorldCompositor` confirms GW's layout: view c0–c3, projection c4–c7 → the world/bone transform is beyond c7.*
* [x] **GuildliteProbe seed** (`Config::probe_shader_constants`): dumps `c0..c95` of the first skinned draws + their centers to the manifest.
* [x] **Calibrated** (map 481 target probe, 2026-07-01): the skinned bone palette is at **c62..c94** as row-major `[3x3 | translation.w]` triples; the translation equals the agent's GWCA `(pos_x,pos_y,pos_z)` at **scale 1.0, axes identical** (root c92..c94 matched the target to **1.6 units**).
* [x] **Isolate** (`Config::isolate_by_bone`, default off): the hook scans the palette registers and keeps a skinned draw only if some bone-triple's translation is within `isolate_tolerance` (~250u) of the Source agent's GWCA position. Validated offline: matches all of the target's draws, rejects a decoy 600u away. Scans a range, not a fixed offset, so it survives shader changes.

## RECEIVE FEEDBACK ON BUILD AND APPLICATION STATE

* [~] **Confirm in a crowd**: capture in an outpost with `isolate_by_bone` on + Source=Target → should yield just the targeted agent. Combine with the extent filter to also drop non-skinned terrain.
 - Feedback received in `./FEEDBACK.md` (+ `./feedback-images`): several tune issues and UI/UX suggestions. Triaged in `./MODEL-EXPORT-SETTINGS.md`.
 - Confirmed live on map 251 (2026-07-02): isolation matched (`iso=17–22` dropped), so the map-481 bone-palette calibration generalizes. The `iso=` diagnostic (`draws_skipped_isolation`) makes it visible per capture (iso>0 matched; iso=0 → re-Probe).
   - **WARN**: This means you cannot export stationary character models too
* [x] **Assess performance**: feedback triaged; fixes shipped across this session. Isolation fixes: **Require skinned mesh** (`require_skinned`, blend-weight test — drops static props/buildings that sit *within* isolate tolerance, the "grabbed a nearby object" bug); **Drop flat draws** (`filter_min_thickness` — removes billboard/HUD quads when the world renders depth-test-off); the **`iso=`** counter; Center-radius decoupled from Trim (was silently dead when Trim was off); a refreshable **Diagnostics** panel (source pos/box, post-trim AABB+volume, dry-run Refresh); Probe made session-only; and rewritten in-panel help. Also diagnosed: some setups draw the 3D world depth-test-off, so Exclude-2D discards the world. Full write-up: `./MODEL-EXPORT-SETTINGS.md`.
 - Next, feedback-driven: (1) validate/auto-detect the bone-palette register window across maps (held on 251; confirm more); (2) skinned-vs-static discrimination — **done** (`require_skinned`); (3) **goal-based export picker** — one "What are you exporting?" mode (My character / One in a crowd / A building / Whole scene) that sets require_skinned/isolate/drop-flat under the hood, so users never reason about the filter matrix (MVP2, per feedback — "helpers for this-not-that-only-when-X"); (4) held-item handling (rigid weapons are XYZ, so require_skinned drops them); (5) multi-frame/angle merge toward "more of the scene."
* [~] **Ideate GuildliteProbe MVP**: We likely will finalize the first plugin with human eyes - the complex nature of the model exporter system surfaced a very real need for an image harness to verify builds with and the very real need to disable and reenable remote dlls. Both of these actions cannot be completed through SSH to interactive sessions - as such an internal plugin is proposed as Probe suite of tools within Guildlite, defined in `WHAT'S AFTER` below.
* [ ] **Receive feedback for publishing**: Receive feedback from recipients of beta dll to finalize the plugin internally, package as standalone `Model Export` tool and PR to GWToolbox. 

----

# WHAT'S AFTER

## Guildelite -> (GuildliteProbe, GuildliteControl, ...)
*Goal: Complete first model-export dll, distribute to users for feedback.*
*Goal: Publish first plugin*
*Goal: Generalize GWToolbox tools and dlls - network disable/enable of dlls in build.sh, tests on builds for expected results*
*Goal: Integrate GWLauncher into personal and Guildlite flows*

# ROADMAP IDEAS

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
