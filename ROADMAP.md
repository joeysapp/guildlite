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
- State of the Game Check: Before deep-diving into DAT parsing, the prior sprint serves as a validation check to see if intercepting the active renderer is easier than DAT decryption

### Sprint 1: Development Environment & Foundation
*Goal: Establish a reliable, repeatable local development pipeline for GWToolbox plugins.*

* [x] Action Item: Clone the `GWToolboxpp` submodule and configure the C++ build environment (Visual Studio on Windows, or evaluate cross-compilation if developing on macOS).
* [x] Action Item: Establish a deployment pipeline to compile and hot-load (or easily inject) the plugin into a running GW client for rapid iteration.
* [x] Action Item: Build a "Hello World" plugin with a basic ImGui window to validate the pipeline.
* [x] Action Item: Prototype a basic actionable interface (e.g., a button that prints the player's current `model_id` and position to the console using GWCA) to serve as an A/B test baseline.

### Sprint 2: Windows, macOS and Linux Interactivity
*Goal: Compose a macOS-to-Windows-and-back set of tools for use in compiling, triaging errors and future test verifications dev to prod.*

* [x] Action Item: Install Visual Studio Community 2026, Desktop Development for C++, reliably get base GWToolbox built and running.
* [x] Action Item: Strengthen Windows remote accessibility. Nearly inaccessible at times, especially in PowerShell itself.
* [x] Action Item: A comprehensive build script that functionally builds a working plugin, that can be dropped into any given GWToolbox/plugin directory. 

### Sprint 3: Basic Model Extraction (The "Snapshot")
*Goal: Hook into the game state and extract plain, uncolored 3D models.*

* [ ] Action Item: Investigate GWCA for hooks into the rendering pipeline or memory state to read actively rendered geometry (vertices, faces).
* [ ] Action Item: Write a basic data exporter that takes active geometric data and writes it to a standard `.obj` or `.stl` file (ensuring normals and faces point in the correct direction).
* [ ] Action Item: Implement the "Snapshot" button in the plugin UI to capture and export the user's active character model.

### Sprint 4: DAT Research & Asset Loading
*Goal: Implement direct DAT file parsing to access assets without relying solely on active memory.*

* [ ] Action Item: Port the DAT parsing concepts from `FULL-ENGINE.md` (Phase 1/2) into C++ within the plugin.
* [ ] Action Item: Build a basic asset loader (`guildlite-assets` equivalent) capable of locating and reading `model_file_id` records directly from `Gw.dat`.
* [ ] Action Item: Expand the basic exporter to output models extracted directly from the DAT file, independent of the active game state.

### Sprint 5: Model Browser & Search UI
*Goal: Create an interactive library of all game models.*

* [ ] Action Item: Build an ImGui-based Model Browser with search capabilities.
* [ ] Action Item: Categorize the browsable DAT contents: Objects, Armor, Items, Animations, Models, and Terrain.
* [ ] Action Item: Connect the UI to the asset loader and exporter so a user can search for a model (e.g., "Chaos Axe") and export it immediately.
* [ ] Action Item: (Stretch) Implement basic wireframe preview or thumbnail generation within the ImGui window.

### Sprint 6: Advanced Extraction (Textures, UVs & Armor)
*Goal: Complete the extraction pipeline with textures and modular components.*

* [ ] Action Item: Research the DAT texture format (likely DDS variants) and implement a texture extractor to convert them to standard formats (PNG/TGA).
* [ ] Action Item: Update the `.obj` exporter to include UV coordinate mappings (`.mtl` generation) paired with extracted textures.
* [ ] Action Item: Map the armor and weapon systems. GWTB already has an "armor" plugin; analyze its source to understand how `model_id` and equipment slots interact, allowing for exporting fully assembled character configurations.

### Sprint 7: Client-Side Scene Compositing
*Goal: Manipulate the game world for content creation.*

* [ ] Action Item: Implement a client-side API to copy the `model_id` and appearance of targeted players/NPCs.
* [ ] Action Item: Build functionality to change the player's own `model_id`, skeleton, and animation state client-side.
* [ ] Action Item: Develop a "Compositing Mode": The ability to spawn fake, client-side-only models or NPCs at specific coordinates (placing models in the world).

### Sprint 8: Shared State & Mini-Games
*Goal: Network the plugin state between users for collaborative/fun features.*

* [ ] Action Item: Implement a shared state protocol. Since we are not building a private server, explore using a lightweight external WebSocket server or encoding data in hidden party chat messages to share plugin state among friends.
* [ ] Action Item: Develop the "Random `model_id` setter" feature.
* [ ] Action Item: Combine shared state and random model IDs to build the "Prop Hunt" mini-game mode, where plugin users can see each other's disguised forms.

### Deferred / Future
* [ ] Research Item: Investigating the new iOS/iPad app around Guild Wars for any possible entry points, e.g. injecting GWToolbox itself into the app.
