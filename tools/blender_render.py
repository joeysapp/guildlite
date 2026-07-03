#!/usr/bin/env python3
"""Headless Blender render of a Guildlite OBJ export (+ .mtl + .tga) to a PNG.

Usage:
    blender --background --python tools/blender_render.py -- <model.obj> <out.png> [angle_deg]

The mac-side render step of the dev loop: after a capture is fetched, render the whole
model framed and textured on a neutral backdrop so it can be eyeballed / diffed against
the in-game screenshot. Deterministic (fixed camera/light), no GUI. Blender 4.x / 5.x.
"""
import bpy, sys, math
from mathutils import Vector

argv = sys.argv
argv = argv[argv.index("--") + 1:] if "--" in argv else []
if len(argv) < 2:
    print("usage: blender --background --python blender_render.py -- <model.obj> <out.png> [angle_deg]")
    sys.exit(1)
obj_path, out_png = argv[0], argv[1]
angle_deg = float(argv[2]) if len(argv) > 2 else 35.0

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
    print("blender_render: no meshes imported from", obj_path)
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

# --- camera: 3/4 front, slightly above, distance scaled to fit -----------------
tgt = bpy.data.objects.new("tgt", None)
scene.collection.objects.link(tgt)
tgt.location = center

cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 50
scene.collection.objects.link(cam)
dist = maxdim * 2.4
a = math.radians(angle_deg)
cam.location = center + Vector((math.sin(a) * dist, -math.cos(a) * dist, maxdim * 0.15))
con = cam.constraints.new('TRACK_TO')
con.target = tgt
con.track_axis = 'TRACK_NEGATIVE_Z'
con.up_axis = 'UP_Y'
scene.camera = cam

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

scene.render.resolution_x = 800
scene.render.resolution_y = 1000
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = out_png
bpy.ops.render.render(write_still=True)
print("blender_render: wrote", out_png, "engine", scene.render.engine)
