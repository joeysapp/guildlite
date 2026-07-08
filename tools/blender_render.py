#!/usr/bin/env python3
"""Blender-CLI (4.x/5.x) render of an OBJ export (w/ .mtl/.tgas) to a PNG

Usage:
    blender --background --python tools/blender_render.py -- <model.obj> <out.png> [angle_deg] ... <todo>

Todos:
- Improved CLI usage like:
 - Arg defaults / usable flags
 - Templates w/ script and Blender settings, fully documented/labeled/listed here under --templates
 - Increased flags for rendering, overriding above template settings such as rotations/transform matrix/lighting/shading
  - **IDEAL**: Create a number of unique and clearly distinct templates with background values, lighting configurations and Blender-powered rendering pipeline states to output creative and identifiable renders (e.g. fisheye / close or angled up/down shots, zoom / orthographic or extremely far shots to communicate distance.) Consider optional backgrounds for creative output vs diagnostic outputs.
 - Novel Blender features as flags/templates like:
  - Fisheye rendering
  - Animation of SUN/SPOTLIGHT/some-cool-blender-light orbiting around the objectOB
 - Flag to output helpful labels (LITERALS):
  - (medium) Output filename in top left
  - (small) Command-line flag values (small, meaningful such as template/rotations/transforms)
  - (well-made, aesthetic) Relative XYZ axis arrows pointing , bottom left (typical RGB 3D object pointing in dirs)
  - (tasteful, good font e.g. Essential PragmataPro or other monospace) Optional label/desc/notes at bottom
 - Batch render series, output to a dir / as a stitched-together image (showing all rotations) / different templates
 - Power-user usage e.g. no file input -> get most recent files from $GUILDLITE_OUTPUT (eg), no file output -> write out to file input - #### - .png
 - Build all above, improve --help/-h and expose all flags / cool blender features

"""
import bpy, sys, math
from mathutils import Vector

argv = sys.argv
argv = argv[argv.index("--") + 1:] if "--" in argv else []
if len(argv) < 2:
    print("objfile=\"$HOME/etc/git/guildlite-output/<fname>\"; blender --threads 12 --background --python $HOME/etc/guildlite/tools/blender_render.py -- $objfile.obj //$objfile.png [angle_deg] --cycles-print-stats")
    sys.exit(1)
obj_path, out_png = argv[0], argv[1]
angle_deg = float(argv[2]) if len(argv) > 2 else 35.0

# --- empty scene & basic flags --------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.resolution_x = 600
scene.render.resolution_y = 1000
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = out_png

# --- import the OBJ (materials + textures come in from the .mtl) ----------------
try:
    bpy.ops.wm.obj_import(filepath=obj_path)           # Blender 3.3+/4.x/5.x
except Exception:
    bpy.ops.import_scene.obj(filepath=obj_path)        # legacy fallback

meshes = [o for o in scene.objects if o.type == 'MESH']
if not meshes:
    print("blender: no meshes imported from", obj_path)
    sys.exit(2)

# --- (arbtirary) combined world-space bounding box --------------------------------
mn = Vector((1e18, 1e18, 1e18))
mx = Vector((-1e18, -1e18, -1e18))
for o in meshes:
    for corner in o.bound_box:
        w = o.matrix_world @ Vector(corner)
        for i in range(3):
            mn[i] = min(mn[i], w[i])
            mx[i] = max(mx[i], w[i])
center = (mn + mx) * 0.5
size = mx - mn
maxdim = max(size.x, size.y, size.z) or 1.0

# --- camera: 3/4 front, slightly above, distance scaled to fit -----------------
tgt = bpy.data.objects.new("tgt", None)
scene.collection.objects.link(tgt)
tgt.location = center

cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 50
dist = maxdim * 1.66
a = math.radians(angle_deg)
cam.location = center + Vector((math.sin(a) * dist, -math.cos(a) * dist, maxdim * 0.15))
con = cam.constraints.new('TRACK_TO')
con.target = tgt
con.track_axis = 'TRACK_NEGATIVE_Z'
con.up_axis = 'UP_Y'
scene.camera = cam
scene.collection.objects.link(cam)

# --- lighting + neutral world --------------------------------------------------
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN'))
sun.data.energy = 3.0
sun.rotation_euler = (math.radians(55), 0.0, math.radians(35))
scene.collection.objects.link(sun)

world = bpy.data.worlds.new("w")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes.get("Background")
if bg:
    bg.inputs[0].default_value = (0.18, 0.18, 0.20, 1.0)
    bg.inputs[1].default_value = 1.0

# --- render engine: EEVEE (Ks -> specular, map_d -> alpha cutout), Workbench fallback ---
engine = None
for cand in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
    try:
        scene.render.engine = cand
        engine = cand
        break
    except Exception:
        pass
if engine is None:
    scene.render.engine = 'BLENDER_WORKBENCH'
    sh = scene.display.shading
    sh.light = 'STUDIO'
    sh.color_type = 'TEXTURE'
    sh.show_shadows = True

# Alpha: opaque body/armor (no map_d -> no BSDF alpha) stays solid; cutout pieces (map_d
# wired the texture alpha to BSDF Alpha) clip cleanly. DITHERED handles both in EEVEE Next.
for m in bpy.data.materials:
    try:
        m.surface_render_method = 'DITHERED'   # Blender 4.2+/5.x
    except Exception:
        try:
            m.blend_method = 'HASHED'          # Blender 3.x/4.0-4.1
        except Exception:
            pass

bpy.ops.render.render(write_still=True)
print("\nblender engine:\t", scene.render.engine, "\noutfile:\t", out_png)
