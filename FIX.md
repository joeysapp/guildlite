Examine the following exported objects, their descriptions, their rendered output and their informational files. Review the Exporter tool thoroughly and propose a surgical fix for the mislaignments described below. Review how the Exporter tool's Picker snapshot fully (almost completely, fixing here) respects animation and animation state / rendering pipeline to export selected textures at time-of-snap, translating items from origin / place-of-snap to a 0,0 exported model.

Remedy how all characters are not identical and have a number of edge-cases listed below. Each item has files `obj, mtl, png` and N number of `_texN.tga` outputs. All items below can be found within the `./render-analysis` directory. "Resting position" for textures means unbound/not matched with correct output animation/render state.

# Skirt Mis-Aligned from Paragon Character
The following three model exports (via Exporter picker and manually selecting player textures) show the player's armored skirt not being bound correctly to the skinned character. The upper-half,  looks nearly perfect. The output of all three can be seen at `player-skirt-misaligned.png`, each shot from default 0,0 preview 
## 1. target_pn68_map248_20260707-074622
*Description: Player is standing still, their skirt armor and boots are not tied to the output and are floating, rotated ~30deg away from character.*
## 2. target_pn68_map248_20260707-074714 (obj,mtl,png,..texN)
*Description: Player is running sideways, their skirt armor and boots are not tied to the model/skinned output and are floating in its resting state.*
## 3. target_pn68_map248_20260707-074650
*Description: Player is kneeling on ground, their skirt armor and boots still not tied, are floating mid-air.*

# Player Hands and Lower Half Misaligned from Ritualist Character
## 1. target_pn60_map248_20260707-085122
*Description: The model (via the same as above) shows stretching of the player skin (arm position in pose to hands unskinned/bound and in resting position.) The lower half of the character appears the same issue as first paragon issue - player robes and shoes/feet are also in resting position, not rotated and in animation/active render state as the upper torso, upper arms and player head are. Two views of this render can be seen at player-robes-hands-misaligned.png

# Background
- In referencing the new ./datcore shared core DAT explorer, providing mft/hash information from in-game to log files and output would make things easier to debug and triage. Variables like dye values, skin color values, hair color values, height values, etc. that are discernable would also be of use.
- Improving `Exporter` and/or using it to help in building out both `datcore` and the `Editor` tool in small ways to unify their shared informational surface - exposing labels.json / exporting Gw.dat related information, etc.
 - **BIG**: Improving the Picker export tool UI/UX - currently you must find a corner with nothing else and manually find your player skins, individually select them to export your player character. Any improvements here would be immense.
 - **BIG**: Integrating datcore's information into Exporter/Editor for searching/listing/grouping models and textures
 - **MED**: Using the `edit composite` functionality to provide access to non-loaded models
- ROADMAP.md: Explains current state of advanced model/posed model export tool and DAT explorer / ingestor relevance to entire Guildlite functionality
- The ./tools/blender_render.py tool can be used at multiple angles to stitch together a test document
- The ./tools/gw-ctl.sh can be used to load the clean-solo profile, capture/capture-dry + screenshot, scp back files to compare in-game screenshot and output obj files after rendering. **NOTE**: Unsure if capture works 100% with new Picker tool - relates to e.g. /target and how the pick target button needs a thorough/mass fix-up.
 - IF successfully picked, the target player should be completely green - showing that the user has selected all textures on their player body to export. An example of this in-game visual can be seen from the capture at `./render-analysis/exporter-picked-player-complete.png` (manually selected ~7-10 textures from the picker list)

# **After proposing and building fixes, either rebuild->reload & programmatically export/capture as above OR ask for a manual capture and output files. The fix must be confirmed before the build can be considered complete.**
