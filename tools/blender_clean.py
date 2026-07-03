#!/usr/bin/env python3
"""Render a Guildlite OBJ with a set of draws deleted -- validate cull rules offline.

Usage:
    blender --background --python tools/blender_clean.py -- <model.obj> <out_prefix> <drop_csv> [angles_csv]

<drop_csv> is a comma list of draw indices to delete (matching `o object_<N>` in the OBJ),
or "" to keep everything. Reuses blender_audit's framing/lighting. The point: prove that
dropping the additive-effect draws leaves a clean, complete body -- before porting the rule
into the capture DLL.
"""
import bpy, sys, math
from mathutils import Vector

argv = sys.argv
argv = argv[argv.index("--") + 1:] if "--" in argv else []
if len(argv) < 3:
    print("usage: ... -- <model.obj> <out_prefix> <drop_csv> [angles_csv]")
    sys.exit(1)
obj_path, out_prefix = argv[0], argv[1]
drop = set(int(x) for x in argv[2].split(",") if x.strip() != "")
angles = [float(a) for a in (argv[3].split(",") if len(argv) > 3 else ["0", "90", "180", "270"])]

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
try:
    bpy.ops.wm.obj_import(filepath=obj_path)
except Exception:
    bpy.ops.import_scene.obj(filepath=obj_path)

# delete dropped draws by object name (importer names them object_<draw_index>)
deleted = 0
for o in list(scene.objects):
    if o.type != 'MESH':
        continue
    name = o.name.split(".")[0]  # blender may suffix dupes .001
    if name.startswith("object_"):
        try:
            idx = int(name[len("object_"):])
        except ValueError:
            continue
        if idx in drop:
            bpy.data.objects.remove(o, do_unlink=True)
            deleted += 1

meshes = [o for o in scene.objects if o.type == 'MESH']
if not meshes:
    print("blender_clean: no meshes left after drop")
    sys.exit(2)

mn = Vector((1e18, 1e18, 1e18)); mx = Vector((-1e18, -1e18, -1e18))
for o in meshes:
    for corner in o.bound_box:
        w = o.matrix_world @ Vector(corner)
        for i in range(3):
            mn[i] = min(mn[i], w[i]); mx[i] = max(mx[i], w[i])
center = (mn + mx) * 0.5
size = mx - mn
maxdim = max(size.x, size.y, size.z) or 1.0

tgt = bpy.data.objects.new("tgt", None); scene.collection.objects.link(tgt); tgt.location = center
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); cam.data.lens = 50
scene.collection.objects.link(cam)
con = cam.constraints.new('TRACK_TO'); con.target = tgt
con.track_axis = 'TRACK_NEGATIVE_Z'; con.up_axis = 'UP_Y'
scene.camera = cam
dist = maxdim * 2.4

for i, (rx, rz, e) in enumerate([(55, 35, 3.0), (55, 215, 1.6)]):
    sun = bpy.data.objects.new(f"sun{i}", bpy.data.lights.new(f"sun{i}", 'SUN'))
    sun.data.energy = e; sun.rotation_euler = (math.radians(rx), 0.0, math.radians(rz))
    scene.collection.objects.link(sun)
world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
bg = world.node_tree.nodes.get("Background")
if bg:
    bg.inputs[0].default_value = (0.16, 0.16, 0.18, 1.0); bg.inputs[1].default_value = 1.3

engine = None
for cand in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
    try:
        scene.render.engine = cand; engine = cand; break
    except Exception:
        pass
if engine is None:
    scene.render.engine = 'BLENDER_WORKBENCH'
    sh = scene.display.shading; sh.light = 'STUDIO'; sh.color_type = 'TEXTURE'; sh.show_shadows = True
for m in bpy.data.materials:
    try:
        m.surface_render_method = 'DITHERED'
    except Exception:
        try:
            m.blend_method = 'HASHED'
        except Exception:
            pass

scene.render.resolution_x = 500; scene.render.resolution_y = 700
scene.render.image_settings.file_format = 'PNG'
for ang in angles:
    a = math.radians(ang)
    cam.location = center + Vector((math.sin(a) * dist, -math.cos(a) * dist, maxdim * 0.12))
    scene.render.filepath = f"{out_prefix}_a{int(ang):03d}.png"
    bpy.ops.render.render(write_still=True)
print(f"blender_clean: {obj_path}  dropped {deleted} objs, {len(meshes)} left, maxdim {maxdim:.1f}")
