Review ModelMod submodule for relevant information and potential breakthroughs for current model-export work (reliable, clean, target exports in a single snapshot button) [NOTE: Submodule checked out to main with DX11 code - seems to be 1.2.x is DX11 alpha, 1.1.x is 64bit support, 1.0.x is base, may need to search GH release notes for last known DX9 build/best release to use in reference https://github.com/jmquigs/ModelMod/releases?page=1]

# Background
Last state of model exporting (first tooling provided by the Guildlite dll) was using A/B analysis and review of settings (./renders/MODEL-EXPORT-SETTINGS.md) There has been progress made and a classified and manually-confirmed models+renders+settings is in progress at ./RENDER-FEEDBACK (classifying output from `dev-tool` runs, an AI-driven visual inspection tool intended for general usage in Guildlite development.)

Before `tools/dev-tool` work is continued/refined/flattened it seems prudent to consider the ModelMod DX9 prior work for relevant help. It may be tertiary information for us - is there a chance we've gone about exporting wrong with vertex-shader approach instead of DAT exporing/otherwise? The model-exporter is certainly robust and promising (actual rendering) but if we can do exporting easier, consider that as a leapfrog. Consider that the "solo export" current goal could be done simply, then we pivot our renderer to be game-world/full renderer and export tool (all under the same hood.) Investigate.

# Initial Thoughts
Cursory glance-over of several readmes:
- Almost every markdown document has random newline breaks throughout. Looks like comments from a 80char maximum line length dev. Suggest to sanitize markdowns first if a possible issue, there is none of that here ever, there is a lot in this repo to go over
- Dev notes make note of Rust-based global memory usage for a lot of the logic - unclear if this is a blocker for us as well
- Note of software not working if a D3D9 renderer is used. Unclear if a blocker
- Dev notes have interesting info about verts/skeleton rigging - as background for Guildlite, initial intent was a cross-platform renderer for GW - deemed low reward s.t. we are now completely reliant on D9 and the Windows client (fine as of now, just an inkling of "hmm.." for interest.)
 - **Importing** models is assuredly an interesting proposition (Prop Hunt, games, fun) but exporting and getting our exporting logic perfected is the priority. Things like **shader hooks** and novel player-facing config is more intriuging (perfect example: Minecraft Acid Shaders 2011)
- The vertices discussion is very intriuging and sounds like a starting point for animations/skeleton capability (devguide MeshRelation note)
- The snapshot explanation in the developer guide is very informative for our current state and specific struggles (placement and selection of things, prior attempts like iso=)
 - The screenshot and AI/human verification pipeline built for this capability is now more interesting reading the difficulties injection-based coding inherently has been driven. Curious to see if any past blockers/thought-impossible/too-much-time features could now be worked towards with the feature-agnostic loop chain we have built/been building.
- **Question**: If / when model isolation is achieved, how granular are these models and meshes and geometries? Could we export an entire character as-is + their separated armor + weapon models at once? 
- Note of the .mm... proprietary formats. Realizing we are ourselves doing some of this - I would imagine our best path for just _exporting_ would be human-readable for animation/node/skeleton information but - first foray into animation and how getting this information from live memory is "not simple (but probably doable.)"
-# p.s. .. BatGuano? a joke? (unimportant question, just an eyebrow raiser)

**Background: Returned home, have Windows machine w/ Gw.exe. May need refresher/cleanup of tools to get build injected for output information / let us watch it for this build**

# Project Relevant Documents
./README.md, ROADMAP.md, INJECTOR.md - Current state of beginning lightweight injector using GWCA and network protocols for macOS -> Windows dev

# After Models - Priorities
1. Refine UI (Gw.exe and macOS build) - Basic bottom-left (~120px padded left to not block Gw menu) toggle menu to open and close all windows
  - NOTE: Bug in Gw.exe where mouse captures are not perfect - can embellish in following build doc.
2. Freecam tool
