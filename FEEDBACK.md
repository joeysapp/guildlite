Model exporting is nearly perfect. Fully textured characters, items and objects confirmed exporting - not reliably yet, but 80% there.

I tested across multiple characters and areas and have some meaningful information. After these settings below are refined and confirmed exporting selected items reliably.. Animations with the bone system will make this an immediate win for a lot of users. I'm not sure if the issues I uncovered below relate much, but the whole system is incredibly impressive so far. Exporting models at-point in their animation and slice would be all you need - I'm not familiar with how those cycles could be shared reasonably in a flat file.

## Settings for Exporting Models
```
Source: Self (Target will be/is giving unclear results until self is understood)
Detail: Advanced
Format: OBJ (+mtl/textures)

Export normals: Yes
Dedupe redraws: Yes
Exclude 2D/UI: Yes

**Trim outliner flies**: 6.0
 - Left alone while tuning for model isolation - bumped up to 15 in most locations there's an enormous low-res
Model up-axis: Z

UV coordinates: Yes
Extract textures: Yes
Write JSON manifest / audit: Yes
**Record armor slots: Yes
Record melee slots: Yes**
 - This is used in model rendering? (cannot tell yet - cannot isolate a single model holding a weapon)

**Scope**: Filtered to self is the main method I'm A/Bing and figuring out these settings and their results - can't seem to get mass zone 
Min triangles: 6
Max triangles: 0
Min vertices: 0
**Max AABB**: 100
 - I was thinking initially this would be useful for the filtering
**Center radius**: 0
 - This seems to be broken - I expect to set this value and see less matches within the bone palette area but I see little to no effect. Is this the probe functionality that we're not 100% on yet, or is a literal mesh radius that is used in the similarity check as if all models were in the same location ( as they technically are)? in other words, what is this intended to do? I feel like this is intended to be very useful
(?) Require a bound texture: Yes
 - What does this do? It does nothing for me
(?) Isolate to a source agent: Yes
 - What does this do? It does nothing for me
(?) Match tolerance: 0..N
 - This seems to do nothing like the description claims (set it to 0, still same rendering of N nearby people as 250)
(?) Probe: dump shader constants to manifest: Yes
 - Assume this is a future feature, was for diagnosing TGA skin color issue?
```

## Issues / Questions
- Setting source to entire scene while in zones does not act as expected and export the entire zone (should it not?) Instead I was only seeing a single bush near me, even with max K turned off. I set AABB to 0 and it put the skybox around the bush in the rendering. Question: Is there a way to get an export of an entire zone?
- A character wearing stark clothing in the scene could not be captured. The setting is bright/sunny, green rendered area. Charaacter is wearing all black with a bright pink Lion Mask (very large, >50 AABB)
 - AABB is set to 50, select self only see pink cape. Expected-ish
 - AABB is set to 200, same result. Unexpected.
 - Character hides pink hat, more is visible (but not much)
 - User can see red striped gloves but not complete black armor. No setting change gets more rendered.
 - Slightly changing my settings unexpectedly grabbed a nearby game object (I think me too, but I can't see myself inside of it.)
   Added pictures and settings for both of these cases above to help - placed at `./feedback-images`
- If I'm setting to self as target, origin is from the player's head right?
- This is probably a no, but how hard is reliably getting more of the scene in the export? I think of the radius/selection system being able to intelligently select only a certain area to render, but I realize we're talking polygons here

## Suggestions
- Place bottom capture information inside of a toggleable dropdown above the export button, showing the same information and any more we can provide for learning/diagnostics such as bounding box/volume, perhaps event coords/center if possible. Arranging them meaningfully and aligning to be legible at a quick glance would be good.
 - If possible - in this new diagnostic dropdown could there be a "Refresh" button for users to refresh their config diagnostics without starting an export every time?
- I think a great feature too would be saved/named configurations from a dropdown perhaps with some working defaults:
```
I have discovered one single working "isolation" - getting all the outpost/nearby zone members' face mesh. Have not had luck isolating own though.
Suggestion in writing guide is to list out a good number of proposed/tested/known-working combinations for various commonly-rdesired results such as:
- Single model export
- Single item export
- Single target export
- Entire zone export
- The only reliable render config I have made: Render faces around you:
 - Source: Target
 - Extent: ~25
 - Match: Does not make a difference
 - Radius: Seemed to effect results, not sure
```

I'm very much learning this system, could you research how it's built and write me out a quick rundown on the unclear items above that would be helpful. If you uncover any bugs or missing/great-to-add functionality and helper text, feel free to make those changes. 
