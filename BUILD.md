Read ./ROADMAP.md and build the following three roadmap items in order of priority and scope:

1. [MAJO] Resolve model extractor exporting transparent textures in a near-invisible state. Reference textures and of multiple instances at `./transparency`.
 - Could possibly be a rendering issue, images were created with blender_render - the macOS preview shows completely normal armor, then when rendered blender_render outputs the player's armor skirt nearly invisible (player_pn65_map248_20260708-060342). The same thing occurs in the second character (player_pn65_map248_20260708-053536) - macOS preview shows fine, in rendering so much of their armor is made transparent almost completely. The third model is a fire elemental whose flame spikes should be transparent but the black shows in both preview and render (the black in the PNG may have been transparent in TGA? Are we missing a mask?)
 - A side-by-side image of all three models' above state for their renderers (macOS, blender) is at ./transparency/all-renders.png
 - Is our pipeline extracting and saving alpha layers properly?
  - **REF**: ./third_party/TexMod/wikipedia-entry.htmlL126: PNGs do not have an alpha channel, so why are we using PNGs when nearly every complex model in game has multiple meshes/layers with various alpha levels?
 - Are we missing alpha masks anywhere, or some other hidden-within-GW.dat information? Reminder that we are still not decoding a good number of DAT entries on the Windows machine.

__[ HUMAN WILL VERIFY TRANSPARENCIES FIXED WHILE SIMPLER TASKS 2 AND 3 ARE BUILT ]__

2. [MEDIUM] Export all objtex content **AND** animation/pose/scene/timing information in a new **gltf / glb** mode.
 - Render several exports with blender_render, give instructions for threejs loading (`topic-get`, file browser external project)

3. [MEDIUM] Build a chat command interceptor / interface that can be used in-game and via stub/`control`, providing the `/chest` command to open Xunlai chests anywhere in the game.
