#!/usr/bin/env python3
"""Blender Render V2

A reusable rendering pipeline for generating diagnostic, presentation,
and dataset imagery from 3D models.

Usage:
  To pass arguments to this script via Blender, you MUST use `--` to separate 
  Blender arguments from the script arguments. Otherwise, Blender will consume 
  flags like `--help` for itself.

  Render a single model:
    blender --background --python tools/blender_render_v2.py -- render model.obj --template ortho
  
  Batch render:
    blender --background --python tools/blender_render_v2.py -- batch models/ -t turntable --contact-sheet
    
  Animate:
    blender --background --python tools/blender_render_v2.py -- animate model.obj --fisheye --lens 5 --distance 0.25 --template orbit-sun --fps 60 --time 5000
  
  Show script help (not Blender's help):
    blender --background --python tools/blender_render_v2.py -- --help
    
  Alternatively, you can run the script directly (outside Blender) just to see help:
    ./tools/blender_render_v2.py --help
"""

from dataclasses import dataclass
from pathlib import Path
import argparse
import math
import sys
import os
import subprocess

try:
    import bpy
    from mathutils import Vector, Quaternion
except ImportError:
    pass

# ==============================================================================
# Phase 3 - Template System
# ==============================================================================

@dataclass
class Template:
    name: str
    distance: float
    lens: float    

    ortho: bool = False
    fisheye: bool = False
    
    yaw: float = 0
    pitch: float = 0
    roll: float = 0

    light_rig: str = "sun"    
    background: tuple = (1, 1, 1, 1)
    
    wireframe: bool = False 
    clay: bool = False
    default_frame: str = "auto"    
    engine: str = 'BLENDER_EEVEE_NEXT'    

TEMPLATES = {
    "diagnostic": Template("diagnostic", lens=50, yaw=35, pitch=10, distance=1.66, background=(0.18, 0.18, 1.20, 0.01), light_rig="inspection"),
    "portrait": Template("portrait", lens=85, yaw=15, pitch=4, distance=1.15, background=(0.12, 0.12, 0.12, 1), default_frame="torso", light_rig="three-point"),
    "ortho": Template("ortho", lens=50, yaw=45, pitch=30, distance=1.5, background=(1, 1, 1, 1), ortho=True, light_rig="sun"),
    "clay": Template("clay", lens=70, yaw=25, pitch=15, distance=1.4, background=(0.16, 0.16, 0.16, 1), engine='BLENDER_WORKBENCH', clay=True, light_rig="studio"),
    "wireframe": Template("wireframe", lens=50, yaw=35, pitch=10, distance=1.66, background=(0.05, 0.05, 0.05, 1), engine='BLENDER_EEVEE_NEXT', wireframe=True, light_rig="inspection"),
    "turntable": Template("turntable", lens=55, yaw=0, pitch=10, distance=1.6, background=(0.18, 0.18, 0.20, 1), light_rig="inspection"),
    "orbit-sun": Template("orbit-sun", lens=50, yaw=35, pitch=10, distance=1.66, background=(0.18, 0.18, 0.20, 1), light_rig="orbit-sun"),
}


# ==============================================================================
# Phase 1 - Architecture: Scene, Camera, Lighting, Renderer
# ==============================================================================

class SceneManager:
    def clear(self):
        bpy.ops.wm.read_factory_settings(use_empty=True)

    def import_obj(self, filepath):
        try:
            bpy.ops.wm.obj_import(filepath=filepath)
        except Exception:
            bpy.ops.import_scene.obj(filepath=filepath)

    def get_bounds(self):
        meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
        if not meshes:
            return Vector((0, 0, 0)), Vector((1, 1, 1))
            
        mn = Vector((1e18, 1e18, 1e18))
        mx = Vector((-1e18, -1e18, -1e18))
        for o in meshes:
            for corner in o.bound_box:
                w = o.matrix_world @ Vector(corner)
                for i in range(3):
                    mn[i] = min(mn[i], w[i])
                    mx[i] = max(mx[i], w[i])
        return mn, mx


class CameraRig:
    def __init__(self, scene_center, scene_size):
        self.center = scene_center
        self.size = scene_size
        self.maxdim = max(scene_size.x, scene_size.y, scene_size.z) or 1.0
        
        for obj in bpy.context.scene.objects:
            if obj.type == 'CAMERA':
                bpy.data.objects.remove(obj, do_unlink=True)

        self.cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        bpy.context.scene.collection.objects.link(self.cam)
        bpy.context.scene.camera = self.cam

    def apply_framing(self, resolved):
        distance_mult = resolved['distance']
        tgt_loc = self.center.copy()
        
        mode = resolved['frame']
        if mode == 'full':
            pass
        elif mode == 'torso':
            tgt_loc.z += self.size.z * 0.25
            distance_mult *= 0.7
        elif mode == 'head':
            tgt_loc.z += self.size.z * 0.4
            distance_mult *= 0.4
        elif mode == 'feet':
            tgt_loc.z -= self.size.z * 0.4
            distance_mult *= 0.4

        self.cam.data.lens = resolved['lens']
        dist = self.maxdim * distance_mult

        yaw_rad = math.radians(resolved['yaw'])
        pitch_rad = math.radians(resolved['pitch'])

        x = math.sin(yaw_rad) * math.cos(pitch_rad) * dist
        y = -math.cos(yaw_rad) * math.cos(pitch_rad) * dist
        z = math.sin(pitch_rad) * dist

        cam_loc = tgt_loc + Vector((x, y, z))
        self.cam.location = cam_loc

        direction = tgt_loc - cam_loc
        if direction.length > 0:
            rot_quat = direction.to_track_quat('-Z', 'Y')
            roll_quat = Quaternion((0.0, 0.0, 1.0), math.radians(resolved['roll']))
            self.cam.rotation_mode = 'QUATERNION'
            self.cam.rotation_quaternion = rot_quat @ roll_quat

        if resolved['orthographic']:
            self.cam.data.type = 'ORTHO'
            self.cam.data.ortho_scale = self.maxdim * distance_mult * 1.1
        elif resolved['fisheye']:
            self.cam.data.type = 'PANO'
            try:
                self.cam.data.panorama_type = 'FISHEYE_EQUISOLID'
            except AttributeError:
                pass


class LightingRig:
    def setup(self, resolved):
        for obj in bpy.context.scene.objects:
            if obj.type == 'LIGHT':
                bpy.data.objects.remove(obj, do_unlink=True)
                
        rig_type = resolved['light_rig']
        
        if rig_type == 'three-point':
            key = bpy.data.objects.new("key", bpy.data.lights.new("key", 'AREA'))
            key.data.energy = 500.0
            key.location = (2, -2, 2)
            key.rotation_euler = (math.radians(45), 0, math.radians(45))
            bpy.context.scene.collection.objects.link(key)
            fill = bpy.data.objects.new("fill", bpy.data.lights.new("fill", 'AREA'))
            fill.data.energy = 200.0
            fill.location = (-2, -1, 1)
            fill.rotation_euler = (math.radians(60), 0, math.radians(-30))
            bpy.context.scene.collection.objects.link(fill)
            rim = bpy.data.objects.new("rim", bpy.data.lights.new("rim", 'AREA'))
            rim.data.energy = 800.0
            rim.location = (0, 3, 2)
            rim.rotation_euler = (math.radians(120), 0, math.radians(180))
            bpy.context.scene.collection.objects.link(rim)
            
        elif rig_type == 'studio':
            top = bpy.data.objects.new("top", bpy.data.lights.new("top", 'AREA'))
            top.data.energy = 800.0
            top.data.size = 5.0
            top.location = (0, 0, 4)
            bpy.context.scene.collection.objects.link(top)
            front = bpy.data.objects.new("front", bpy.data.lights.new("front", 'AREA'))
            front.data.energy = 300.0
            front.data.size = 3.0
            front.location = (0, -4, 1)
            front.rotation_euler = (math.radians(80), 0, 0)
            bpy.context.scene.collection.objects.link(front)
            
        elif rig_type == 'dramatic':
            spot = bpy.data.objects.new("spot", bpy.data.lights.new("spot", 'SPOT'))
            spot.data.energy = 1000.0
            spot.data.spot_size = math.radians(30)
            spot.location = (1.5, -1.5, 2)
            spot.rotation_euler = (math.radians(45), 0, math.radians(45))
            bpy.context.scene.collection.objects.link(spot)
            
        elif rig_type == 'inspection':
            sun1 = bpy.data.objects.new("sun1", bpy.data.lights.new("sun1", 'SUN'))
            sun1.data.energy = 2.0
            sun1.rotation_euler = (math.radians(45), 0.0, math.radians(45))
            bpy.context.scene.collection.objects.link(sun1)
            sun2 = bpy.data.objects.new("sun2", bpy.data.lights.new("sun2", 'SUN'))
            sun2.data.energy = 1.0
            sun2.rotation_euler = (math.radians(45), 0.0, math.radians(-135))
            bpy.context.scene.collection.objects.link(sun2)
            
        elif rig_type == 'orbit-sun':
            light = bpy.data.objects.new("orbit_light", bpy.data.lights.new("orbit_light", 'POINT'))
            # Energy will be scaled dynamically based on distance in animate()
            light.data.shadow_soft_size = 0.25
            bpy.context.scene.collection.objects.link(light)

        else: # sun or default
            sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN'))
            sun.data.energy = 3.0
            sun.rotation_euler = (math.radians(45), 0.0, math.radians(35))
            bpy.context.scene.collection.objects.link(sun)
        
        world = bpy.data.worlds.new("w")
        bpy.context.scene.world = world
        world.use_nodes = True
        bg = world.node_tree.nodes.get("Background")
        if bg:
            bg.inputs[0].default_value = resolved['background']
            bg.inputs[1].default_value = resolved.get('env_strength', 1.0)


class Renderer:
    def __init__(self, obj_path):
        self.obj_path = obj_path
        self.scene_mgr = SceneManager()
        self.scene_mgr.clear()
        self.scene_mgr.import_obj(obj_path)
        self.mn, self.mx = self.scene_mgr.get_bounds()
        self.center = (self.mn + self.mx) * 0.5
        self.size = self.mx - self.mn
        self.maxdim = max(self.size.x, self.size.y, self.size.z) or 1.0
        self._effect_dominant = self._compute_effect_dominant()

    def _compute_effect_dominant(self):
        """True when most of the mesh is additive (flames/auras). Such models are
        drawn to add light over the dark game world; on a light backdrop they wash
        out, so we default them to a dark background unless the user overrides -bg."""
        import json
        jp = Path(self.obj_path).with_suffix('.json')
        if not jp.exists():
            return False
        try:
            data = json.loads(jp.read_text())
        except Exception:
            return False
        add = tot = 0
        for c in data.get('chunks', []):
            t = int(c.get('triangles', 0) or 0)
            tot += t
            if (c.get('dest_blend') == self.D3DBLEND_ONE) or bool(c.get('is_effect')):
                add += t
        return tot > 0 and add >= 0.6 * tot

    def _resolve_config(self, template, overrides):
        frame = overrides.get('frame') if overrides.get('frame') and overrides.get('frame') != 'auto' else template.default_frame
        bg = overrides.get('background') or template.background
        # Additive-dominant models (fire elementals, spirits) only read on a dark
        # backdrop; default them there unless the caller passed an explicit -bg.
        if 'background' not in overrides and getattr(self, '_effect_dominant', False):
            bg = (0.05, 0.05, 0.07, 1.0)
        passes = overrides.get('passes', 'beauty')
        return {
            'distance': overrides.get('distance') if overrides.get('distance') is not None else template.distance,
            'lens': overrides.get('lens') if overrides.get('lens') is not None else template.lens,
            'orthographic': bool(overrides.get('orthographic')) if overrides.get('orthographic') is not None else template.ortho,
            'fisheye': bool(overrides.get('fisheye')) if overrides.get('fisheye') is not None else template.fisheye,
            'yaw': overrides.get('yaw') if overrides.get('yaw') is not None else template.yaw,
            'pitch': overrides.get('pitch') if overrides.get('pitch') is not None else template.pitch,
            'roll': overrides.get('roll') if overrides.get('roll') is not None else template.roll,
            'light_rig': overrides.get('light_rig') or template.light_rig,
            'background': bg,
            'env_strength': overrides.get('env_strength', 1.0),
            'orbit_radius': overrides.get('orbit_radius', 2.0),
            'passes': passes,
            'frame': frame,
            'overlay': bool(overrides.get('overlay'))
        }

    def _setup_engine(self, template, resolved):
        scene = bpy.context.scene
        target_engine = template.engine
        
        if resolved['fisheye']:
            target_engine = 'CYCLES'
            
        try:
            scene.render.engine = target_engine
        except Exception:
            if target_engine == 'BLENDER_EEVEE_NEXT':
                try:
                    scene.render.engine = 'BLENDER_EEVEE'
                except Exception:
                    scene.render.engine = 'BLENDER_WORKBENCH'
            else:
                scene.render.engine = 'BLENDER_WORKBENCH'
        return scene.render.engine

    def _setup_transparency(self, resolved):
        scene = bpy.context.scene
        bg = resolved['background']
        # If RGBA and alpha < 1.0
        if len(bg) == 4 and bg[3] < 1.0:
            scene.render.film_transparent = True
        else:
            scene.render.film_transparent = False
        scene.render.image_settings.color_mode = 'RGBA'

    def _setup_overlays(self, resolved):
        scene = bpy.context.scene
        if resolved['overlay']:
            if hasattr(scene.render, 'use_stamp'):
                scene.render.use_stamp = True
            if hasattr(scene.render, 'use_stamp_labels'):
                scene.render.use_stamp_labels = False
                
            scene.render.use_stamp_note = True
            scene.render.use_stamp_date = False
            scene.render.use_stamp_time = False
            scene.render.use_stamp_camera = False
            scene.render.use_stamp_lens = False
            scene.render.use_stamp_scene = False
            
            if hasattr(scene.render, 'stamp_background'):
                scene.render.stamp_background = (0.0, 0.0, 0.0, 1.0)
            if hasattr(scene.render, 'stamp_foreground'):
                scene.render.stamp_foreground = (1.0, 1.0, 1.0, 1.0)
            
            # note formatting matching requested order
            note = (
                f"Distance: {resolved['distance']}\n"
                f"Lens: {resolved['lens']}\n"
                f"Orthographic: {resolved['orthographic']}\n"
                f"Fisheye: {resolved['fisheye']}\n\n"
                f"Yaw: {resolved['yaw']}\n"
                f"Pitch: {resolved['pitch']}\n"
                f"Roll: {resolved['roll']}\n\n"
                f"Light_rig: {resolved['light_rig']}\n"
                f"Background: {resolved['background']}\n"
                f"Env_strength: {resolved['env_strength']}\n"
                f"Passes: {resolved['passes']}\n"
                f"Frame: {resolved['frame']}"
            )
            scene.render.stamp_note_text = note

    def _setup_wireframe(self, template):
        if template.wireframe:
            for obj in bpy.context.scene.objects:
                if obj.type == 'MESH':
                    mod = obj.modifiers.new(name="Wireframe", type='WIREFRAME')
                    mod.thickness = 0.2
                    mod.use_replace = True
                    
                    mat = bpy.data.materials.new(name="WireMat")
                    mat.use_nodes = True
                    if mat.node_tree:
                        bsdf = mat.node_tree.nodes.get("Principled BSDF")
                        if bsdf:
                            if 'Base Color' in bsdf.inputs:
                                bsdf.inputs['Base Color'].default_value = (0.05, 0.05, 0.05, 1)
                            if 'Emission' in bsdf.inputs:
                                bsdf.inputs['Emission'].default_value = (0.8, 0.8, 0.8, 1)
                            elif 'Emission Color' in bsdf.inputs:
                                bsdf.inputs['Emission Color'].default_value = (0.8, 0.8, 0.8, 1)
                    
                    obj.data.materials.clear()
                    obj.data.materials.append(mat)
        elif template.clay and bpy.context.scene.render.engine == 'BLENDER_WORKBENCH':
            sh = bpy.context.scene.display.shading
            sh.color_type = 'SINGLE'
            sh.single_color = (0.5, 0.5, 0.5)
            sh.light = 'MATCAP'

    def _setup_passes(self, resolved, out_path):
        scene = bpy.context.scene
        passes = resolved['passes']
        if passes and passes != 'beauty':
            scene.use_nodes = True
            tree = getattr(scene, 'node_tree', None)
            if tree is None:
                print("Warning: compositor node_tree not found, skipping passes.")
                return
                
            tree.nodes.clear()
            rl = tree.nodes.new('CompositorNodeRLayers')
            pass_list = [p.strip() for p in passes.split(',')]
            view_layer = scene.view_layers[0]
            
            file_out = tree.nodes.new('CompositorNodeOutputFile')
            file_out.base_path = str(Path(out_path).parent)
            file_out.file_slots.clear()
            
            for p in pass_list:
                if p == 'beauty': continue
                slot = file_out.file_slots.new(p)
                slot.path = Path(out_path).stem + f"_{p}_"
                
                if p == 'depth':
                    view_layer.use_pass_z = True
                    if rl.outputs.get('Depth'):
                        tree.links.new(rl.outputs.get('Depth'), file_out.inputs[p])
                    file_out.format.file_format = 'OPEN_EXR'
                elif p == 'normal':
                    view_layer.use_pass_normal = True
                    if rl.outputs.get('Normal'):
                        tree.links.new(rl.outputs.get('Normal'), file_out.inputs[p])
                elif p == 'ao':
                    view_layer.use_pass_ambient_occlusion = True
                    if rl.outputs.get('AO'):
                        tree.links.new(rl.outputs.get('AO'), file_out.inputs[p])

    # ------------------------------------------------------------------
    # GW-aware material fixup.
    #
    # Guild Wars reuses a diffuse texture's ALPHA channel as a spec/gloss/layer
    # mask on OPAQUE character draws (armor, skirts, hair). It is NOT framebuffer
    # opacity: the pixel shader outputs ~1.0 and the blend is SRCALPHA/INVSRCALPHA
    # only for edge AA. Proof from the captures: 72.6% of a skirt's sampled texels
    # are alpha==0 yet it is solid dark armor in-game. So importing map_d as the
    # Principled Alpha (which the .obj importer does) + alpha-hashing turns solid
    # armor into a near-invisible ghost -- the exact "transparent skirt/hair/cape".
    #
    # Effects (flames, auras, glows) are the opposite: GW draws them ADDITIVELY
    # (dest_blend == D3DBLEND_ONE), where black adds nothing and so must read as
    # transparent. Their alpha channel is a meaningless 255, so map_d cannot key
    # out the black -- the "black in flame should not render" bug.
    #
    # We recover the intent per material from the sidecar capture .json (authored
    # blend state per draw) and rebuild the node graph:
    #   opaque   -> ignore the texture alpha, render solid
    #   additive -> emissive, black keyed transparent via a luminance alpha
    D3DBLEND_ONE = 2

    def _blend_classes(self):
        """Map sanitized texture stem -> 'opaque'|'additive' from the capture json."""
        import json, re
        classes = {}
        jp = Path(self.obj_path).with_suffix('.json')
        if not jp.exists():
            return classes
        try:
            data = json.loads(jp.read_text())
        except Exception:
            return classes
        san = lambda s: re.sub(r'[^0-9A-Za-z_]', '_', s)
        for c in data.get('chunks', []):
            tf = c.get('texture_file', '')
            if not tf:
                continue
            stem = san(Path(tf).stem)
            additive = (c.get('dest_blend') == self.D3DBLEND_ONE) or bool(c.get('is_effect'))
            # additive wins if ANY draw sharing the texture is additive
            if classes.get(stem) == 'additive' or additive:
                classes[stem] = 'additive'
            else:
                classes.setdefault(stem, 'opaque')
        return classes

    def _class_for(self, matname, classes):
        base = matname[4:] if matname.startswith('mat_') else matname
        if base.endswith('_fx'):         # new exporter: additive-effect variant
            base = base[:-3]
        elif base.endswith('_a'):        # legacy exporter: alpha-blended variant of a shared atlas
            base = base[:-2]
        return classes.get(base, 'opaque')

    @staticmethod
    def _set_render_method(mat, blended):
        # Blender 4.2+/5.0 EEVEE-Next uses surface_render_method; older uses blend_method.
        if hasattr(mat, 'surface_render_method'):
            try: mat.surface_render_method = 'BLENDED' if blended else 'DITHERED'
            except Exception: pass
        if hasattr(mat, 'blend_method'):
            try: mat.blend_method = 'BLEND' if blended else 'OPAQUE'
            except Exception: pass

    def _setup_materials(self):
        classes = self._blend_classes()
        self._additive_images = set()
        n_add = n_op = 0
        for mat in bpy.data.materials:
            if not mat.use_nodes or mat.node_tree is None:
                continue
            nt = mat.node_tree
            bsdf = nt.nodes.get("Principled BSDF")
            if bsdf is None:
                continue
            bc = bsdf.inputs.get('Base Color')
            alpha_in = bsdf.inputs.get('Alpha')
            img_node = None
            if bc is not None and bc.is_linked:
                img_node = bc.links[0].from_node
            if img_node is None or img_node.type != 'TEX_IMAGE':
                img_node = next((n for n in nt.nodes if n.type == 'TEX_IMAGE'), img_node)

            cls = self._class_for(mat.name, classes)

            if cls == 'additive' and img_node is not None:
                emis = bsdf.inputs.get('Emission Color') or bsdf.inputs.get('Emission')
                if emis is not None:
                    try: nt.links.new(img_node.outputs['Color'], emis)
                    except Exception: pass
                if 'Emission Strength' in bsdf.inputs:
                    bsdf.inputs['Emission Strength'].default_value = 1.5
                if bc is not None:
                    for l in list(bc.links): nt.links.remove(l)
                    bc.default_value = (0.0, 0.0, 0.0, 1.0)
                if alpha_in is not None:
                    for l in list(alpha_in.links): nt.links.remove(l)
                    # alpha = sharpened luminance: true black (the additive void) keys
                    # fully transparent, but any lit colour (flame, lava body) ramps to
                    # solid fast so it does not wash out. Map 0.03..0.32 -> 0..1 clamped.
                    bw = nt.nodes.new('ShaderNodeRGBToBW')
                    nt.links.new(img_node.outputs['Color'], bw.inputs['Color'])
                    mr = nt.nodes.new('ShaderNodeMapRange')
                    mr.clamp = True
                    mr.inputs['From Min'].default_value = 0.03
                    mr.inputs['From Max'].default_value = 0.32
                    mr.inputs['To Min'].default_value = 0.0
                    mr.inputs['To Max'].default_value = 1.0
                    nt.links.new(bw.outputs['Val'], mr.inputs['Value'])
                    nt.links.new(mr.outputs['Result'], alpha_in)
                self._set_render_method(mat, blended=True)
                for attr, val in (('shadow_method', 'NONE'), ('use_transparent_shadow', False)):
                    if hasattr(mat, attr):
                        try: setattr(mat, attr, val)
                        except Exception: pass
                if getattr(img_node, 'image', None) is not None:
                    self._additive_images.add(img_node.image)
                n_add += 1
            else:
                # opaque: strip the bogus opacity link, render solid
                if alpha_in is not None:
                    for l in list(alpha_in.links): nt.links.remove(l)
                    alpha_in.default_value = 1.0
                self._set_render_method(mat, blended=False)
                n_op += 1
        print(f"[Materials] opaque={n_op} additive={n_add} (classes={len(classes)} from json)")

    def render(self, template_name, out_path, overrides):
        template = TEMPLATES.get(template_name, TEMPLATES["diagnostic"])
        resolved = self._resolve_config(template, overrides)

        scene = bpy.context.scene
        scene.render.resolution_x = 600
        scene.render.resolution_y = 1000
        scene.render.image_settings.file_format = 'PNG'
        scene.render.filepath = out_path
        
        self._setup_engine(template, resolved)
        self._setup_transparency(resolved)
        
        cam_rig = CameraRig(self.center, self.size)
        cam_rig.apply_framing(resolved)
        
        lighting = LightingRig()
        lighting.setup(resolved)
        
        self._setup_overlays(resolved)
        self._setup_wireframe(template)
        self._setup_passes(resolved, out_path)

        self._setup_materials()

        bpy.ops.render.render(write_still=True)
        print(f"\n[Renderer] engine:\t{scene.render.engine}\n[Renderer] outfile:\t{out_path}")

    # ------------------------------------------------------------------
    # glTF / GLB export. Emits a single self-contained .glb (geometry + UVs +
    # normals + embedded textures + GW-correct materials) with the capture's
    # pose / scene / timing / skeleton manifest carried in glTF `extras` so an
    # external viewer (three.js) can read it from `gltf.scene.userData`.
    def _gltf_extras(self):
        """Curate the sidecar capture .json into a metadata blob for glTF extras."""
        import json
        jp = Path(self.obj_path).with_suffix('.json')
        if not jp.exists():
            return None
        try:
            data = json.loads(jp.read_text())
        except Exception:
            return None
        subj = data.get('subject', {}) or {}
        # Keep the whole manifest but drop the giant per-draw probe register dumps
        # from the flat convenience view (they stay available under 'probe').
        manifest = {
            'tool': data.get('tool'), 'version': data.get('version'),
            'timestamp': data.get('timestamp'), 'scope': data.get('scope'),
            'counts': {k: data.get(k) for k in
                       ('draws_captured', 'vertices', 'triangles', 'unique_textures')},
            'subject': subj,
            'pose': {
                'model_state': subj.get('model_state'),
                'animation_id': subj.get('animation_id'),
                'animation_code': subj.get('animation_code'),
                'animation_type': subj.get('animation_type'),
                'animation_speed': subj.get('animation_speed'),
            },
            'scene': {
                'map_id': subj.get('map_id'),
                'instance_type': subj.get('instance_type'),
                'instance_name': subj.get('instance_name'),
                'pos': [subj.get('pos_x'), subj.get('pos_y'), subj.get('pos_z')],
                'rotation': subj.get('rotation'),
            },
            'chunks': data.get('chunks'),   # per-draw blend/texture/aabb (for re-materialise)
            'probe': data.get('probe'),     # vertex-shader constant / bone palette (for re-rig)
            'notes': {k: data.get(k) for k in ('pose_note', 'probe_note') if data.get(k)},
        }
        return manifest

    def _bake_effect_alpha(self):
        """glTF PBR expresses opacity only as baseColorTexture.alpha * factor -- it cannot
        carry the luminance->alpha node graph we use for additive effects at render time.
        So for effect images bake the luminance key straight into the image's alpha channel
        (RGB/emissive untouched). GW effect textures ship a meaningless all-255 alpha, so
        this is non-destructive, and these images are used only by additive materials."""
        imgs = getattr(self, '_additive_images', None)
        if not imgs:
            return
        try:
            import numpy as np
        except Exception:
            np = None
        lo, hi = 0.03, 0.32   # matches the render-path MapRange: black -> clear, colour -> solid
        for img in imgs:
            try:
                w, h = img.size
                n = w * h
                if n == 0:
                    continue
                if np is not None:
                    px = np.empty(n * 4, dtype=np.float32)
                    img.pixels.foreach_get(px)
                    px = px.reshape(-1, 4)
                    lum = 0.299 * px[:, 0] + 0.587 * px[:, 1] + 0.114 * px[:, 2]
                    px[:, 3] = np.clip((lum - lo) / (hi - lo), 0.0, 1.0)
                    img.pixels.foreach_set(px.reshape(-1))
                else:
                    px = list(img.pixels)
                    for i in range(n):
                        lum = 0.299 * px[i*4] + 0.587 * px[i*4+1] + 0.114 * px[i*4+2]
                        px[i*4+3] = min(1.0, max(0.0, (lum - lo) / (hi - lo)))
                    img.pixels[:] = px
                img.update()
                print(f"[glTF] baked luminance alpha into effect image {img.name}")
            except Exception as e:
                print(f"[glTF] alpha bake skipped for {getattr(img,'name','?')}: {e}")

    def export_gltf(self, out_path, overrides):
        scene = bpy.context.scene
        self._setup_materials()
        self._bake_effect_alpha()

        manifest = self._gltf_extras()
        if manifest is not None:
            import json
            # Blender custom props -> glTF extras when export_extras=True. Store the
            # manifest as one compact JSON string (robust across nested types) plus a
            # couple of flat keys for convenience.
            blob = json.dumps(manifest, separators=(',', ':'))
            scene['guildlite_manifest'] = blob
            scene['guildlite_tool'] = manifest.get('tool') or 'guildlite'
            subj = manifest.get('subject') or {}
            for o in bpy.context.scene.objects:
                if o.type == 'MESH':
                    o['guildlite_manifest'] = blob
            print(f"[glTF] manifest attached ({len(blob)} bytes of extras)")

        # Y-up: our OBJ is authored Y-up (ObjWriter RemapUp), Blender imports to Z-up,
        # export_yup converts back to glTF's Y-up so the model stands correct in three.js.
        kwargs = dict(
            filepath=out_path,
            export_format='GLB',
            export_texcoords=True,
            export_normals=True,
            export_materials='EXPORT',
            export_yup=True,
            use_selection=False,
            export_extras=True,
        )
        try:
            kwargs['export_image_format'] = 'AUTO'   # embed textures (PNG/JPEG) in the .glb
        except Exception:
            pass
        # Blender rev-to-rev renames some flags; retry without the optional ones.
        try:
            bpy.ops.export_scene.gltf(**kwargs)
        except TypeError:
            for opt in ('export_extras', 'export_image_format', 'export_yup'):
                kwargs.pop(opt, None)
            bpy.ops.export_scene.gltf(**kwargs)
        import os
        size = os.path.getsize(out_path) if os.path.exists(out_path) else 0
        print(f"\n[glTF] outfile:\t{out_path}\n[glTF] size:\t{size} bytes")

    def animate(self, template_name, out_path, overrides):
        template = TEMPLATES.get(template_name, TEMPLATES["orbit-sun"])
        resolved = self._resolve_config(template, overrides)

        scene = bpy.context.scene
        scene.render.resolution_x = 600
        scene.render.resolution_y = 1000
        
        self._setup_engine(template, resolved)
        self._setup_transparency(resolved)
        
        cam_rig = CameraRig(self.center, self.size)
        cam_rig.apply_framing(resolved)
        
        lighting = LightingRig()
        lighting.setup(resolved)
        
        self._setup_overlays(resolved)
        self._setup_wireframe(template)
        self._setup_materials()

        fps = overrides.get('fps', 30)
        duration_ms = overrides.get('time', 1000)
        total_frames = int((duration_ms / 1000.0) * fps)
        
        scene.render.fps = fps
        scene.frame_start = 1
        scene.frame_end = total_frames
        
        # Insert keyframes once
        dist = self.maxdim * resolved.get('orbit_radius', 2.0)
        for obj in scene.objects:
            if obj.type == 'LIGHT' and obj.name.startswith("orbit_light"):
                # Scale energy physically by inverse square law
                obj.data.energy = 500.0 * max(1.0, dist ** 2)
                for frame in range(1, total_frames + 1):
                    scene.frame_set(frame)
                    angle = (frame - 1) / total_frames * 2 * math.pi
                    lx = self.center.x + math.sin(angle) * dist
                    ly = self.center.y - math.cos(angle) * dist
                    lz = self.center.z # Same height as center of object
                    obj.location = (lx, ly, lz)
                    obj.keyframe_insert(data_path="location", frame=frame)
            elif obj.type == 'LIGHT' and obj.name.startswith("sun"):
                for frame in range(1, total_frames + 1):
                    scene.frame_set(frame)
                    angle = (frame - 1) / total_frames * 2 * math.pi
                    obj.rotation_euler = (math.radians(45), 0, angle)
                    obj.keyframe_insert(data_path="rotation_euler", frame=frame)
        
        fmt = overrides.get('format', 'mp4')
        try:
            if fmt == 'gif':
                raise TypeError("Force external ffmpeg for GIF")
                
            scene.render.image_settings.media_type = 'VIDEO'
            if fmt == 'mp4':
                scene.render.ffmpeg.format = 'MPEG4'
                scene.render.ffmpeg.codec = 'H264'
            elif fmt == 'webm':
                scene.render.ffmpeg.format = 'WEBM'
                scene.render.ffmpeg.codec = 'WEBM'
                
            scene.render.filepath = out_path
            bpy.ops.render.render(animation=True)
            
        except TypeError:
            import tempfile
            import subprocess
            print("Warning: FFMPEG not found in Blender. Rendering image sequence and stitching via external ffmpeg...")
            
            with tempfile.TemporaryDirectory() as tmpdir:
                scene.render.image_settings.file_format = 'PNG'
                scene.render.filepath = os.path.join(tmpdir, "frame_")
                bpy.ops.render.render(animation=True)
                
                if fmt == 'mp4':
                    cmd = [
                        "ffmpeg", "-y", "-r", str(fps),
                        "-i", os.path.join(tmpdir, "frame_%04d.png"),
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", out_path
                    ]
                elif fmt == 'webm':
                    cmd = [
                        "ffmpeg", "-y", "-r", str(fps),
                        "-i", os.path.join(tmpdir, "frame_%04d.png"),
                        "-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", out_path
                    ]
                elif fmt == 'gif':
                    cmd = [
                        "ffmpeg", "-y", "-r", str(fps),
                        "-i", os.path.join(tmpdir, "frame_%04d.png"),
                        "-filter_complex", "[0:v] split [a][b];[a] palettegen=reserve_transparent=on:transparency_color=ffffff [p];[b][p] paletteuse",
                        "-loop", "0", out_path
                    ]
                
                try:
                    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except FileNotFoundError:
                    print(f"Error: 'ffmpeg' is not installed on the system! Cannot generate {out_path}.")
                except subprocess.CalledProcessError as e:
                    print(f"Error: ffmpeg failed to stitch the animation: {e}")
        print(f"\n[Renderer] engine:\t{scene.render.engine}\n[Renderer] Animated output:\t{out_path}")


def generate_contact_sheet(images, output_path):
    print(f"Creating contact sheet {output_path}...")
    try:
        cmd = ["montage", "-geometry", "+0+0", "-tile", "4x"] + images + [output_path]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("Contact sheet created via ImageMagick.")
    except Exception:
        try:
            from PIL import Image
            imgs = [Image.open(i) for i in images]
            cols = min(4, len(imgs))
            rows = math.ceil(len(imgs)/cols)
            w, h = imgs[0].size
            grid = Image.new('RGB', size=(cols*w, rows*h))
            for i, img in enumerate(imgs):
                grid.paste(img, box=(i%cols*w, i//cols*h))
            grid.save(output_path)
            print("Contact sheet created via PIL.")
        except Exception as e:
            print(f"Warning: Contact sheet generation failed. Need ImageMagick or PIL. ({e})")

# ==============================================================================
# Phase 2 - CLI
# ==============================================================================

def parser():
    shared_args = argparse.ArgumentParser(add_help=False)
    shared_args.add_argument("-t", "--template", choices=list(TEMPLATES.keys()) + ["orbit-sun"], default="diagnostic", help="Render template")
    shared_args.add_argument("-o", "--output", help="Output file (render/animate) or output directory (batch)")
    
    shared_args.add_argument("--distance", type=float, help="Camera distance multiplier")
    shared_args.add_argument("--lens", type=float, help="Camera focal length (mm)")
    shared_args.add_argument("--orthographic", action="store_true", help="Use orthographic camera")
    shared_args.add_argument("--fisheye", action="store_true", help="Use fisheye camera")
    
    shared_args.add_argument("--yaw", type=float, help="Yaw angle (orbit around center horizontally)")
    shared_args.add_argument("--pitch", type=float, help="Pitch angle (orbit around vertically) (deg)")
    shared_args.add_argument("--roll", type=float, help="Roll angle (orbit around origin to camera) (deg)")
    
    shared_args.add_argument("-bg", "--background", help="Override background color (csv of RGB or RGBA floats, e.g. 1,0,0,0.5)")
    shared_args.add_argument("-env", "--env-strength", type=float, default=1.0, help="Environment/Ambient lighting strength")
    shared_args.add_argument("--light_rig", "--light-rig", "--light", choices=["sun", "studio", "three-point", "dramatic", "inspection", "orbit-sun"], help="Override lighting rig")
    shared_args.add_argument("--passes", default="beauty", help="Comma-separated list of passes (e.g. beauty,depth,normal,ao)")    

    shared_args.add_argument("--frame", choices=["auto", "full", "torso", "head", "feet"], default="auto", help="Framing/Composition focus")
    
    shared_args.add_argument("--overlay", action="store_true", help="Add diagnostic text overlay to output image")

    p = argparse.ArgumentParser(
        description="Blender Render V2 Pipeline",
        epilog="Note: When running via Blender, separate your script arguments with '--' (e.g. `blender --python script.py -- --help`)."
    )
    sp = p.add_subparsers(dest="cmd", required=True)
    
    # render
    r = sp.add_parser("render", parents=[shared_args], help="Render a single model")
    r.add_argument("obj", help="Path to .obj file")
    
    # batch
    b = sp.add_parser("batch", parents=[shared_args], help="Batch render multiple models or angles")
    b.add_argument("inputs", nargs="+", help="Paths to .obj files or directories containing .obj files")
    b.add_argument("--angles", default="0,60,120,180,240,300", help="Comma-separated yaw angles (e.g. 0,45,90,135,180,225,270,315)")
    b.add_argument("--contact-sheet", action="store_true", help="Generate contact sheet from batch")
    
    # animate
    a = sp.add_parser("animate", parents=[shared_args], help="Render an animated loop")
    a.add_argument("obj", help="Path to .obj file")
    a.add_argument("--fps", type=int, default=30, help="Frames per second")
    a.add_argument("--time", type=int, default=1000, help="Duration in milliseconds")
    a.add_argument("--format", choices=['mp4', 'webm', 'gif'], default='mp4', help="Output format (webm and gif support transparency and auto-looping in many platforms)")
    a.add_argument("--orbit-radius", type=float, default=2.0, help="Radius multiplier for orbit animations (default: 2.0)")

    # gltf
    g = sp.add_parser("gltf", parents=[shared_args], help="Export a self-contained .glb (objtex + GW-correct materials + pose/scene manifest in extras) for three.js")
    g.add_argument("obj", help="Path to .obj file")

    # templates
    sp.add_parser("templates", help="List available templates")

    return p


def main():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        executable = os.path.basename(sys.argv[0]).lower()
        if executable.startswith("blender"):
            argv = []
        else:
            argv = sys.argv[1:]

    if not argv:
        parser().print_help()
        sys.exit(1)

    args = parser().parse_args(argv)

    if args.cmd == "templates":
        print(f"{'Name':<15} {'Engine':<20} {'Frame':<10} {'Light':<15}")
        print("-" * 65)
        for k, v in TEMPLATES.items():
            print(f"{k:<15} {v.engine:<20} {v.default_frame:<10} {v.light_rig:<15}")
        return

    if args.cmd in ("render", "batch", "animate", "gltf"):
        if 'bpy' not in sys.modules:
            print("Error: Must run inside Blender.")
            sys.exit(1)
            
        bg_val = None
        if args.background:
            try:
                parts = [float(x.strip()) for x in args.background.split(",")]
                if len(parts) == 3:
                    parts.append(1.0)
                if len(parts) != 4:
                    raise ValueError("Background must have 3 or 4 components.")
                bg_val = tuple(parts)
            except ValueError:
                print(f"Error parsing --background '{args.background}': Must be a comma-separated list of 3 or 4 floats (e.g., '1,0,0' or '0.5,0.5,0.5,0').")
                sys.exit(1)
            
        overrides = {
            'distance': args.distance,
            'lens': args.lens,
            'orthographic': args.orthographic if args.orthographic else None,            
            'fisheye': args.fisheye if args.fisheye else None,
            
            'yaw': args.yaw,
            'pitch': args.pitch,
            'roll': args.roll,

            'light_rig': args.light_rig,
            'frame': args.frame,            
            'passes': args.passes,
            'background': bg_val,
            'env_strength': args.env_strength,
            
            'overlay': args.overlay if args.overlay else None
        }
        overrides = {k: v for k, v in overrides.items() if v is not None}

        if args.cmd == "render":
            if args.output:
                out_p = Path(args.output)
                if out_p.is_dir() or args.output.endswith('/'):
                    out_path = str(out_p / Path(args.obj).with_suffix(".png").name)
                else:
                    out_path = str(out_p)
            else:
                out_path = str(Path(args.obj).with_suffix(".png"))
                
            print(f"  {args.obj}\n > {out_path}")
            print(f"[Template]: {args.template}")
            Renderer(args.obj).render(args.template, out_path, overrides)

        elif args.cmd == "gltf":
            if args.output:
                out_p = Path(args.output)
                if out_p.is_dir() or args.output.endswith('/'):
                    out_path = str(out_p / Path(args.obj).with_suffix(".glb").name)
                else:
                    out_path = str(out_p)
            else:
                out_path = str(Path(args.obj).with_suffix(".glb"))
            print(f"  {args.obj}\n > {out_path}")
            Renderer(args.obj).export_gltf(out_path, overrides)

        elif args.cmd == "animate":
            overrides['fps'] = args.fps
            overrides['time'] = args.time
            overrides['format'] = args.format
            overrides['orbit_radius'] = args.orbit_radius
            
            if args.output:
                out_p = Path(args.output)
                if out_p.is_dir() or args.output.endswith('/'):
                    out_path = str(out_p / Path(args.obj).with_suffix("." + args.format).name)
                else:
                    out_path = str(out_p)
            else:
                out_path = str(Path(args.obj).with_suffix("." + args.format))
                
            print(f"[Animating] {args.obj} -> {out_path} ({args.time}ms @ {args.fps}fps)")
            print(f"[Template]: {args.template}")
            Renderer(args.obj).animate(args.template, out_path, overrides)

        elif args.cmd == "batch":
            out_dir = None
            if args.output:
                out_dir = Path(args.output)
                out_dir.mkdir(parents=True, exist_ok=True)
                
            models = []
            for inp in args.inputs:
                p = Path(inp)
                if p.is_dir():
                    models.extend(list(p.glob("*.obj")))
                elif p.is_file() and p.suffix.lower() == '.obj':
                    models.append(p)
                    
            angles = [float(x.strip()) for x in args.angles.split(",")]
            
            for model in models:
                renderer = Renderer(str(model))
                rendered_images = []
                
                for angle in angles:
                    angle_overrides = overrides.copy()
                    angle_overrides['yaw'] = angle
                    
                    suffix = f"_{int(angle)}deg.png"
                    if out_dir:
                        out_path = str(out_dir / (model.stem + suffix))
                    else:
                        out_path = str(model.with_name(model.stem + suffix))
                        
                    print(f"[Batch rendering] {model} at {angle} deg -> {out_path}")
                    renderer.render(args.template, out_path, angle_overrides)
                    rendered_images.append(out_path)
                    
                if args.contact_sheet and rendered_images:
                    sheet_path = str(model.with_name(model.stem + "_contact_sheet.png"))
                    if out_dir:
                        sheet_path = str(out_dir / (model.stem + "_contact_sheet.png"))
                    generate_contact_sheet(rendered_images, sheet_path)

if __name__ == "__main__":
    main()
