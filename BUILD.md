# BUILD

## **DRAFT, WORKING ON OWN FLOWS** COMPLETE MODEL EXPORT 
* End goal here is to export and catalogue all of the content, assets and art in the game for personal creative use. A rendering layer using GWAC is outputting one-to-one 3D models with the caveat of two or three severe handicaps through the vertex shaders GW uses such as: Capturing flow requires serious work per model, Models are almost never complete with (WORKING ON), No working animations/skeletons for float-based sizes (No model is alike in practical planning), (GOOD) stationery NPCs and objects seem doable, Dyes and customization a distance priority. The most meanningful priority items to complete:
 1. **COMPLETE MODEL CAPTURE**
  - Can start with: by self, as wself in complete solitary - rendering engine too infrequent and unreliably at this point
  - Make as EASY AS POSSIBLE with NO finnicky circling or anything more than target-and-click.
  - **IDEA**: It MAY BE NECESSARY TO BEGIN MODEL CAPTURES/BUILDS IN A NAKED STATE - IDEALLY WITH DESSOUS REMOVED TO BASE KEN DOLL LAYER. Otherwise it seems far too easy for a ray or skeleton bone to case off a piece of armor or weapon resulting in an incomplete capture. **NOTE**: The term in code is "BareSkin"  or NakedCharacter I can't recall? The completely naked entitty is comical to me now beginning to understand how hard rendering with clothes on is.
 2. Animation - Blender and Personal creative engine. IOnly worked with OSRS Content (OBJs, nothing as complex as this. Excited to get to this stage.)

**WARN**: IT IS HIGHLY LIKELY THE DEV-TOOLS HARNESS IS TOO NEW FOR AUTOMATED QUALITY, SOLO MODEL CAPTURES. AS A RESULT, THE FOLLOWING MANUAL/BOTTED SPECIFIC REQUESTS ARE LISTED AS WELL.**
 
----

## MULTI-STATE COMPLETE MODEL EXPORTS 
Use the Guildlite model xport tool in the injector to provide the following **COMPLETE** models, verifed via via Blender tooling from enough angles to visible confirm model was captured comprehensively. IF POSSIBLE, REQUEST TO CAPTURE POSES. UNSURE HOW TO USE ANIMATIONSYET, CANNOT FIND RESTING POSE. LFG HELP IF KNOWEDGABLE, CODING Blender CONTROLS INTO OWN TOOL
- Any Female Elementalist
 - In: CharacterNude (noo armor. First stationary, then any emotes in full if possible)
 - In: Chaos Gloves, (any is fine, naked is best for composability)
- **Any and All Female Professions, Tonics States, Minifigures and Mobs**
 - In: CharacterNude (posing, any and all animations)
 - In: CharacterNude, Any and All Equiptment 
 - In: Any and All Armors, (no equiptment)
 
---

# TOOLS AND REFERENCED CONSUMERS
* guildlite/tools/blender_render.py - Render objects using high-fidelityy Blender configurations with successfully exported models
* guildlite/tools/dev-loop - Useful to audit AI while running complex tasks.
* **../topic-get/src/frontend/forge** - Multi-doomain art compositor, 3D fields, 3D->2D, rasters and **FUTURE** Upgrading 3js renderers to handle +MTLs
* **../topic-get/internal/forge** - Internal domain interop, graph editing and **FUTURE** Planning Researching reality Guild Wars animations, bones, skeletons nodes, bringing in timer-based content as a result
 - **NOTE**: Seems like associating /binding skeletons and timestamps to a rendering flow like seen here (10 seconds): [**topic-get - OSRS Runescape Objects Warping Through Time**](https://youtu.be/8CabITKIsSA)


# RESEARCH AND BUILD TO ACQUIRE CREATIVE RESOURCES FOR TOPIC-GET
Using the knowledge gained from dev-loop, read above and below asks to provide rendered models base f for creative content. Research at length IF and HOW aninmations coulcould be provided in data formm/instructions for Blender/track setups and finally, a correction and reguidance to to instead use a **~20 minute model setter and cleanroom generator to get near-source quality content.** The perfect scenario here would be 
