Guildlite has been built with a control surface provided over SSH/terminal through reading and evaluating ./control. This ingress is used in several ways, the primary and most regular use case being reloading the active dll into a new build.

Most if not all of the available ./control commands can be found in the `Info` panel, accessed by left-clicking 'Info' in bottom right. 

# Build Steps
- Analyze state, review previous attempts at 'stitching' renders
- Build solutions - Extend control surface - Propose bone-animation solutions
- analyze Pazzos captured pizza
- analyze Pazzos complete control panel and surface as possible, e.g.:
 - WASDFZX, R/EX. When freecam mode ON, intercept and do not pass-through model controls. Provide through two new toggles: Lock Camera, Lock Target (default: self). Show locked items next to selected items with an un-disable. 
 - Integrate existing labels (MANY) into new datcore labels.json and determine how to meaningfully combine the two for cross-application use

- [~] ./build.sh --guildlite and wait
- [~] printf 'reload\n' > /tmp/gl-control; scp -q /tmp/gl-control guildlite-win:Documents/guildlite/control and wait. The unforunate truth is there has been no review and extension of prior commands into new commands which must be remedied today. Some of the groups include but are not limited to the following manual discoveries (will need to add more flags/search context for models, objects, etc.):
- [ ] Providing more programmatic surface for controls the better. Things I cannot do now, but could if this document successfully built
 - Save/load more information to settings.json like texture selection (successful load-in when textures present, otherwise drop selection)
 - Load into any targetted character and export model, ensure targetting a fluke/unrelated
 - Model Exporter's NPC List jumps around constantly while at the end, accessing tunnels that are at the end of the render cycle. This is due to sweat/etc. on equipment - cannot be avoilded - but would ask for towels/wipedown for courtesy as I do the same.
 - Flying high at >10k feet with no seatbelt, etc.

--- 

```
Player/self, read from source and strike him! Current states are: scale=1, identity=unset, equipment=unset, saved_chars=unset

Integrating the built tags.json will be interesting. I run a local deployment for building the frontend (React SPA) but realize the WebView plan will need some work/review. Hope to have a working labels/tags/compendium-concretized-and-combined-to-single-file textfile I can add to while not playing the game.
```

- [ ] Send controls to select N textures manually UNLESS persist through reload (good) and wait.
- [~] Send controls to capture-dry or capture to review A/Bconfigurations
- [ ] Send controls and help/info to both CLI and transmog faces - should see colored/uniq character
- [ ] Review until fix - exports are either IN bind-pose or as rendered in whatever posed/animated/bound/skinned state and bone position they are visually in
