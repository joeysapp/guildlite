# Guildlite - Roadmap
## Checklist for All Builds
- [ ] **Continually review and improve core functionality and UX**
 - Fundamental Ease of Use
 - Understandability of Features
 - Core Reliability and User Experiences
 - Network Control Reliability
- [ ] **All tools and options set and read a global state***
- [ ] **All tools accessible via toolbar**
- [ ] **All builds verified via `control` of running Guildlites**

# Fundamental
### [~] `datcore` Interface in both CLI and  Guildlite
*Goal #1: Parse the DAT directly, so assets don't depend on active game state.*
*Goal #2: A cross-platform DAT browser for export/search (`datcli`) and in-game tooling/fun*
*Proposed: A reusable DAT Imgui element to navigate through DAT information, allowing for things like:*
 - Browse for weapons, armors (Usable in `Editor` in the Equipment)
 - Browse for NPCs, objects (Usable in `Editor` in the Transmog)
 - Interact with local labels.json, identify models in-game and write out labels and notes for capture over SSH
 - **TODO**: Research practicality of loading in target model_ids and iteratively pulling out data from a live client - e.g. rendering posed/bound models in game

# Tools
### [~] (HIGH) `Extractor`
*Export model textures, components, animation and skeleton information with in-game poses and vertex positions exporteed.*
- [x] Pose reconstruction — poses the bind body forward into the LIVE frame from the bone palette
- [~] Character bindable skeleton / model / textures / skin composites, armor, weapon, hair composites
 - **MASSIVE PROGRESS MADE.** All character exports, if manually isolated/picked, export as expected.
 - Works as snapshot for some of the above, needs `gltf / glb` work to unify exported content
 - Export complete rig/pose the captured character - exact assembly + animation (#pose)
- [ ] (MEDIUM) Improve picker list of textures:
 - Ensure cross-character, app-wide disabling is reliable (having to often re-disable some textures, not sure if duplicates or not)
 - Prevent list order from jumping constantly while at bottom of list while trying to disable/select. Have a timing buffer/gray-out "unseen-leaving-list" entries perhaps, it moves quite often currently
- [ ] Any use for -lodfull flag for Gw.exe:
> "Instructs the client to use the highest level of "LOD" (level of detail). 3D assets will be rendered instead of 2D "imposters". There can still be some "popping" as some art assets move into and out of the edges of the view or a bump in the terrain." 
- [ ] (HIGH) Handle transparent textures on export - unclear why some models have nearly-invisible pieces of armor (Could be missing solid pieces / multiple submeshes? Are we surfacing and exporting all subtextures properly in the picker interface?)
- [ ] (MEDIUM) Export models as a `gltf/glb` for animation and scene information to be consumed by Blender and other projects (`topic-get`, threejs)
- [ ] (HIGH BUT COMPLEX) Other character/model picking and exporting
  — **Needs work.** Must stand own character against a wall (or no other textures in view) to export, but works reliably.
  - Unclear how best to do this - e.g. isolation render or bone proximity, etc.
  - Improving the picker UX/UI e.g. hovering over a character highlighting entries in the picker list (and not jumping around) would be a massive win and would count as a build 
- [ ] (MEDIUM) Complete texture handling, in-game systems handling 
 - Armor/weapon/skin-color/hair-color composition

### [~] (MEDIUM) `Editor`
Goal: A new 'Editor' panel that allows users to edit their character's visual appearance comprehensively.
- [x] Transmog — whole-model swap into any NPC.
- [x] Scale — emulated `AgentScale` packet (1..255%). Often requires two transformers.
- [x] Equipment + dye — per-slot spoof of `equip->items[slot]` (model_file_id + dye) then the  `EquipItem`/`RemoveItem` vtable to redraw. Dye is robust (recolour real gear)
- [ ] (HIGH) Model browser, searcher, picker as described by `datcore` Imgui interface
 - Likely imports and uses datcore?
 - Populated from the client's live DAT, not just in-memory information
 - Global `GetNPCArray()` / GetObjects / etc.
- [ ] **Non-functional**: Profession / Sex — direct `AgentLiving` field writes. Investigate uses for this / ways to trick Gw into repainting models. 
 - IDEA: What about loading in dummy models that we can target? That would still be useful.
- [x] UI/UX: Apply/revert, save/load, global states, targetting works in-game and over SSH

### [~] (LOW) `Freecam`
*Goal: Break the client renderer away from character/skeleton and fly the camera around the rendered map.*
*- [ ] Fix: Needs ability to lock character / hotkeys to intercept character movements as camera control also controls character currently.*
*- [ ] Fix: Needs ability to respect chatbox focus - typing in chatbox while in freecam mode moves the camera. Chat should take priority.*
*- [ ] Todo: FOV, Roll camera, orbit a target, record/dolly a path and movement that can be saved and replayed (with labels.)*

---

# In-Game Extensions / Surfaces
### [ ] (HIGH) Commands (in-game chat, Terminal/SSH controls to Guildlite/control)
*Goal: Provide the following list of MVP commands in-game and over SSH. Catalog thoroughly in both CLI tool help/docs AND in-game `Info` panel:*
 - (HIGH) /chest - Opens interactive Xunlai chest in town/outpost, opens saved Xunlai state to view but not edit anywhere else. GWToolboxpp proivdes via OpenXunlaiChest, improve and build intelligently e.g. in Guildlite settings/state for cross-character or account inventory features in the future
 - (?) Why do we not already surface commands to the in-game client?
 - (!) Investigate commands to use and extend: https://wiki.guildwars.com/wiki/{Special_command,Emote,Command_line_arguments}

### [ ] (MEDIUM) Investigate TexMod/gMod Integration
*Goal: Load in existing texture modifications (Cartography) and ideate future creative capabilities, e.g. actively editing textures programatically, upscaling textures with AI, creating variants and mash-ups.*
*Reference: gMod is the modern ancestor to Texmod. The end goal for Guildlite is to be as lightweight/independent of external tooling as possible - reference implementation but look to improve and reduce mem/cruft.*

### [ ] (MEDIUM) Investigate and Catalog In-Game Surfaces Thoroughly
*Goal: Look at what the game provides wholesale and look where it can be meaningfully improved/extended; we can likely save time using prior work and vanilla features..*
*References: https://wiki.guildwars.com/wiki/{Hero_flag,Dye#Usage,Movement_controls,Hair,_and_Indeed,_Everything_Stylist,Reconnect_after_disconnect}*
- e.g. Click-to-navigate on World Map (within zone / across zones), Optimized hero flagging during fights and pre/post encounters, Movement controls and all keyboard/mouse controls surfaced by Guildlite in-game and over SSH

### [ ] (LOW) Rendering
*Goal: Review Guildlite layer and any opportunities for novel/clever rendering features, ideas such as:*
- Warping of geometry (Linear -> fisheye, spherical geometry, animated e.g. Minecraft Acid Shaders, scaling targeted objects making personal character larger/smaller etc.)
- Visual shaders (Atmosphere, fog, additional effects)
- Importing custom objects

---

# Research
### [~] Investigate Submodule References for Completed Work and Capabilities
*Goal: Use existing work in reference while building out Guildlite, submodules described selectively below as they are added/removed:*
- [ ] TexMod/gMod: Loads in custom textures
- [ ] Projects by @ldufr
 - [ ] OpenTyria: Server for Gw.exe, run Guild Wars completely locally. Interesting!
 - [ ] nexus: Run multiple Guild Wars off a single Gw.exe (Improvement over GWLauncher?) Interesting! 
 - [ ] Headquarter: Unclear. Bot framework? Low interest I suppose but I do thing automating some things is legal
- [x] GuildWarsMapBrowser (Modern usage of GWDatBrowser) 
- [x] ModelMod: Export/import models into games
- [x] GWToolbox: Uses GWCA for similar QOL capabilities and features 

### [ ] Investigate Practicality of macOS - Gw.exe
*Goal: Document practicality and options of reliably running Guild Wars on macOS with the lowest amount of dependencies/extra installs.*
*Draft Prior Knowledge: Wine/Rosetta/VMWare emulation, Parallels/macOS/Other dual booting*
*Draft Ideas: Lightweight, native running of Gw.exe VS. Entire rewrite of game engine*
 - Question: Can we lift *some* of the DX9 out to Metal/other and write the parts we can't? What is the MVP here?
 - Question: In getting an MVP - look at all the bot/external work. There are headless clients running bot farms (known of), surely a macOS client is doable

---

# Future Ideas
### Multiple Guild Wars / Guildlites Running and Controllable
*Goal: Use a single Gw.exe (ref: third_party/nexus) for multiple clients and injected clients, all targetable and controllable over SSH as usual*

### Shared State & Mini-Games
*Goal: Network plugin state for collaborative/fun features.*
* Shared-state protocol (lightweight external WebSocket, or encode state in hidden party chat — no private server). Random `model_id` setter. Combine → "Prop Hunt" (users see each other's disguised forms).

### Client-Side Scene Compositing
*Goal: Edit and add to the client's game world for content creation.*
*Inspiration: RuneLite's [Creator Kit](https://github.com/ScreteMonge/creators-kit) 'Anvil' compositor — creative in-game compositions with animations, actions, replayability for content creators.*
* Client-side API to copy the `model_id` and appearance of targeted players/NPCs.
* Change the player's own `model_id`, skeleton, and animation state client-side.
* "Compositing Mode": spawn fake, client-side-only models/NPCs at coordinates (place models in the world). *(ModelMod's `MeshRelation` — weight a new mesh to the live skeleton without having the skeleton — is the reference trick here.)*

### Future Research
* The new iOS/iPad Guild Wars app — possible entry points (e.g. injecting into the app).
