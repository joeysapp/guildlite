# ModelMod Reference — Findings for Model Export

An investigation of the `modelmod/` submodule (jmquigs/ModelMod) against our current
live-D3D9 exporter, aimed at the active goal: *press one button, get a clean, complete
target export with no noise.* Read: the dev/user guides, `DEVNOTES.md`, the Rust snapshot
hook (`Native/hook_snapshot`), the F# snapshot core (`MMManaged/Snapshot.fs`), snapshot
profiles, and the Blender importer — cross-referenced against `injector/src/{Capture,ObjWriter,Exporter}`
and `renders/MODEL-EXPORT-SETTINGS.md`.

## TL;DR — the verdict

- **We did not go about exporting wrong.** ModelMod uses the *identical* mechanism we do:
  hook `DrawIndexedPrimitive`, grab the bind-pose, model-local vertex/index buffers + vertex
  declaration at snapshot time. There is no secret DAT path in it to leapfrog to. Its `.dat`
  files are its own binary decl/IB/VB dumps, not `Gw.dat` assets. The live-capture approach is
  *validated* by a mature reference tool.
- **The real leapfrog is an architecture inversion, not a new capture method:** *select one
  draw call, don't capture the whole scene and filter it down.* ModelMod never has our
  "isolate one agent from a crowd stacked at the origin" problem because **it never captures the
  crowd.** This is the direct path to the reliable single-button export.
- **The two hardest things on our roadmap — per-agent isolation and animation — ModelMod
  already hit, named, and partially solved**, and we are *better positioned* than it was
  (we have GWCA; it needed an external DLL for the same data).
- **DX9 reference point:** the submodule HEAD carries *both* DX9 and DX11; DX9 wasn't removed.
  For a pure-DX9 read, check out the tag **`stable-dx9-2023`**. But the snapshot logic that
  matters is renderer-agnostic and readable on HEAD as-is.

---

## 1. We did not go about exporting wrong

ModelMod's snapshot (`Native/hook_snapshot/src/hook_snapshot.rs` → `MMManaged/Snapshot.fs`) does
exactly what `injector/src/Capture.cpp` does:

1. Hook the D3D9 draw path.
2. On the snapshot key, read the currently-bound vertex declaration (`GetVertexDeclaration`),
   index buffer (`GetIndices`), and stream-0 vertex buffer.
3. Decode positions / UVs / normals / **blend indices / blend weights** per the declaration.
4. Apply an orientation transform and write geometry + textures.

Same bind-pose, model-local reality we documented in `MODEL-EXPORT-SETTINGS.md`: the geometry is
the artist's authored mesh at the origin; the per-agent world position, rotation and skeleton live
in the vertex-shader constant registers, applied on the GPU *after* the buffer we read. ModelMod
lives with this too — its entire animation story (below) is about coping with skinning-in-the-shader.

**So the vertex-shader approach is not a wrong turn.** DAT parsing remains a genuinely separate,
larger project (correctly parked as future work on our roadmap). ModelMod offers nothing there —
it is a live-capture tool, full stop.

## 2. The leapfrog — select a draw, don't filter a scene

This is the single most important finding.

**Our model:** arm a frame → capture *every* triangle-list draw → run a stack of heuristics to
guess which draws are "me": `filter_max_extent`, `filter_center_radius`, `require_texture`,
`filter_min_thickness`, `require_skinned`, `drop_effects`, `isolate_by_bone` + tolerance + probe +
per-map bone-palette calibration. Every one of these exists to *reconstruct*, unreliably, an answer
to "which draws did the user mean?"

**ModelMod's model:** the user visually selects the mesh in-game first. It keeps a list of recently
used textures; the user pages through them and the selected one is swapped for a bright green
texture so the target is highlighted on screen. The snapshot key then grabs **only the single draw
call currently bound to that selection** — the `this_is_selected` gate in `take()`
(`hook_snapshot.rs:114,139`). No scene capture, no isolation, no bone-palette math, no tolerance
tuning. You point at the thing and grab the thing.

Our own `ROADMAP.md:37` states the pain exactly — *"the ability to press a button and get your
target/scene reliably without having to double check … some workaround potentially wildly move like
intercepting/MiTM movement packets while trying to get a photo of yourself."* That instinct is to
physically separate agents *in the world* to beat origin-overlap. **ModelMod shows you separate them
at the draw-call level instead — interactively, for free.** No packet MITM, no walking away from the
crowd.

This is the reliable single-button export: **highlight → one key → exactly that mesh.** It very
likely obsoletes most of the filter stack for the solo-target case.

## 3. Granularity — character vs. armor vs. weapon

Answering the open question ("could we export a character + separated armor + weapon at once?"):

A GW character is drawn as **multiple draw calls** — body, each armor piece, face, hair, weapon are
each their own draw with their own texture/material. Consequences:

- **One clean piece** = snap one draw (the ModelMod path, perfectly clean).
- **Whole character as *separated* sub-meshes** = snap several draws / accumulate over a short
  window. The pieces come apart *for free* because they were never joined — the opposite of the
  fight we're having now. "Character + separated armor + weapon at once" is natural, not hard.
- ModelMod's snapshot already spans a **time window** (`snap_ms`, default 250ms) rather than one
  draw, precisely because a single begin/end scene "often misses data, and sometimes fails to
  snapshot anything at all" (`hook_snapshot.rs:50-60`). That window is how it accumulates the pieces.

Two capture modes worth offering, then:

1. **Solo-piece (interactive select):** the reliable single-button path. Highlight one draw, snap it.
2. **Whole-agent (accumulate by identity):** snap every draw sharing the target's bone palette within
   the window — our existing `isolate_by_bone` **reframed as *accumulate matching draws* rather than
   *filter a scene*.** Same signal, inverted use: it builds up the character instead of tearing down
   the scene.

## 4. Isolation and the world→object transform — already solved, and we're ahead

Our `isolate_by_bone` reads the vertex-shader constant registers looking for a bone-palette triple
whose translation lands near the target's GWCA world position, and `Probe` dumps `c0..cN` so we can
hand-solve which register that is (`Capture.h:91-94`: "c0-c3 hold view, c4-c7 projection; the
per-agent world/bone transform is somewhere beyond"). This is fragile hand-reverse-engineering
against one map's calibration.

ModelMod hit the **exact same problem** in its experimental animation path and documents the fix
verbatim (`hook_snapshot.rs:428-434`):

> *"the reference game I used for testing requires an external dll to obtain the player transform
> because the animation bones fed to the shader are in world space; this transform lets me map them
> back into object space."*

That world→object mapping is precisely what our tolerance dance is approximating. ModelMod solved it
with a small external DLL (`snap_extdll` / `XDLLSTATE::get_player_transform`) that hands back the
player's world transform, so it can subtract it out.

**We are better positioned than ModelMod was:** we already have GWCA, which gives us the player's /
agent's world transform natively — no external DLL, no game-specific hack. We can get the clean
world→object transform ModelMod had to bolt on. The isolation problem is more tractable for us than
it was for the reference.

Systematic instrument for the register hunt: ModelMod captures the **full vertex-shader constant set
`c0..c255`** *and the vertex-shader disassembly* (`_vshader.asm`) per snapshot, instead of a 6-sample
probe window. If we keep fighting bone-palette calibration across maps, capturing the shader ASM +
full constants once is the tool to nail the register layout properly rather than eyeballing it.

## 5. Animation substrate — we capture it, then throw it away

Concrete, cheap, high-value gap.

Our capture *detects* skinning: `Capture.cpp` sets `has_blend` from `D3DDECLUSAGE_BLENDWEIGHT /
BLENDINDICES` and `D3DFVF_XYZB*`. But it uses that only as the `is_skinned` *filter boolean* — the
actual per-vertex blend indices and weights are **read past and discarded.** `MeshChunk` never stores
them, and `ObjWriter.cpp` emits only `v / vt / vn / f`.

ModelMod **decodes and preserves** them: `Snapshot.fs` fills `blendIndices` / `blendWeights` arrays
per vertex and writes them into the `.mmobj` as `#vbld` comment lines, which the Blender importer
turns back into vertex groups. That plus the bone-palette matrices (which our `Probe` already reads
out of the shader constants) is the **entire raw substrate for a posed / rigged export.**

If animation/rigging is in scope: **stop discarding the bytes we already touch.** Store per-vertex
blend indices + weights on `MeshChunk`, emit them (OBJ comment lines à la ModelMod, or a sidecar
file). Low effort — we're already at the read cursor for that data.

Caveat from the reference: ModelMod deliberately **never reconstructs a real skeleton.** For mods it
weights new verts by nearest reference vert (`MeshRelation`), not by a rig. It confirms our own read
— a true skeleton from live memory is "not simple but doable," and *nobody in the reference tool got
there.* The material is capturable; the rig reconstruction is still net-new work if we want it.

## 6. GW compatibility watch-item — blend data hidden in COLOR channels

ModelMod carries snapshot-profile flags `BlendIndexInColor1` / `BlendWeightInColor2` and dedicated
`readBoneIndicesFromColor` / `readBoneWeightsFromColor` decoders (with a BGRA→RGBA swizzle). They
exist because **some D3D9 games smuggle bone indices/weights in COLOR semantic slots** rather than
proper `BLENDINDICES` / `BLENDWEIGHT`.

Our `require_skinned` keys *only* on `D3DFVF_XYZB*` / `BLENDWEIGHT|BLENDINDICES` usage. Our notes say
GW character verts are `XYZB1`, so we're probably fine — but **if we ever see a GW character mesh that
`require_skinned` wrongly drops, this is the first thing to check:** the blend data may be in a COLOR
channel. (This is also a second reason `require_skinned` is a fragile foundation, consistent with the
caveat already in `MODEL-EXPORT-SETTINGS.md`.)

## 7. The DX9 reference point (answering the roadmap NOTE)

The submodule HEAD (`main`) carries **both** renderers — `shared_dx/src/defs_dx9.rs` sits alongside
`hook_render_d3d11.rs`. DX9 was **not** removed; DX11 was added beside it. Tag lineage confirms the
history the NOTE guessed at:

- `1.0.0.x` — base DX9 (C++ native). `1.0.0.13-pre-rust-merge` marks the point before the native
  layer was rewritten C++ → Rust.
- `1.1.0.x` — 64-bit support.
- `1.2.0.x` — DX11 alpha (current line).
- **`stable-dx9-2023`** — the clean, tagged stable DX9 build. This is the one to `git checkout` if
  you want to read the DX9 hook/injection path without DX11 noise.

But you likely don't need to: the parts worth mining (the F# `Snapshot.fs` geometry decode, the
snapshot-profile system, the mmobj format, the Blender pair) are renderer-agnostic and read fine on
HEAD. Only the low-level hook/injection specifics are DX9-vs-DX11 forked, and those we already own
our own version of.

## 8. Cheap wins worth lifting

- **Data-driven snapshot profiles.** `SnapshotProfiles.yaml` externalizes orientation as
  `pos: ["rot x 90","rot y 180","scale 0.1"]`, `uv: ["flip y"]`. We hardcode the Z→Y up-axis remap
  and the `1 - v` UV flip. Externalizing them into a tiny config makes per-map/per-need orientation a
  tweak instead of a rebuild.
- **Snap over a time window, not one frame.** Their `snap_ms` window exists because one begin/end
  "often misses data." This may directly explain our "entire torso/bodies missing, slices of
  clothing" misses (`RENDER-FEEDBACK`, `ROADMAP.md:26`) — GW may split a character across draws we
  aren't all catching in a single armed frame. Worth testing an accumulate-over-N-ms capture.
- **GPU-anim sanity check.** They read the vertex-shader disassembly and abort if it's a plain
  `m4x4 oPos, v0, c0` (non-skinned transform) (`hook_snapshot.rs:249`). A cheap, definitive assertion
  that "this really is a skinned character draw" — more robust than inferring from vertex format.
- **mmobj + Blender import/export pair** (`BlenderScripts/io_scene_mmobj`). If we ever round-trip
  blend data through Blender, this is a working reference — and we already drive Blender via
  `tools/dev-tool/blender_render.py`, so it's close to home.

## 9. What NOT to chase

- **Not the stack.** Don't adopt ModelMod's F#/CLR + Rust split or its runtime. Our C++ / GWCA /
  ImGui injector is the right, lighter substrate. Mine *ideas*, not architecture. (Its `DEVNOTES.md`
  is a litany of paket/dotnet/FSharp.Core version hell and global-`static mut` UB — a tax we don't
  want.)
- **Not DAT export.** Still the big separate project; ModelMod doesn't do it.
- **Not (yet) the mod-*replacement* half.** Match-by-prim/vert-count, `GPUReplacement`, `MeshRelation`
  reprojection — that's for injecting *modified* art back into the game. It maps to the Prop Hunt /
  "change the player's own model_id/skeleton" / Scene Compositing direction on our roadmap, **not** the
  export goal. Worth remembering it exists when we get there: `MeshRelation` is exactly "weight a new
  mesh using the game's live skeleton without having the skeleton," which is the core trick that
  future direction needs.

---

## Suggested sequencing

1. **Before more dev-tool filter tuning: prototype select-one-draw capture.** Add draw/texture
   cycling (page through the armed frame's draws, highlight the current candidate by swapping/tinting
   its texture) + a snap-selected-draw path. Highest-leverage move; directly the reliable single
   button; likely retires most of the filter stack for solo targets.
2. **Reframe `isolate_by_bone` as accumulate-by-identity** for the whole-character mode, using GWCA's
   agent transform for the world→object mapping ModelMod needed an external DLL to get.
3. **If animation is in scope, stop discarding blend data** — store + emit per-vertex indices/weights;
   we already capture the palette via Probe.
4. **Adopt the cheap wins:** data-driven snapshot profile, time-window snap (may fix the missing-torso
   misses), shader-ASM GPU-anim check.
5. **Pivot the framing** (their model): the tool becomes a *world/scene renderer + interactive picker +
   exporter under one hood* — which is exactly the Scene Compositing / free-cam direction already on
   the roadmap. Select-and-snap is the same UX primitive as select-and-composite.
</content>
</invoke>
