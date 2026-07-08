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

-   Sun
-   Studio
-   Three-point
-   Inspection
-   Dramatic
-   Orbit

Future:

-   HDRI
-   Procedural sky
-   Animated lighting

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

# Guiding Principles

1.  Templates over conditionals.
2.  Deterministic rendering.
3.  Automation first.
4.  Human-friendly outputs.
5.  AI-friendly artifacts.
6.  Extensible architecture.
7.  Blender features exposed through a stable CLI rather than custom
    scripts.
