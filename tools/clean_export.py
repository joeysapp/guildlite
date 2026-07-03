#!/usr/bin/env python3
"""Offline cleaner for a Guildlite OBJ export -- the Mac-side mirror of the DLL's
drop_effects / clean-solo filtering, for salvaging captures taken before the filter
existed (or re-cleaning with different thresholds without re-capturing).

    python3 tools/clean_export.py <model.obj> [--out <clean.obj>]
                                   [--max-extent 150] [--min-thickness 1.5]
                                   [--keep-effects] [--drop-alpha]

Drops, per the sibling <model>.json manifest:
  * effect planes   -- chunk.is_effect (new captures) OR, if that field is absent,
                       chunk.alpha_blend (old captures: approximate -- also loses legit
                       hair/cape cutouts; pass --keep-effects to disable).
  * oversize meshes -- AABB longest axis > --max-extent (terrain/structure).
  * flat billboards -- AABB thinnest axis < --min-thickness (HUD/decals).
Keeps every other draw. Re-indexes the OBJ correctly (each `o object_<N>` block is
self-contained) and points mtllib at the original .mtl so textures still resolve when
the clean file sits in the same folder.
"""
import argparse, json, os, sys

ap = argparse.ArgumentParser()
ap.add_argument("obj")
ap.add_argument("--out", default=None)
ap.add_argument("--max-extent", type=float, default=150.0)
ap.add_argument("--min-thickness", type=float, default=1.5)
ap.add_argument("--origin-radius", type=float, default=0.0,
                help="drop draws whose model-local center is farther than this from origin "
                     "(rejects world terrain that leaks in when require_skinned is off; the "
                     "skinned character sits at the origin, static world geo at world coords). 0=off")
ap.add_argument("--keep-effects", action="store_true", help="do not drop effect/alpha draws")
ap.add_argument("--drop-alpha", action="store_true", help="force dropping ALL alpha_blend draws")
args = ap.parse_args()

obj_path = args.obj
mani_path = os.path.splitext(obj_path)[0] + ".json"
manifest = json.load(open(mani_path))
chunks = {c["draw_index"]: c for c in manifest["chunks"]}
has_is_effect = any("is_effect" in c for c in manifest["chunks"])

def should_drop(n):
    c = chunks.get(n)
    if c is None:
        return False, "unknown"
    dx = c["aabb_max"][0] - c["aabb_min"][0]
    dy = c["aabb_max"][1] - c["aabb_min"][1]
    dz = c["aabb_max"][2] - c["aabb_min"][2]
    if args.origin_radius > 0:
        cx, cy, cz = c["center"]
        if (cx * cx + cy * cy + cz * cz) ** 0.5 > args.origin_radius:
            return True, "far-from-origin"
    if args.max_extent > 0 and max(dx, dy, dz) > args.max_extent:
        return True, "oversize"
    if args.min_thickness > 0 and min(dx, dy, dz) < args.min_thickness:
        return True, "flat"
    if not args.keep_effects:
        if args.drop_alpha and c.get("alpha_blend"):
            return True, "alpha"
        if has_is_effect and c.get("is_effect"):
            return True, "effect"
        if not has_is_effect and c.get("alpha_blend"):
            return True, "alpha~effect"  # old manifest: alpha stands in for effect
    return False, "keep"

# --- pass 1: parse OBJ into per-object blocks, tracking global v/vt/vn bases -------
blocks = []  # {n, base:(v,vt,vn), v:[], vt:[], vn:[], f:[]}
header_mtllib = None
cur = None
gv = gvt = gvn = 0
for line in open(obj_path):
    if line.startswith("mtllib "):
        header_mtllib = line.strip()
        continue
    if line.startswith("o "):
        name = line.strip()[2:]
        n = int(name[len("object_"):]) if name.startswith("object_") else -1
        cur = {"n": n, "usemtl": None, "base": (gv, gvt, gvn), "v": [], "vt": [], "vn": [], "f": []}
        blocks.append(cur)
        continue
    if cur is None:
        continue
    if line.startswith("usemtl "):
        cur["usemtl"] = line.strip()
    elif line.startswith("v "):
        cur["v"].append(line); gv += 1
    elif line.startswith("vt "):
        cur["vt"].append(line); gvt += 1
    elif line.startswith("vn "):
        cur["vn"].append(line); gvn += 1
    elif line.startswith("f "):
        cur["f"].append(line)

# --- pass 2: re-emit kept blocks with remapped indices ----------------------------
out_path = args.out or (os.path.splitext(obj_path)[0] + "_clean.obj")
kept, dropped, drop_reasons = [], [], {}
for b in blocks:
    drop, reason = should_drop(b["n"])
    (dropped if drop else kept).append(b["n"])
    drop_reasons.setdefault(reason, 0)
    drop_reasons[reason] += 1 if drop else 0

def remap_face(line, base_old, base_new):
    out = ["f"]
    for tok in line.split()[1:]:
        parts = tok.split("/")
        comp = []
        for i, p in enumerate(parts):
            if p == "":
                comp.append("")
            else:
                # old is 1-based absolute into the SOURCE list; local (1-based) =
                # old - base_old[i]; new absolute = base_new[i] + local. No extra +1.
                old = int(p)
                comp.append(str(old - base_old[i] + base_new[i]))
        out.append("/".join(comp))
    return " ".join(out) + "\n"

with open(out_path, "w") as o:
    o.write("# Guildlite cleaned snapshot (tools/clean_export.py)\n")
    o.write(f"# source: {os.path.basename(obj_path)}  kept {len(kept)}/{len(blocks)} draws\n")
    if header_mtllib:
        o.write(header_mtllib + "\n")
    nv = nvt = nvn = 0
    for b in blocks:
        if b["n"] in dropped:
            continue
        o.write(f"o object_{b['n']}\n")
        if b["usemtl"]:
            o.write(b["usemtl"] + "\n")
        for l in b["v"]:
            o.write(l)
        for l in b["vt"]:
            o.write(l)
        for l in b["vn"]:
            o.write(l)
        base_old = b["base"]          # (v,vt,vn) count before this block in the SOURCE
        base_new = (nv, nvt, nvn)     # ...in the OUTPUT
        for l in b["f"]:
            o.write(remap_face(l, base_old, base_new))
        nv += len(b["v"]); nvt += len(b["vt"]); nvn += len(b["vn"])

sig = "is_effect" if has_is_effect else "alpha_blend(approx)"
print(f"{os.path.basename(obj_path)}: kept {len(kept)}/{len(blocks)} draws, dropped {len(dropped)} "
      f"({', '.join(f'{k}={v}' for k,v in drop_reasons.items() if v)}) via {sig} -> {os.path.basename(out_path)}")
