
## BUILD FIRST DEV LOOP
Using the following suggested template for making critical choices based on prior decisions, the first being a loop to provide a harness to quantify settings  content over time, beginning with large discoeries and fixing unseen bugs, eventually perfecting a list of settings to use in different settings and scenarios. The initial draft is as follows:

**Rendering: Finalize Remaining Features Using Dev Loop**
 0) Use `gw-cmd / Dev Loop` control layer to Take Screenshot of Windows Gw.exe
 1) Move Screenshot to models/ingame.png
   A) Pick Random `model-export` Settings From Recommended `model/SETTINGS.md` Settings
	   OR
   B) Analyze Image with AI to Determine Best `model-export` Using `models/SETTINGS.md`
 3) Use `gw-cmd / Dev Loop` to Set Windows Gw.exe Guildlite settings as specified
 4) Use `gw-cmd / Dev Loop` to Target Self/Other/None (When Testing Area)
 5) Use `gw-cmd / Dev Loop` to Follow Complete Written Plan Instructions To Export Object
 6) Move Exported Objectfile(s) to models/object..
 7) Use Windows 3D Object Viewer to Generate Rendering of Object
 8) Move Rendering to models/render.png
 9) Compare `ingame.png` To `render.png`
 ... repeat while recording changes and outcomes N times in same setting, and same actions ..
11) Report Findings to models/analysis.txt

**GOAL**: Uncovering Bugs/Broken Render Logic
**GOAL**: Building the First Basic Dev Loop By Hand, Recording All Steps, Automating All Steps, And Qualifying Final Outcome

## BACKGROUND - CURRENT STATE OF MODEL EXPORT
- [ ] **Rendering: Categorize Remaining Render Priority Issues:**
 - **Character Skin in Renders (Vertex Shader?)**
 - **Review ./output/* Objects and Renderings to Assess State of Model Export Quality**
  0) All: Characters re not rendered with skin - ever
  1) 1-5: Show Most Renders Not Fully Exporting Content
  2) hazel: Shows complete lack of quality in macOS builtin rendering; Blender resulting in the same. Looking for simple mtl+/obj viewers on macOS/Linux.
  3) warrior: Export with settings expected to export entire character, but only exported face
