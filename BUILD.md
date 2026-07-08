# Pick Mode as Priority
Guildlite `Exporter` recently gained a `Pick Mode` to select visible textures for export. This is the sole mode being used now with pose/animation and bone weight export always set to true.

Perfecting the Pick Mode to be reliable and controllable over SSH is the #1 priorty for the `Model Export` tool.

# Bind-Pose v. Bound/Skinned Render - When and Why
Across all characters and equipment/armor tested, only sections of the player character can be exported/snapshotted as expected (rendered mid-animation) with the other half exported otherwise (in bind-pose.) This arrangement can flip, such that the first half is in bind-pose and the second is a posed/real state.

The general flip-flop seems to be upper-body renders / lower-body renders, meaning a given pick snapshot will have an expected upper body (head, hair, face, neck, upper armor and skin, upper arms) in perfect snapshot pose and lower body in bind-pose (legs, shins and feet, gloves and hands.) This can swap as stated for the upper body to be in bind-pose and lower-body in rendered state, but is less common.

Suggestions range from reviewing how a bound/skinned texture is captured, along with knowing that many of these items we do not classify as "skinned" - many player 'skins' (their physical skin, colored/hued to player settings like hair and face setting) end in bind-pose while some are posed perfectly.

If required, can view all contents of ./render-analysis for png renderings of outputs of a bottom-half of a character is reliably in bind-pose while the rest is as expected. In bind-pose is lower body (armored skirt, armored shoes, upper neck texture.) 

# Improved Guildlite UI/UX (Exporter, General)
The following controls are proposed:
- General controls usable over gw-ctl AND in chat (eventually if not now):
 - Log into a character, log out to character screen
 - Target a character/npc/item
The following upgrades to Exporter are proposed:
- Extend exporter state persist/save through reloads to include:
 - Selected rendered signatures (verts, faces, ?label)
- **Review logic behind animation/pose bind toggles in exporter config. THEY ARE ALWAYS SET TO TRUE, but when toggled off not all items are exported in bind-pose. Likely related/similar issue to active main build issue.**
- Review the export target button v. manual texture selection in build.
 - Both are working but export target seems to export in bind-pose more often. Have been disabling many, many textures resulting in export target single button working decently well
 - Exporting manually-selected textures DOES NOT WORK unless the character is targeted, which we need as a control surface for this to be verifiable. Secondary solution: If nothing is targeted, target self.

# Proposed Build Steps
- Analyze state, review previous attempts at 'stitching' renders
- Build solutions - Extend control surface - Propose bone-animation solutions

- ./build.sh --guildlite and wait
- printf 'reload\n' > /tmp/gl-control; scp -q /tmp/gl-control guildlite-win:Documents/guildlite/control and wait

- Send controls to select N textures manually UNLESS persist through reload (good) and wait
- Send controls to capture-dry or capture
- Review until fix - exports are either IN bind-pose or as rendered in whatever posed/animated/bound/skinned state and bone position they are visually in
