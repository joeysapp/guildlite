Read the following build requirements related to the completely-populated `./Gw.dat` file, the current ./ROADMAP.md build items. Propose a series of steps to extract and integrate all information and content into Guildlite respecting its creative intent. It seems prudent to first build our tooling to extract/ingest/navigate it systematically for export as the GuildWarsMapBrowser does (which would be used, but is not cross-platform.)

A threejs project is referenced later as an export target and can be reviewed in full if necessary.

## PREVIOUS BUILD
Begin building the described Model Editor in ./ROADMAP.md using/implementing all relevant build pieces around it. Treat the ROADMAP and any other items required as open items to implement/refine/fix in the first Editor.

# IN-GAME BUILD
Using the newly-referenced GWDatBrowser (and gMod for texture editing), build out the proposed browsers for instances of *_model_id uses in Guildlite, namely:
- Target model to transmog into within the Editor tool
- Target armor to transmog target armor into
 - Intelligently populate e.g. read target, only show available armors.
- Target weapon to transmog target equip into

Use GWDatBrowser and existing -image complete download to begin several components and browsers for the above NPC/character models and textures, armor models and textures, .. anything with models/textures.

Surface all DAT-focused, model-focused, texture-focused capabilities built here over the SSH controls; e.g. searching/getting of models and textures.

Interface with the Editor and Exporter tools completely - use the information found in the DAT meaningfully across Guildlite - ID->names, sorting, association, etc. 

# CROSS-PLATFORM BUILD
Using GWDatBrowser as a reference, extract all available content from Gw.dat into cross-platform structures for creative use beginning with character models.

The ideal first ingestor and reader of exported models and textures: `~/etc/git/topic-get`, a React SPA that uses threejs to render plain objects. Fully exporting/using models with bones/animations is the goal state today.

# INTENT
The exporter will eventually be perfected with and export models of any type for use in other creative tools (Blender, own 3D/2D renderer, etc.) Consider this `Gw.dat` functionality as a fundamental driver for Guildlite's creative intent. It is more important to use the models externally than in-game for reference and priority in this build.

# GW.DAT 
The complete `Gw.dat` has been copied to the root directory but not committed; the architectural and development team is welcome to place/move it as desired.

## Background
- There are many related/moving parts detailed in ROADMAP; review it and the project state to determine which items can be bundled into the MVP 1. Editor build. 
- The first tool built: **Exporter**
 - Most recent work has been picker/targetting export and beginning of pose/export stitching. Next is pose refinement.
- The second tool built: **Freecam**
 - Works. A number of refinements planned.
- The third tool built: **Editor**
- Third party tools: GWDatExporer and GuildWarsMapBrowser (Comprehensive Windows-only dat visual/exporter tool - ideally what we build)
- Ideal first-ingestor and reader of game objects: ~/etc/git/topic-get (React file browser using threejs)
- **[NEW]: Exploring the DAT and surfacing all information**
