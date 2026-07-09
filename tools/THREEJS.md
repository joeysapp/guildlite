# Loading Guildlite `.glb` exports in three.js

The Extractor's glTF mode emits a **single self-contained `.glb`** per capture: geometry +
UVs + normals, all textures embedded (PNG), GW-correct PBR materials, and the capture's
pose / scene / timing / skeleton manifest carried in glTF `extras`. Nothing else is needed at
runtime — no sidecar `.mtl`, `.tga`, or `.json`.

## Export

```bash
# one model  ->  transparency/<id>.glb
blender --background --python tools/blender_render.py -- gltf transparency/<id>.obj -o out/<id>.glb
```

(`blender` = `/Applications/Blender.app/Contents/MacOS/Blender` on macOS.) The exporter reads
the sidecar `<id>.json` next to the `.obj` for blend classification + the manifest, so keep the
`.obj`, `.mtl`, `.tga`, and `.json` together when exporting. The resulting `.glb` is standalone.

## What the materials already encode (so you don't have to)

GW does not use a diffuse texture's alpha as opacity on character draws — it is a gloss/spec
mask — while effect draws (flames, auras) are **additive** (black = transparent). The exporter
resolves both at bake time, so a **stock `GLTFLoader` renders them correctly**:

| GW draw                     | glTF material                                                            | in three.js |
|-----------------------------|--------------------------------------------------------------------------|-------------|
| armour / skin / hair / skirt | `alphaMode: OPAQUE`, solid                                               | solid, no ghosting |
| flame / aura / glow (additive) | `alphaMode: BLEND`, `emissiveFactor:[1,1,1]` + `emissiveTexture`, alpha = baked luminance key | glows; black keyed out |

No custom shaders required. For fire/effect models a **dark or neutral background reads best**
(additive art is authored to add light over the dark game world), and you can push
`material.emissiveIntensity` up for a stronger glow.

## Minimal loader

```html
<script type="importmap">
{ "imports": {
    "three": "https://unpkg.com/three@0.169.0/build/three.module.js",
    "three/addons/": "https://unpkg.com/three@0.169.0/examples/jsm/"
} }
</script>
<script type="module">
import * as THREE from 'three';
import { GLTFLoader }    from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(innerWidth, innerHeight);
renderer.setPixelRatio(devicePixelRatio);
document.body.appendChild(renderer.domElement);

const scene  = new THREE.Scene();
scene.background = new THREE.Color(0x14151a);              // dark: good for both armour + fire
const camera = new THREE.PerspectiveCamera(45, innerWidth / innerHeight, 0.01, 1000);
const controls = new OrbitControls(camera, renderer.domElement);

scene.add(new THREE.HemisphereLight(0xffffff, 0x404040, 2.0));
const key = new THREE.DirectionalLight(0xffffff, 2.0); key.position.set(3, 5, 2); scene.add(key);

new GLTFLoader().load('out/<id>.glb', (gltf) => {
  const model = gltf.scene;
  scene.add(model);

  // --- read the Guildlite capture manifest from glTF extras ---
  const raw = model.userData?.guildlite_manifest;            // scene-level extras
  if (raw) {
    const m = JSON.parse(raw);
    console.log('subject', m.subject?.primary_name, '/', m.subject?.secondary_name);
    console.log('pose', m.pose, 'scene', m.scene);
    // m.chunks[] = per-draw blend/texture/aabb ; m.probe[] = bone-palette registers (re-rig)
  }

  // frame the camera on the model's bounds
  const box = new THREE.Box3().setFromObject(model);
  const size = box.getSize(new THREE.Vector3()).length();
  const c = box.getCenter(new THREE.Vector3());
  controls.target.copy(c);
  camera.position.copy(c).add(new THREE.Vector3(0, size * 0.1, size * 1.2));
  camera.near = size / 100; camera.far = size * 10; camera.updateProjectionMatrix();

  // optional: stronger fire
  model.traverse((o) => {
    if (o.isMesh && o.material?.emissiveIntensity !== undefined && o.material.emissive?.getHex())
      o.material.emissiveIntensity = 1.5;
  });
});

renderer.setAnimationLoop(() => { controls.update(); renderer.render(scene, camera); });
</script>
```

Serve over http (module scripts + fetch need it): `python3 -m http.server` then open the page.

## The manifest (`gltf.scene.userData.guildlite_manifest`, a JSON string)

Also attached to every mesh node's `userData` for convenience. Parse it with `JSON.parse`.

| field | meaning |
|-------|---------|
| `subject` | full capture subject: profession, level, sex, weapon, equipment, position, map |
| `pose` | `model_state`, `animation_id` / `animation_code` / `animation_type` / `animation_speed` — the frame the mesh was grabbed in (the pose is already baked into the vertex positions) |
| `scene` | `map_id`, `instance_type`/`instance_name`, world `pos`, `rotation` |
| `chunks[]` | per-draw record: `texture_file`, `alpha_blend`/`src_blend`/`dest_blend`/`is_effect`, `aabb_*` — enough to re-derive materials |
| `probe[]` | vertex-shader constant registers = the bone-palette transforms, for future re-rigging |
| `counts` | vertices / triangles / draws / unique textures |

### Note on animation
A capture is a **single live-frame snapshot**: the pose is baked into the vertex positions, so
the `.glb` displays the exact in-game stance with no armature. `pose` + `probe[]` (the bone
palette) are carried so a skinned rig can be reconstructed later; multi-frame animation would
require multi-frame capture and is not exported today.
