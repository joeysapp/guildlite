#!/usr/bin/env python3
"""Multi-angle audit render of a Guildlite OBJ export.

Usage:
    blender --background --python tools/blender_audit.py -- <model.obj> <out_prefix> [angles]

Loads the OBJ *once* and renders a turnaround (default 0/90/180/270 deg) so a capture
can be eyeballed for completeness from every side -- the "is this a whole model or did it
drop the back/legs?" check that a single 3/4 view can't answer. Writes one PNG per angle:
    <out_prefix>_a000.png, _a090.png, _a180.png, _a270.png
Stitch them into a contact sheet afterwards with ImageMagick `montage`.

Deterministic (fixed camera/light), no GUI. Blender 4.x / 5.x. Two-sun lighting so back
views aren't black; brighter neutral world than blender_render.py for legibility.
"""
import bpy, sys, math
from mathutils import Vector

argv = sys.argv
argv = argv[argv.index("--") + 1:] if "--" in argv else []
if len(argv) < 2:
    print("usage: blender --background --python blender_audit.py -- <model.obj> <out_prefix> [angles_csv]")
    sys.exit(1)
obj_path, out_prefix = argv[0], argv[1]
angles = [float(a) for a in (argv[2].split(",") if len(argv) > 2 else ["0", "90", "180", "270"])]

# --- empty scene ---------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# --- import the OBJ (materials + textures come in from the .mtl) ----------------
try:
    bpy.ops.wm.obj_import(filepath=obj_path)          # Blender 3.3+/4.x/5.x
except Exception:
    bpy.ops.import_scene.obj(filepath=obj_path)        # legacy fallback

meshes = [o for o in scene.objects if o.type == 'MESH']
if not meshes:
    print("blender_audit: no meshes imported from", obj_path)
    sys.exit(2)

# --- combined world-space bounding box -----------------------------------------
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

# --- target empty + camera (rebuilt per angle) ---------------------------------
tgt = bpy.data.objects.new("tgt", None)
scene.collection.objects.link(tgt)
tgt.location = center

cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 50
scene.collection.objects.link(cam)
con = cam.constraints.new('TRACK_TO')
con.target = tgt
con.track_axis = 'TRACK_NEGATIVE_Z'
con.up_axis = 'UP_Y'
scene.camera = cam
dist = maxdim * 2.4

# --- two-sun lighting so back views aren't black + neutral world ---------------
for i, (rx, rz, e) in enumerate([(55, 35, 3.0), (55, 215, 1.6)]):
    sun = bpy.data.objects.new(f"sun{i}", bpy.data.lights.new(f"sun{i}", 'SUN'))
    sun.data.energy = e
    sun.rotation_euler = (math.radians(rx), 0.0, math.radians(rz))
    scene.collection.objects.link(sun)

world = bpy.data.worlds.new("w")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes.get("Background")
if bg:
    bg.inputs[0].default_value = (0.16, 0.16, 0.18, 1.0)
    bg.inputs[1].default_value = 1.3

# --- render engine: EEVEE (Ks -> specular, map_d -> alpha cutout) ---------------
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

for m in bpy.data.materials:
    try:
        m.surface_render_method = 'DITHERED'   # Blender 4.2+/5.x
    except Exception:
        try:
            m.blend_method = 'HASHED'          # Blender 3.x/4.0-4.1
        except Exception:
            pass

scene.render.resolution_x = 500
scene.render.resolution_y = 700
scene.render.image_settings.file_format = 'PNG'

for ang in angles:
    a = math.radians(ang)
    cam.location = center + Vector((math.sin(a) * dist, -math.cos(a) * dist, maxdim * 0.12))
    scene.render.filepath = f"{out_prefix}_a{int(ang):03d}.png"
    bpy.ops.render.render(write_still=True)
    print("blender_audit: wrote", scene.render.filepath)

print(f"blender_audit: done {obj_path}  ({len(meshes)} meshes, maxdim {maxdim:.1f}, engine {scene.render.engine})")
