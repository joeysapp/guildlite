A RuneLite-esque project was ideated earlier this year looking at GWToolbox (similar software, only a handful of plugins currently.) It was scoped to be a cross-platform, open-source plugin-heavy client for the MMORPG Guild Wars. It was deemed as too difficult for a single developer at the time but a proposed plugin for GWToolbox itself has been ideated and seems both more doable and filling a hole that no other software is filling - model/texture extraction and controls provided as a GWToolbox plugin.

## Suggested Relevant Notes
The below are items planned in the FULL-ENGINE.md plan that stand out as areas where we'll be building. That is, unless GWTB surfaces the majority of what we need at this point in time. It is doubtful as I spoke with a main developer and he said this was a large task. I did find that GWToolbox has a 'armor' plugin that lets you swap out your (own profession) armor to others and set your own client weapons (all client side) but nothing crazy yet like change profession/gender/height/model_id or animation/state. It's definitely in there!
- High-level concepts found in 2.3 Directory Structure (L150:228)
 - guildlite-core - Core engine - Likely handled by GWTB, getting entity model_id/armor id, etc. is likely surfaced already
 - guildlite-renderer - Graphics engine - Exporting objects (flat/rendered/uv mapped) and selecting rendered items to export/inspect. May be unnecessary - e.g. exporting the DAT content and state with a pipeline output to render flat->painted objects is easier
 - guildlite-assets - Asset loading - DAT file traversal, models, textures
 - tools/{dat-explorer,asset-extractor} - Use to catalog and interact with models, textures and effects without literal in-game access
- DAT file format research (L286:303)
- Asset Loading Pipeline (L425:450ish) and Rendering pipeline (L611:
 - In-game library of models to export, possible thumbnail generation or name search
 - Comprehensive item/armor loadins and outputs
 - All objects, terrain
 - Possible to use already built rendering to change own character model/animation/skeleton/armor/weapon/state, have an 'export' button act more like a snapshot that writes to object and uv/color pair
Most of the rest of the guildlite mass plan doesn't seem relevant but feel free to look if needed.

## Proposed Roadmap
- Use FULL-ENGINE.md points and GWTB/GWCA to set out a minimap roadmap and action items and builds to hit
- Build and run GWTB plugins locally, reliably and easily (currently on macOS, W10 computer running GWToolbox but no real network build pipeline as of yet - for future)
- Prototype actionable plugin interface to do A/B tests with, re-evaluate state and plan
- DAT research, state of the game (e.g. can we just snapshot models and write out objs from the active renderer?)
- Plain models, no coloring necessary - basics like object faces pointing the correct direction written to standard obj/stl file
- .. Plugin interface with:
 - .. Model browser with search/plugins
 - .. Model browser usable and applicable
- .. Objects, armor, items, animations, models, terrain
- .. Textures, uv maps, effects/lighting
- .. Exporting/copying other players and models client side
- .. Compositing scenes client side (placement of models in game world for content creation)
- .. Shared plugin client state (see other friend set as another model_id)
- .. Random model_id setter (prop_hunt game)

## References
- ./GWToolboxpp - Submodule of GWTB with active development and GWCA references
- ./creators-kit - Model tool for RuneLite (Java, some of above features mentioned)
