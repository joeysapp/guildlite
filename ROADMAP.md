## INJECTION ROADMAP in PHASES
- [x] Injector: Init, ImGui base and info (overlay), screenshots via backbuffers (dev superpower over ssh)
- [x] Exporter: Port the 6 files, write the 4 glue modules, wire the control panel. Runs standalone. → `injector/src/{Capture,ObjWriter,TextureExport,GameState,GuildliteConfig}` + `{Game,Settings,Exporter,Overlay}`.
- [x] Dev Loop: stub + reloadable core + control file — iterate from the Mac over SSH with visual verification, no re-injection. → `guildlite-stub.dll` + `guildlite-core.dll`, control file at `Documents\guildlite\control`. **macOS Metal console shipped** (`gui/`, `./build.sh --macos`). See INJECTOR.md.
- [ ] Fun Features: Model extraction, character editor, free camera, in-game commands e.g. /chest /npcs, TexMod, shader editor .. macOS, shared-state, Prop Hunt, etc.

# BUILD

### [~] Advanced Model Extraction
*Textures, components, animation/skeleton info.*
- [x] Texture extractor to PNG/**TGA** → `TextureExport.{h,cpp}`, dependency-free: software DXT1/3/5 + `StretchRect` readback. Reads live bound textures, not the DAT.
- [x] UV coordinates + `.mtl` generation paired with extracted textures.
- [x] **ModelMod reference investigation** → `MODELMOD-FINDINGS.md`. Verdict: it uses our identical `DrawIndexedPrimitive` snapshot mechanism (no DAT path to leapfrog); the leverage is *select-a-draw vs filter-a-scene* + preserving the skinning substrate we were discarding.
- [x] **Skin-weight substrate** — capture + export GW's per-vertex bone indices (packed in a D3DCOLOR beta) as OBJ `#vbld` lines + manifest `weighted`. Verified: 30+ bones, real skeleton.
- [~] **Pick mode** — interactive select-one-draw with in-game highlight. 
- [x] **Missing armor/robe — ROOT-CAUSED + fixed (it was NEVER a depth-test issue).** GW draws the  dress/skirt and some armor as meshes it does **not flag as skinned**, so `require_skinned` dropped  them and pick "Skinned only" hid them — the long-standing "coherent character, missing armor"  symptom. Fix: `clean-full` no longer uses `require_skinned` (it size-gates scenery instead); "Skinned only" now warns it hides non-skinned pieces. Misleading depth-test "armor fix" claims removed.
- [~] **Non-skinned world-space re-seating** — those non-skinned pieces are baked into **world  space** (skinned meshes are bind-pose-local at the origin), so a raw grab scattered them ~7000u from the body and the export rendered as a speck. Now re-seated onto the body via `local = Rz(-facing)·(world - agent_pos)` from the GWCA snapshot (`Exporter::AlignWorldSpaceChunks`). Result: a **complete, correctly-oriented** character. Caveat below (#pose).
- [~] **Pose reconstruction (`pose_to_live`) — prototyped + baked in.** Closes the "close, not exact" gap: poses the bind body forward into the LIVE frame from the **bone palette** so GW's live-pose non-skinned armor lines up. **Palette layout solved + self-calibrating**: per-draw, bone `k` → VS registers `c(base+3k)` as `[3x3|t]`; `base` found by scanning for the bone whose translation == the agent's GWCA pos (`Exporter::PoseChunks`). Rigid single-bone: `world_v =
  BoneMatrix[bone]·bind_v`. Verified: reconstructs a coherent live-pose body. Remaining: confirm every skinned chunk poses once all draws are probed (`kProbeMaxSamples` now 64); See**Rig/pose** for the full-skeleton/animation extension.
- [~] **Whole-character grouping (`pick target`)** — built, but bone-palette-calibration-bound (0 on map 280); gated behind isolation self-calibration.
- [~] Armor/weapon systems: per-slot equipment, `model_file_id`s, dyes → manifest (identity only; per-slot **geometry** gating is DAT work / -image work).
- [~] Character state / skeleton / animation structures: model/animation **state ids** + equipment identity via GWCA → manifest. Per-vertex bone binding now exported (`#vbld`); true skeletal/frame export is future.

### [~] Rig/pose the captured character - exact assembly + animation (#pose)
*Goal: make the re-seated non-skinned pieces line up EXACTLY, and unlock posed/animated export.* The one gap left in the "one button → complete character" path: we export the skinned body in **bind pose** while GW's non-skinned pieces (dress/skirt/armor) are baked in the **live** pose, so the rigid re-seating (`AlignWorldSpaceChunks`) gets root + facing right but limb-level pose is "close, not exact". Two regimes seen 2026-07-06:
* **Root-attached single garment (dress/skirt)** — the rigid re-seat seats it well; symmetric, so a residual facing error is invisible. This is the "close" MVP working as intended.
* **Multi-bone limb armor (pauldrons/greaves)** — GW packs plates for *several bones* (both shoulders, arms, legs) into ONE non-skinned draw, each baked at its own limb's live position. A single rigid transform brings the chunk *near* the body but CANNOT seat each plate — confirmed: no whole-chunk rotation seats them. This one genuinely needs per-bone placement (below), not a rotation tweak. The raw material to close it is already captured:
* `#vbld` — per-vertex bone indices + weights (the mesh→bone binding), exported today.
* The bone **palette** (per-agent bone transforms) — dumpable to the manifest via Probe (VS constants). Remaining work is assembly: reconstruct the bone transforms from the palette, then either (a) **pose the bind-pose body forward** into the live frame so it matches the non-skinned pieces, or (b) build areal skeleton + skinning so the export can be re-posed/animated in a DCC tool. Nobody in the ModelMod reference got here; we're better positioned (GWCA gives the world transform). Cheap near-term step: document how to hand-rig `#vbld` + palette in Blender for a one-off posed render.
*Reference: Visual review of posing seems to be somewhat unreliable from AI confirmation - several false positives have been spotted. One character (male assassin) was stitched together perfectly, but other professions/genders have multiple broken connections saying that we may need a thorough checklist and manual review of results.*
*Consider: Extend the blender rendering tool with savable poses and animations such as male-assassin-dance-snapshot-N / npc-cast-animation-N, etc.*

### [~] Client-Side Free Camera
*Goal: Break the client renderer away from character/skeleton and fly the camera around the rendered map.*
*Fix: Needs ability to lock character / hotkeys to intercept character movements as camera control also controls character currently.*
*Fix: Needs ability to respect chatbox focus - typing in chatbox while in freecam mode moves the camera. Chat should take priority.*
*Improvements: FOV, Roll camera, orbit a target, record/dolly a path and movement that can be saved and replayed (with labels.)*

### [ ] Direct DAT Asset Reading
*Goal: Parse the DAT directly, so assets don't depend on active game state.*
* Likely requires running -image to download all assets prior.
* Port DAT parsing concepts from `FULL-ENGINE.md` (Phase 1/2) into C++.
* Basic asset loader (`guildlite-assets`) to locate/read `model_file_id` records from `Gw.dat`.
* Expand the exporter to output models extracted directly from the DAT.

### [ ] Model/Texture/Area/NPC/Character/../Any Browser & Search UI for Guildlite
*Goal: Interactive library of models/textures/etc. - anything in game that could be served through a list usefully, usable for multiple panels and planned tools.*
*Limitations: possibly bound by DAT/GWCA structure; MVP may be a 'seen objects' browser - unless -image resolves issue (likely will - add to guildlite controls?)*
* ImGui Model Browser with search; categorize DAT contents (Objects, Armor, Items, Animations, Models, Terrain); connect to the asset loader + exporter (search "Chaos Axe" -> export).
* Immediately usable in the Model Exporter Pick browser, .. Character Editor
* Text labels vs. wireframe/thumbnail preview; MVP likely searchable text WITH an empty placement image/wireframe.
* Animations/pose selector in Character Editor

### [~] Targeting Functionality & UI for Guildlite
*Goal: A reliable and expected UI/UX for features and tools that (can) rely on a "target" in-game, knowing:*
- GWToolbox provides a /target command, described in Chat Commands build item
- Model Exporter picker and target logic relies on targetting but there is no way for us to target in-game, therefore we have not been able to A/B test

### [~] Model Editor (MVP 1 built — client-side appearance editor)
*Goal: A new 'Editor' panel that allows users to edit their character's visual appearance comprehensively.*
**[~] MVP 1 SHIPPED** → `injector/src/{Editor,AppearanceApply,EditorConfig}` + the third Guildlite tool
`[Editor]` in the toolbar. Investigation of GWToolbox's **Armory** (`ArmoryWindow.cpp`) + **TransmoModule**
done: both mechanisms ported. The Editor **writes** appearance (mirror of the Exporter's read); all
game-memory/vtable/packet writes are marshalled onto GW's game thread (`GW::GameThread::Enqueue`), and
`AppearanceApply` is the one auditable place they live (the read/write split, like GameState/AppearanceApply).
- [x] **Transmog** — whole-model swap into any NPC via emulated `AgentModel`/`NpcGeneralStats`/`NPCModelFile`
  StoC packets (GWToolbox's proven path, NOT a raw `transmog_npc_id` write). NPC **picker list** from the
  client-global `GetNPCArray()` (id + model + profession per row).
- [x] **Scale** — emulated `AgentScale` packet (1..255%). Live 2026-07-07: scale ALONE doesn't visibly
  resize (GW only re-applies scale on a model reload), **but paired with a Transmog it works — CONFIRMED
  255% + transmog #285 rendered a giant.** So the reliable combo is transmog+scale; standalone scale is
  the field-set only (noted in-panel).
- [x] **Equipment + dye** — per-slot spoof of `equip->items[slot]` (model_file_id + dye) then the
  `EquipItem`/`RemoveItem` vtable to redraw. Dye is robust (recolour real gear); model swap reuses the
  slot's real item type (experimental); weapons/costumes are the known-fragile bits (planned).
- [x] **Profession / Sex** — direct `AgentLiving` field writes (experimental: many models don't re-skin
  live from these — the reliable look-change path is transmog/equipment; called out honestly in-panel).
- [x] **Apply / Revert** — a pre-edit snapshot per agent restores the true look; **Revert all**.
- [x] **Savable/loadable Character configs** with labels (persisted `editor.json` via glaze).
- [x] **Global states** — named looks bound to a **target set** (Self / Target / All players / All NPCs)
  with **enable + priority**; several enabled at once, applied low-priority-first so the highest wins a
  contested field ("All players as X", "Self as Y" compose). One-shot over agents present now.
- [x] **Shared targeting** — Source = Player/Target (mirrors the Exporter); every edit + state works over
  SSH via `edit ...` control verbs (apply/revert/transmog/scale/slot/save/load/states), so it is A/B-drivable
  from the Mac even without an in-game /target.
*Honest gaps (planned): edits are CLIENT-SIDE and reset on zone (no auto-reapply yet — re-Apply after a map
change); NPC names are still encoded (picker shows ids); weapon redraw + costume/festival-hat tables need the
signature-scanned game funcs; hair/face/skin-colour and true **geometry (transform matrices) / custom textures**
are DAT/-image-era work (see Direct DAT Asset Reading + TexMod) — not in MVP 1.*

### [~] Review Submodule References for Completed Work and Capabilities
*Goal: Use existing work in reference while building out Guildlite, submodules described selectively below as they are added/removed:*
- [x] ModelMod: Export/import models into games
- [x] GWToolbox: Uses GWCA for similar QOL capabilities and features
- [ ] TexMod: Loads in custom textures
- Projects by @ldufr
 - [ ] Headquarter: Unclear. Bot framework?
 - [ ] OpenTyria: Server for Gw.exe, run Guild Wars completely locally
 - [ ] nexus: Run multiple Guild Wars off a single Gw.exe (Improvement over GWLauncher?)

### [ ] Novel Rendering Features and Techniques
*Goal: Review Guildlite layer and any opportunities for novel/clever rendering features, ideas such as:*
- Warping of geometry (Linear -> fisheye, spherical geometry, animated e.g. Minecraft Acid Shaders, scaling targeted objects making personal character larger/smaller etc.)
- Visual shaders (Atmosphere, fog, additional effects)
- Importing custom objects

### [ ] Chat Commands - [Investigate GWToolbox Chat commands (/chest , /target , ...)]
*Goal: Mimic GWToolbox's chat /chest command (OpenXunlai..), determine its relevance in building a "safe" (aka not immediately bannable/detectable) command like it.*
*Background: GWToolbox provides the /chest command freely as a widely-used and installed tool with no consequence; can we build on this with our planned list of NPCs in outpost/zone feature?*
*Related: Clickable outpost NPC list -> dialog/UI, extending Xunlai to e.g. merchants/rune traders/dye traders. Unclear if Xunlai is only position-independent NPC interaction.*
*UI: All non-tool (e.g. in-game, non-Guildlite) commands must be documented in the controls/command UI section*
*Related: /target [name] allowing programmatic targetting of NPCs, players and even items on the ground. Provided by GWToolbox except items.*
_**IMPORTANT**: The more GWCA/in-game commands we surface in chat commands - we also gain access to via network controls, e.g. we have no way to target self currently. A /target command would allow us to target specific things over ssh for A/B dev._
_**IMPORTANT**: Review GWToolbox commands, propose/implement/reference legitimately useful commands but do not wholesale implement all - GWTB is known for cruft and features without a functional use. Future goals such as template/team loading can be investigated in due time._
_**IMPORTANT**: All commands must be documented in `Info` panel._

### [ ] Investigate TexMod Integration
*Goal: Load in existing texture modifications.*
*Concession: In looking at existing code, determine if worth integrating vs. simplifying/optimizing ourselves and providing it as a simple tool, e.g. 'Textures' panel with load-in tools, options and improved functionality TexMod does not offer.*

### [ ] Review Practicality of macOS - Gw.exe
*Goal: Document practicality and options of reliably running Guild Wars on macOS with the lowest amount of dependencies/extra installs.*
*Draft Prior Knowledge: Wine/Rosetta/VMWare emulation, Parallels/macOS/Other dual booting*
*Draft Ideas: Lightweight, native running of Gw.exe VS. Entire rewrite of game engine*
 - Question: Can we lift *some* of the DX9 out to Metal/other and write the parts we can't? What is the MVP here?
 - Question: In getting an MVP - look at all the bot/external work. There are headless clients running bot farms (known of), surely a macOS client is doable

## [~] Guildlite
*Goal: Continually review and improve core functionality for development and usage, such as:*
 - Fundamental Ease of Use
 - Understandability of Features
 - Core Reliability and User Experiences
 - Network Control Reliability
*Multi-Gw.exe targetting to drive and reload independent clients*
*UI: Surface all tools and options with saved/loadable state*
 - [x] Guildlite toolbar toggleable all tools
 - [x] Unify Controls+Overlay panels — done: `Controls` → **`Info`** (`injector/src/Info.{h,cpp}`), which now
   leads with the old Overlay **Status** (health + screenshot/demo/unload actions), then a **Commands** crib,
   then the controls reference. Standalone Overlay window removed. Toolbar reordered to
   **[Editor] [Exporter] [Freecam] [Info]** (+ Demo for dev).

---

## FUTURE IDEAS

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
</content>
