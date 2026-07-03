# Model Export — Settings Rundown & Isolation Guide

A field guide to the export panel, written against the feedback in `FEEDBACK.md`.
It explains what every unclear setting actually does, why several of them "seem to
do nothing," and how to reliably pull one model out of a scene.

If you read nothing else, read the next section — it explains ~80% of the confusion.

---

## The one thing that explains everything: model-local space

Guild Wars skins characters **in a vertex shader**. That means the vertex buffers
we grab hold **bind-pose, model-local** coordinates — the mesh as the artist
authored it, centered on its own origin. The per-agent position, rotation and
skeleton live in the **shader constant registers**, applied on the GPU *after* we
read the geometry.

Consequences, and they are the root of nearly every question in the feedback:

1. **Every agent overlaps at the origin.** You, the person next to you, and an NPC
   across the outpost all land in the same ~13 × 13 × 90-unit box at (0,0,0) in the
   captured data. There is no spatial gap between them to filter on.
2. **The size/count/extent/radius filters operate in that model space.** They can
   drop *categories* of geometry (tiny sprites, terrain-sized meshes, far effect
   quads) but they **cannot tell two overlapping characters apart** — there is
   nothing positional to separate.
3. **Only one mechanism recovers per-agent identity: Isolation (bone-palette).** It
   reads the agent's world position back out of the shader constants and keeps a
   draw only if its bones sit near the agent you targeted. This is the *only* knob
   that separates one person from a crowd.

So: **filters thin a scene; Isolation picks a person.** Reaching for Max AABB or
Center radius to "grab just me" is fighting the architecture — that's why they felt
broken.

---

## Every setting, decoded

Ordered to match `FEEDBACK.md`. "Model space" = the captured bind-pose coordinates
above. "World space" = GW's live map coordinates (what GWCA reports as `pos_*`).

### Record armor slots / Record weapon slots
**Manifest-only. Does not affect rendering or which geometry is exported.** These
write each equipment slot's `model_file_id` + dye into the JSON manifest for
provenance. They were mislabeled in your notes as "used in model rendering?" — no.
A live grab is always the whole visible body; per-slot geometry gating is DAT-loader
work on the roadmap. Helper text now says this in-panel.

### Max AABB extent (`filter_max_extent`)
Drops any single draw whose **model-space** bounding box is larger than the value
(longest axis, world units). Good for culling terrain-sized meshes. **It cannot
isolate a person** — every character is the same ~90u-tall box at the origin, so no
extent threshold separates you from a neighbor.
- Your pink-mask case ("AABB 50 → only cape; AABB 200 → same"): raising this admits
  *larger* chunks, but the chunks you wanted were being dropped by a *different*,
  more aggressive filter (Isolation at tolerance 25 — see below), so loosening a
  non-binding filter changed nothing. The new `iso=` diagnostic makes this visible.

### Center radius (`filter_center_radius`)
Drops a draw whose center is farther than this (world units) from the **scene's
robust center** (the median of all kept draw centers), in model space. Needs ≥ 4
draws to have a stable center. Useful for shaving off a detached prop or a terrain
skirt that survived the other filters.

Two things made it feel broken, both now addressed:
- **It was silently coupled to "Trim outlier fliers."** The radius test lived
  *inside* the trim pass, so if you had Trim off it did nothing at all. **Fixed** —
  radius now runs in Filtered scope whether or not Trim is on.
- **It is measured from the scene center, not from you.** You expected "set radius,
  see fewer matches near me." But there is no "me" in model space to measure from —
  all agents are at the origin. Radius culls geometry that is spatially *stray*
  within the capture, not geometry belonging to other people. For "just me," use
  Isolation.

### Require a bound texture (`require_texture`)
Drops draws that have **no stage-0 texture bound** — typically a handful of
glow/effect quads. It works (in Filtered scope), but **almost all world geometry is
textured**, so in practice it removes very little and looks inert. It is a
tidy-up, not an isolation tool. Helper text now says so.

### Drop flat draws < thickness (`filter_min_thickness`)  ← billboard/HUD killer
Drops any draw whose **thinnest** model-space dimension is under this many units.
Billboards, ground decals, floating combat text and nameplates are authored as
**exactly flat** 2-triangle quads (thickness `0.00`); a real mesh always has some
thickness, so `~1–2` removes the flat class without touching geometry. This is the
tool for the **"my export is full of UI/flat sheets"** problem — see the
depth-test-off section below. Because it runs in the capture hook *before* Trim, it
also stops a flood of billboards from hijacking the trim center and silently
trimming your actual body. Validated on a real capture: `thickness ≥ 1.0` dropped
80 billboard quads and kept the 7 real body chunks (60u-tall, 200–300 tris each).

### Require skinned mesh (`require_skinned`)  ← the "a character, not scenery" filter
Keeps only meshes whose **vertex format carries bone blend weights** (`D3DFVF_XYZB*`
/ `BLENDWEIGHT`). GW skins characters, so their vertices have blend data; **static
world geometry and props do not** — they're plain `XYZ`. This is the definitive fix
for **"it exported a nearby building/statue instead of me."** Isolation can't help
there (a prop sitting next to you is *within* tolerance, so it's kept); this drops
static geometry outright regardless of where it is.

Validated on your own capture: of 17 chunks, the 13 skinned ones are your body
(`XYZB1`, AABB `40 × 64 × 80`); the 4 dropped are the structure + rigid panels
(`XYZ`, the thing that blew your box out to `62 × 65 × 200`). **For a solo self-
capture, this alone gives just your body — no isolation needed.** Trade-off: rigidly
attached items (some weapons) are `XYZ` too and get dropped; they remain in the
manifest's equipment list. The manifest now records `is_skinned` per chunk.

**Caveat — it is a class filter, not an isolation filter, and not a foundation.**
`require_skinned` answers "is this a character?" not "is this *my* character?": in a
crowd it keeps the *whole crowd* (everyone is skinned), and it structurally **drops every
static prop/object/scenery mesh** — exactly the stationary content that is otherwise the
easiest to capture. Use it as a convenience toggle when you are already solo and want the
terrain gone, not as the thing a clean capture is built on. For solo self-capture prefer
the `clean-solo` recipe below (require-skinned OFF + `drop_effects`), which leaves static
attachments intact and does not pretend the crowd problem away.

### Drop effect planes (`drop_effects`)  ← the black-panel killer
Drops **additive/"screen" effect draws** — enchant auras, glows, weapon trails, buff
sparkles. GW blends these onto the framebuffer with an additive destination factor
(`DEST=ONE` or `INVSRCCOLOR`), so their **black texture background is invisible in-game**.
A mesh export has no such blending, so every one of them lands as a **solid black panel**
stuck to the model (the forearm/shoulder slabs you saw on every armored capture; a pure
grab of nothing-but-these is the "black rectangles with a glowing core" case). Detected
from the live D3D blend mode (`is_effect`), so it is pose- and skin-independent, and it
**keeps legit alpha cutouts** (hair/cape/feather use `DEST=INVSRCALPHA`). The manifest now
records `src_blend`/`dest_blend`/`is_effect` per chunk even when the filter is off, so the
rule can be verified/tuned from one capture. This is THE fix for "black panels on my model,"
and the piece that lets `require_skinned` be turned off without the effects flooding back.
Watch it work via the **`effect=` counter** in Diagnostics.

### Isolate to Source agent (`isolate_by_bone`)  ← the real isolation
Keeps a **skinned** draw only if one of its bone-palette registers holds a **world
position within Match tolerance** of the Source agent's GWCA position. This is the
one filter that separates agents. It *is* fully wired: on Export/Refresh the panel
seeds the render thread with the Source's live `pos_*`, and the hook scans the
constant registers (`c0..c95`) for a `[3×3 | translation]` bone triple whose
translation lands near that position.

Why it may have felt like "does nothing," and how to tell which:
- **You couldn't see it working.** Isolation drops used to be lumped into the
  generic `filtered` counter. There is now a dedicated **`iso=` count** in
  Diagnostics. If it's > 0, isolation is actively dropping other-agent draws.
- **`iso=` stays 0 with Isolate on** → the bone palette didn't match on this
  map/shader (the calibration was taken on map 481; a different layout is possible).
  Turn on **Probe**, export once, and check the manifest `probe[]` for the register
  whose translation tracks `subject.pos_*`.
- **It only touches skinned draws.** A static prop that happens to carry a world
  matrix in its shader can still slip through, and non-skinned terrain is untouched
  — pair Isolation with Max AABB / Require texture to clean those up.

### Match tolerance (`isolate_tolerance`)
The radius (world units) for the bone match above. Rule of thumb **~250** — smaller
than the spacing between agents, larger than one agent's own bone spread.
- **Too small (e.g. 25):** you can clip your own body down to a stray piece. Your
  "only the cape exported" grab used tolerance 25 — the cape's bone landed inside
  25u, most of the body didn't, so everything else was dropped. That's the mechanism
  behind the single-banner render.
- **Too large (e.g. 2500):** neighbors and nearby props fall back inside the sphere.
  Your "grabbed a nearby object, not me" grab used 2500 — wide enough that a static
  Canthan well-shrine within 2500u survived while your own draws got trimmed as the
  minority. Keep it near 250.
- **"0 vs 250 looked identical"** is the tell that Isolation wasn't matching at all
  on that capture (see `iso=` above) — tolerance only matters once something matches.

### Probe: dump shader constants (`probe_shader_constants`)
**Isolation calibration only — not textures, not skin color.** (Your guess tied it
to the TGA color issue; unrelated.) It writes the raw vertex-shader constants
`c0..c95` of the first few skinned draws into the manifest `probe[]` block so you can
find/verify which register carries the agent's world position. Leave it off unless
Isolation stops matching (`iso=0`) and you need to re-calibrate.

---

## Your Issues / Questions

**"Whole scene in a zone only exported a single bush (+ skybox when AABB=0)."**
`Whole scene` captures **one armed frame's** visible, depth-tested draws — and GW
frustum-culls hard, so that's just what the camera saw, not the whole zone. The bush
was near the camera; with the AABB cap off, the skybox (a huge textured mesh) came
in too. There is no single-frame path to a *whole zone* — the geometry simply isn't
all drawn at once. Options: capture from several camera angles and merge, or wait for
DAT asset loading (roadmap) which reads models independent of what's on-screen. Also
note **Trim runs in Whole-scene too**; for a broad scene grab, turn Trim off so
distant terrain isn't treated as outliers.

**"Stark-clothing character — could only get the cape / partial armor."**
Same root cause as the tolerance-25 case above: stacked filters where Isolation (or
Max AABB against the big Lion Mask, >50u) was the binding constraint. Use the
workflow below — Refresh, read which counter is doing the dropping, loosen *that* one.

**"If Source = self, is the origin the player's head?"**
No. The origin is the **model's authored pivot**, which for GW characters sits around
the **feet/pelvis**, not the head. The up-axis remap (Z→Y) is a pure rotation about
that origin, which is why the head ends up pointing +Y. The agent's GWCA `pos_*`
(shown in Diagnostics) is the **ground/feet** world position, and that's what
Isolation matches against.

**"How hard is reliably getting *more* of the scene?"**
For live capture, you're bounded by what GW draws that frame. More coverage = more
frames from more angles, or DAT parsing later. Within one frame, the useful lever is
turning filters *off* (Whole scene, Trim off) and accepting the extra geometry.

---

## When the world renders depth-test-off (the "UI in my export" case)

`Exclude 2D/UI` works by dropping draws rendered with the depth test **off** —
assuming the 3D world is depth-tested and only the HUD/UI is not. **On some setups
GW draws the whole 3D world with the depth test off too.** Symptom: with Exclude-2D
on you get `skipped: 2d=<big>, captured=0` (the world is being thrown away as if it
were UI). Turning Exclude-2D off fixes capture — but now the HUD comes in with the
world, because the one signal separating them is gone.

Those HUD pieces are **flat billboards** (nameplates, floating text, decals): exactly
planar, vertex-shaded and textured, so extent/texture filters miss them. Two things
then go wrong at once:
1. Your export fills with flat sheets.
2. The billboards outnumber the real mesh, so **Trim** centers on them and trims your
   *actual body* as the outlier — you end up with billboards and no character.

**Fix:** set **Drop flat draws < thickness** to `~1.5`. It removes the billboard
class in the capture hook (before Trim), which also un-breaks Trim and Isolation on
the real geometry. Then isolate as normal.

## Recommended workflow (uses the new Diagnostics panel)

1. **Source** = Player or Target (target the agent you want).
2. Open **Diagnostics** → confirm "Source world pos" is populated (that's what
   Isolation will match).
3. Click **Refresh diagnostics** — arms a capture and reports counts + model AABB
   **without writing files**. Iterate here; it's cheap.
4. Read the `skipped:` line and find the **binding constraint**:
   - lots in `filtered=` → a size/count/extent filter is doing the cutting.
   - lots in `iso=` → Isolation is cutting (good, if you want one agent).
   - `iso=0` with Isolate on → palette didn't match; Probe + widen tolerance.
   - lots in `trimmed=` → Trim is aggressive; raise MAD k or turn it off.
5. Adjust the *binding* setting, Refresh again, repeat until `captured` and the model
   AABB look like one character (~13 × 13 × 90 pre-remap).
6. **Export Snapshot** to write the files.

---

## Known-good and starting configs

Only one combination has been confirmed reliable in the field so far; the rest are
starting points to tune with Refresh, not guarantees.

**Faces of everyone around you (confirmed working):**
```
Source: Target        Scope: Filtered
Max AABB extent: ~25  (heads/faces are small; bodies + terrain exceed it)
Isolate: off          Match/Radius: n/a
```

**Solo clean self-capture — `profile clean-solo` (the reliable path):**
```
Be ALONE in the instance (private district / empty explorable / guild hall), then:
Source: Player                 Scope: Filtered
Require skinned mesh: OFF       ← solo => no crowd to exclude; keeps static attachments too
Drop effect planes: ON         ← kills the additive black panels (auras/glows)
Max AABB extent: 150           ← drops terrain/structure-sized meshes (no skin-gate now)
Drop flat draws < thickness: 1.5   ← drops HUD billboards
Trim outlier fliers: on        Exclude 2D/UI: on (OFF if 2d=big/captured=0)
→ Refresh; want captured ≈ a dozen chunks, model AABB ~40×64×80, and effect= > 0
  (proof the black panels were dropped). Remove armor in-game first for a bare-skin base.
```
The old `require_skinned: ON` recipe still works when you specifically want *only* skinned
body and no static attachments, but it is no longer the default — see the caveat above.

**One agent out of a crowd — add isolation on top:**
```
...everything above, plus:
Isolate to Source agent: ON    Match tolerance: 250   (NOT 25–60; that clips the body)
Source: Player or Target (target the one you want)
→ Refresh; iso > 0 means it dropped the other agents. If captured is tiny, tolerance
  is too tight → raise it. Require-skinned drops the scenery; isolation picks the person.
```

**Single item/prop:** no reliable recipe yet — props are static, so Isolation
doesn't apply. Use Whole scene with the camera framed tight on the prop, Max AABB to
drop terrain, then Trim. Refresh-tune.

**Whole zone:** not achievable in one frame (frustum culling). Best current
approximation: Whole scene, Trim **off**, AABB **off**, capture several angles.

---

## What changed in the build alongside this doc

- **Require skinned mesh** (`require_skinned`) — keeps only bone-weighted character
  meshes, dropping static props/terrain/buildings even when they sit next to you. The
  clean fix for "it grabbed a nearby object." `is_skinned` is now recorded per chunk.
- **Drop flat draws** (`filter_min_thickness`) — removes billboards/decals/HUD text
  (exactly-planar quads) at the source, before Trim. Needed when the world renders
  depth-test-off so Exclude-2D can't separate the HUD.
- **Probe is now session-only** — no longer persisted, so it resets off each session
  instead of quietly bloating every manifest.
- **`iso=` diagnostic counter** — isolation drops are now separated from generic
  filter drops in the panel and the manifest (`draws_skipped_isolation`), so you can
  see whether Isolation is doing anything.
- **Center radius decoupled from Trim** — it now applies in Filtered scope
  regardless of the "Trim outlier fliers" toggle.
- **Diagnostics dropdown + Refresh** — live Source world pos/box, last-capture
  counts, and **model AABB + volume**, refreshable without writing an export.
- **Rewritten in-panel help** for Center radius, Require texture, Isolate, Match
  tolerance, Probe, and armor/weapon recording, reflecting the model-local reality.

Still open (needs in-game confirmation): whether the map-481 bone-palette calibration
holds across maps/shaders. The `iso=` counter is exactly the instrument to answer it
— if it reads 0 in a crowd with Isolate on, re-Probe and update the register window.
