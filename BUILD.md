Read the current ./ROADMAP, architect and build the three components for a shippable, lightweight plugin to export content from Guild Wars:
1. Base Model Snapshot
2. Advanced Model Snapshot
3. Complete Settings and Controls for above

# References
- GWToolboxpp subrepo for GWA/knowledge of character state
 - Armor and weapons already provided - models likely behind DAT but a welcome challenge
 - Animation/skeleton/state export also seem likely
- Reliable and active connection to a running Windows GW.exe and Guildlite plugin is available at this point
 - An active harness/shim for reading game state over SSH for planning and verification could be useful
 - `ssh bob@bobmobile` and recent POSIX harness for deploying builds exist; consider heavily logging and auditing of in-game information with this information-rich surface.
 - Consider harnesses for this and future use to GWToolbox to e.g. trigger character state changes for DAT/audit inspection, movement and zone traversals
- GWToolbox Active Tooling through networked running GW.exe (`ssh bob@bobmobile`)
 - https://www.gwtoolbox.com/docs/info/ Game state info
 - https://www.gwtoolbox.com/docs/armory_window/ Object/mesh browser (e.g. `/armory "Obsidian Attire" 3 3 12 12`)
 - https://www.gwtoolbox.com/docs/target_widgets/ Info window on target (exporting selection)

Catalogued below are priority items and relevant context around current and future state of project with a handful of potentially-grand concepts; it is becoming clear GWT itself while powerful may not be utilized/recognized yet and a number of highly-useful tools could be built, not just a model exporter.

**IMPORTANT**
Frame the `guildlite` project to be an entry, high-level inspection of Guild Wars and GWToolbox. The model export plugin is serving as an effective, low cost and creative route to begin with. Treat all aspects of our research and designs as knowledge and tooling for future ideas within Guild Wars.
  A structure closer to the following projects could prove highly effective and powerful:
  1. guildlite - Personal analysis tool for GW and GWTB. Used for prototyping and PoC. May be made private to prevent abuse from public bad actors.
  2. GWToolbox - Model Exporter - Highly-customizable object exporter for use in external tools (this)
  3. GWToolbox - Scene Compositor - Tool for content creators to create and replay scenes from a vast array of models (planned to be within this, but seems meaningfully separatable while using prior knowledge gained here)
  3. GWToolbox - Freecam - Client view of entire rendered area visible to the client's renderer (Interest in determining ToS/game rules; nothing seems to indicate this could be abused)
  4. GWToolbox - Prop Hunt - Party game (Users randomly assigned models, uses prior knowledge, also interest in determining ToS in network hijacking or external network used to add game flavor)
  5. GWToolbox - DAT Tool - Seems to be potentially against ToS, unclear why/how. It may be prudent to do this work in private/off GitHub while learning GWTB/Gw.exe/our new direct networked-Windows interface to game state and information. 

**IMPORTANT**: You will likely encounter multiple roadblocks with POSIX tools/* and build.sh; know that physical diagnosis is available as well as manual verification.

**IMPORTANT**: Additions to guildlite tools/ such as: POSIX screencaptures of client window or area for verification and audit against content

**IMPORTANT**: Additions to system tools such as: Render an .OBJ to a .PNG
 - (Proposed) Highly valuable for current build. Strong recommendation to encorporate in build and stepwise verification of otherwise human-required validation of future and current plugin build results.
 - Beginning flows such as: [render char] -> AI: "img shows sword" ->  `gwtb /armory AXE` -> [render char] -> AI: "img shows axe"
 - **ASK**: Unclear if a harness (networkable or not) to GWT has been made yet. May fall in realm of abusable tools that call for `guildlite` to be made private as handful of first plugins are ideated and perfected.

**NOTE**: I would bet there has not been much Guild Wars tooling due to its compiled nature; the injection and auditable surface we have (only just built - likely will fault and need fixing) could very much be a fun, low-risk, high-visibility project space. There are 1-3? GWToolbox Plugins and two of them were separate tools made in 2006 (gMod, uMod - not "plugins" or "widgets", "modules".)
