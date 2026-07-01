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

---

# BUILD

### Basic Model Extraction
*Goal: Hook into the game state for plain, uncolored 3D models.*

* [ ] Investigate GWCA for hooks into the rendering pipeline or memory state to read actively rendered geometry (vertices, faces).
* [ ] Write a basic data exporter that takes active geometric data and writes it to a standard `.obj` or `.stl` file.

### Advanced Model Extraction
*Goal: Complete the first model extraction feature with textures, components, animation and any model/skeleton information.*

* [ ] Research the DAT texture format (likely DDS variants) and implement a texture extractor to convert them to standard formats (PNG/TGA).
* [ ] Research the character state, skeleton, animation structures.
* [ ] Update the `.obj` exporter to include UV coordinate mappings (`.mtl` generation) paired with extracted textures.
* [ ] Map the armor and weapon systems. GWTB already has an "armor" plugin; analyze its source to understand how `model_id` and equipment slots interact, allowing for exporting fully assembled character configurations.
 - Hook into armor plugin behavior allowing for users to export other armors and weapons
 - Investigate client-side limitations; is a model_id swap here as simple as the above armor plugin? Believed to be _no_ as gated by DAT client knowledge. *HOWEVER*, looking at NEXT items, could a client-side DAT library be used for editing your own model once you've seen said model? The assumption being the armor/weapon DAT information has been well-researched, but not e.g. every combination of character/profession/gender/skin-color/hair, etc. 

### UI/UX - Control and Setings
*Goal: Provide a comprehensive control/settings panel with upcoming NEXT/ROADMAP entries planned for.*

* [ ] Replace the Snapshot button with a first-class floating control panel.
* [ ] Build an interface for the model extraction of current player OR selected object using above sprints:
  - Base: Target name/id, export path (? default to GWToolbox/COMPUTERNAME/guildlite/)
  - Detail: No textures, textures, ? UV/effect/world-relative painted models
  - Armor: Armor, ? no armor, ? only armor (? separate pieces ?)
  - Weapons: No items, items, only items (? separate files)
  - Animation: No animation, animation state at click, animation state from scalar number set by user, (? Future capacity to export frames or model animation data?)
  - Export button

----

# ROADMAP AND PROPOSED PLUGINS

### Client-Side Free Camera
*Goal: Break client renderer away from character / skeleton and allow camera flying around rendered map.*
*Concerns: Unclear - likely not a concern but being careful and checking is worth the effort. Have purchased a second test account for both personal safety and future character<->character testing and tooling.*

### Client-Side Scene Compositing
*Goal: Edit and add to the client's game world for content creation.*
*Inspiration: The [Creator Kit](https://github.com/ScreteMonge/creators-kit) for RuneLite (and several other tools like it) are used to great effect by the content creator community. Read through how its 'Anvil' compositor allows for creative in-game compositions with animations, actions and replayability as key features for content creators.*

* [ ] Implement a client-side API to copy the `model_id` and appearance of targeted players/NPCs.
* [ ] Build functionality to change the player's own `model_id`, skeleton, and animation state client-side.
* [ ] Develop a "Compositing Mode": The ability to spawn fake, client-side-only models or NPCs at specific coordinates (placing models in the world).

### Direct DAT Asset Loading
*Goal: Implement direct DAT file parsing to access assets without relying solely on active memory.*

* [ ] Port the DAT parsing concepts from `FULL-ENGINE.md` (Phase 1/2) into C++ within the plugin.
* [ ] Build a basic asset loader (`guildlite-assets` equivalent) capable of locating and reading `model_file_id` records directly from `Gw.dat`.
* [ ] Expand the basic exporter to output models extracted directly from the DAT file, independent of the active game state.

### Model Browser & Search UI
*Goal: Create an interactive library of all game models.*
*Limitations: Potentially bound by DAT/GWA structure. May act more as a 'seen objects' browser for MVP.*

* [ ] Build an ImGui-based Model Browser with search capabilities.
* [ ] Categorize the browsable DAT contents: Objects, Armor, Items, Animations, Models, and Terrain.
* [ ] Connect the UI to the asset loader and exporter so a user can search for a model (e.g., "Chaos Axe") and export it immediately.
* [ ] (Stretch) Implement basic wireframe preview or thumbnail generation within the ImGui window.

### Shared State & Mini-Games
*Goal: Network the plugin state between users for collaborative/fun features.*

* [ ] Implement a shared state protocol. Since we are not building a private server, explore using a lightweight external WebSocket server or encoding data in hidden party chat messages to share plugin state among friends.
* [ ] Develop the "Random `model_id` setter" feature.
* [ ] Combine shared state and random model IDs to build the "Prop Hunt" mini-game mode, where plugin users can see each other's disguised forms.

### Future Research
* [ ] Research Item: Investigating the new iOS/iPad app around Guild Wars for any possible entry points, e.g. injecting GWToolbox itself into the app.
