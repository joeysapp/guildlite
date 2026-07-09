# Blender Render V2 Roadmap

## Vision

Evolve the current Blender rendering script from a procedural diagnostic
renderer into a reusable rendering pipeline capable of generating:

-   Diagnostic renders
-   Presentation-quality renders
-   AI/ML datasets
-   Documentation imagery
-   Automated batch rendering

Core philosophy:

> Import object → Normalize scene → Apply template → Produce
> deterministic render artifacts.

------------------------------------------------------------------------

# Milestones

  -----------------------------------------------------------------------
  Version                 Theme                   Primary Deliverable
  ----------------------- ----------------------- -----------------------
  V2.0                    Architecture            Modular renderer
                                                  preserving current
                                                  functionality

  V2.1                    Production              Templates, lighting
                                                  rigs, camera rigs,
                                                  batch rendering

  V2.2                    Dataset Pipeline        Render passes, contact
                                                  sheets, manifests,
                                                  overlays

  V3.0                    Asset Studio            Config-driven
                                                  templates, asset
                                                  catalog generation
  -----------------------------------------------------------------------

------------------------------------------------------------------------

# Phase 0 --- Existing Renderer

Status: ✅ Complete

Capabilities: - OBJ import - Automatic camera fit - Neutral lighting -
PNG output - EEVEE / Workbench fallback

------------------------------------------------------------------------

# Phase 1 --- Architecture

## Goal

Separate rendering logic from Blender setup and CLI.

Deliverables:

-   cli.py
-   renderer.py
-   scene.py
-   camera.py
-   lighting.py
-   templates.py

Success criteria:

-   Rendering performed through a Renderer class.
-   New templates require editing only templates.py.

------------------------------------------------------------------------

# Phase 2 --- CLI

Deliverables

-   render
-   batch
-   templates

Representative commands:

``` bash
render npc.obj
render npc.obj --template portrait
batch models/
templates
```

Goals

-   Eliminate positional configuration arguments.
-   Automatic output naming.
-   Discoverable --help.

------------------------------------------------------------------------

# Phase 3 --- Template System

Templates become declarative objects describing:

-   Camera
-   Lighting
-   Background
-   Render engine
-   Framing

Initial templates:

-   diagnostic
-   portrait
-   ortho
-   clay
-   wireframe
-   turntable

------------------------------------------------------------------------

# Phase 4 --- Camera Framework

Features:

-   yaw
-   pitch
-   roll
-   focal length
-   orthographic
-   fisheye

Framing modes:

-   auto
-   full
-   torso
-   head
-   feet

------------------------------------------------------------------------

# Phase 5 --- Lighting Framework

Lighting rigs:

-   [ ] Sun
-   [ ] Dramatic
-   [ ] Orbit Animated lighting - animated output, simple orbititing sun in a circle above model or at level with model from a set distance away. A full animation loop is the sun orbiting in a full circle (2PI, etc.)
-   Studio
-   HDRI
-   Procedural sky
-   Three-point
-   Inspection

------------------------------------------------------------------------

# Phase 6 --- Batch Rendering

Support:

-   Multiple models
-   Multiple templates
-   Multiple angles

Future:

-   Parallel rendering
-   Recursive directories

------------------------------------------------------------------------

# Phase 7 --- Render Passes

Outputs:

-   Beauty
-   Depth
-   Normal
-   Ambient Occlusion
-   Albedo
-   Material ID
-   Object ID
-   Wireframe

------------------------------------------------------------------------

# Phase 8 --- Output Formats

Support:

-   PNG
-   EXR
-   JPEG
-   WebP

Multiple formats per render job.

------------------------------------------------------------------------

# Phase 9 --- Contact Sheets

Generate composite overview images showing multiple viewpoints.

Primary purpose:

-   Human inspection
-   GitHub documentation
-   Discord previews
-   AI review

------------------------------------------------------------------------

# Phase 10 --- Overlays

Optional overlays:

-   Filename
-   Template
-   Camera
-   Lighting
-   Axis widget
-   Triangle count
-   Material count
-   Timestamp

------------------------------------------------------------------------

# Phase 11 --- Configuration

Move templates into TOML configuration files.

    templates/
        portrait.toml
        clay.toml
        studio.toml

------------------------------------------------------------------------

# Phase 12 --- Asset Catalog

Generate complete render artifact directories:

``` text
npc_merchant/
├── beauty.png
├── portrait.png
├── contact_sheet.png
├── normals.png
├── depth.exr
├── metadata.json
└── manifest.json
```

Generate optional HTML catalog for browsing exported assets.

------------------------------------------------------------------------

# Phase 13 --- GW material fidelity + glTF export  (✅ shipped)

## GW-correct materials (fixes near-invisible armour + black effect panels)

The `.obj` importer wires a texture's alpha channel into Principled `Alpha`, but **GW does not
use diffuse alpha as opacity** on character draws — it is a gloss/spec/layer mask (the pixel
shader outputs ~1.0; the SRCALPHA blend is only edge AA). Proof from the captures: 72% of a
skirt's sampled texels are `alpha == 0`, yet it is solid dark armour in-game. Feeding that alpha
to alpha-hashing turned solid armour into a near-invisible ghost (the "transparent skirt / hair /
cape" bug). Conversely, effect draws (flames, auras) are **additive** (`dest_blend == D3DBLEND_ONE`)
where black adds nothing and must read as transparent — their alpha channel is a meaningless 255,
so the black exported as a solid panel (the "black in flame should not render" bug).

`Renderer._setup_materials()` recovers the intent per material from the sidecar capture `.json`
(authored blend state per draw) and rebuilds the node graph:

| class    | signal                              | material |
|----------|-------------------------------------|----------|
| opaque   | not additive                        | ignore texture alpha, render solid |
| additive | `dest_blend == ONE` or `is_effect`  | emissive; black keyed transparent by a luminance ramp |

Additive-dominant models (fire elementals, spirits) auto-default to a **dark background** unless
`-bg` is passed, because additive art only reads over darkness. The exporter (`ObjWriter.cpp`)
now emits the same classes directly into the `.mtl` (opaque = no `map_d`; additive = `_fx`
emissive), so fresh raw exports are correct in any viewer.

## `gltf` mode --- self-contained `.glb` for three.js

```bash
blender --background --python tools/blender_render.py -- gltf model.obj -o model.glb
```

Emits one `.glb`: geometry + UVs + normals + **embedded textures** + the GW-correct materials
above + the capture's pose / scene / timing / skeleton manifest in glTF `extras`. Because glTF
PBR can only express opacity as `baseColorTexture.alpha`, the additive luminance key is **baked
into the image alpha at export** so a stock `GLTFLoader` renders flames correctly with no custom
shader. Loading guide + manifest schema: **`tools/THREEJS.md`**.

------------------------------------------------------------------------

# Guiding Principles

1.  Templates over conditionals.
2.  Deterministic rendering.
3.  Automation first.
4.  Human-friendly outputs.
5.  AI-friendly artifacts.
6.  Extensible architecture.
7.  Blender features exposed through a stable CLI rather than custom
    scripts.
