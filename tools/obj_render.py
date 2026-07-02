#!/usr/bin/env python3
"""Render a Wavefront .obj to a shaded .png -- standard library only.

Guildlite verification helper. The plugin exports models on a Windows box; this
turns one into a small image an operator (or an image classifier) can eyeball to
confirm "the export is a sword / an axe / a character" without opening a DCC app.
It is deliberately dependency-free (no numpy, no Pillow): a software z-buffer
rasteriser plus a hand-rolled PNG writer, so it runs anywhere Python does.

    obj_render.py model.obj out.png [--width 640] [--height 640]
                  [--yaw 35] [--pitch 20] [--bg 24,26,32] [--color 200,205,215]
                  [--max-tris 200000] [--up none|x|y|z]

Notes
-----
* Uses only vertex positions + faces (materials/UVs ignored) -- shape is what we
  verify. Faces are fan-triangulated; n-gons are fine.
* Auto-frames the model (orthographic fit) so scale never matters.
* Flat Lambert shading from a fixed key light; back faces are still drawn (GW rips
  have inconsistent winding) so nothing goes missing.
* --up remaps a model's up-axis to +Y before rendering. Use --up z for a RAW GW
  grab (Z-up, looks like it's lying down); leave it 'none' for a Guildlite export
  already reoriented by the plugin's up-axis setting.
"""

import argparse
import math
import struct
import sys
import zlib


def write_png(path, w, h, rgb):
    """rgb: bytearray of length w*h*3, 8-bit RGB, row-major top-to-bottom."""
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF))

    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0 (none)
        raw += rgb[y * stride:(y + 1) * stride]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def load_stl(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 84:
        return [], []
    # ASCII STL starts with "solid" and contains "facet"; ours is binary.
    if data[:5].lower() == b"solid" and b"facet" in data[:512]:
        verts, faces = [], []
        toks = data.decode("ascii", "replace").split()
        i = 0
        cur = []
        while i < len(toks):
            if toks[i] == "vertex" and i + 3 < len(toks):
                cur.append((float(toks[i + 1]), float(toks[i + 2]), float(toks[i + 3])))
                if len(cur) == 3:
                    b = len(verts); verts += cur; faces.append((b, b + 1, b + 2)); cur = []
                i += 4
            else:
                i += 1
        return verts, faces
    n = struct.unpack("<I", data[80:84])[0]
    verts, faces = [], []
    off = 84
    for _ in range(n):
        if off + 50 > len(data):
            break
        v = struct.unpack("<12f", data[off:off + 48])
        b = len(verts)
        verts.append((v[3], v[4], v[5]))
        verts.append((v[6], v[7], v[8]))
        verts.append((v[9], v[10], v[11]))
        faces.append((b, b + 1, b + 2))
        off += 50
    return verts, faces


def load_obj(path):
    verts = []
    faces = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                p = line.split()
                verts.append((float(p[1]), float(p[2]), float(p[3])))
            elif line.startswith("f "):
                idx = []
                for tok in line.split()[1:]:
                    # f v, v/vt, v//vn, v/vt/vn -- take the vertex index only
                    v = tok.split("/")[0]
                    if v:
                        i = int(v)
                        idx.append(i - 1 if i > 0 else len(verts) + i)
                for k in range(1, len(idx) - 1):  # fan triangulate
                    faces.append((idx[0], idx[k], idx[k + 1]))
    return verts, faces


def reorient(verts, up):
    """Rotate so the given model up-axis maps to +Y (matches the plugin's RemapUp)."""
    if up == "z":   # Z-up -> Y-up (raw GW grab), head up
        return [(x, -z, y) for (x, y, z) in verts]
    if up == "x":   # X-up -> Y-up
        return [(-y, x, z) for (x, y, z) in verts]
    return verts    # 'y' / 'none': already Y-up


def parse_triple(s, default):
    try:
        parts = [max(0, min(255, int(x))) for x in s.split(",")]
        return tuple(parts) if len(parts) == 3 else default
    except ValueError:
        return default


def render(verts, faces, w, h, yaw_deg, pitch_deg, bg, color, max_tris):
    if not verts or not faces:
        return None

    cy, sy = math.cos(math.radians(yaw_deg)), math.sin(math.radians(yaw_deg))
    cx, sx = math.cos(math.radians(pitch_deg)), math.sin(math.radians(pitch_deg))

    def transform(p):
        x, y, z = p
        x, z = x * cy + z * sy, -x * sy + z * cy   # yaw about Y
        y, z = y * cx - z * sx, y * sx + z * cx     # pitch about X
        return x, y, z

    tv = [transform(p) for p in verts]
    # Outlier-robust framing: a few stray verts (mis-grouped chunks, effects) shouldn't
    # shrink the model to a dot. Frame on the 1st..99th percentile of projected coords.
    def pct(vals, lo, hi):
        s = sorted(vals)
        return s[max(0, int(len(s) * lo))], s[min(len(s) - 1, int(len(s) * hi))]
    xs = [p[0] for p in tv]
    ys = [p[1] for p in tv]
    minx, maxx = pct(xs, 0.01, 0.99)
    miny, maxy = pct(ys, 0.01, 0.99)
    span = max(maxx - minx, maxy - miny) or 1.0
    margin = 0.9
    scale = margin * min(w, h) / span
    ox = (w - (minx + maxx) * scale) / 2.0
    oy = (h - (miny + maxy) * scale) / 2.0

    def project(p):
        # screen y is flipped so +Y is up
        return (p[0] * scale + ox, h - (p[1] * scale + oy), p[2])

    img = bytearray(bg[0:3] * (w * h))
    zbuf = [1e30] * (w * h)
    light = (0.40, 0.55, 0.74)  # normalised-ish key light direction

    drawn = 0
    for (a, b, c) in faces:
        if drawn >= max_tris:
            break
        if not (0 <= a < len(tv) and 0 <= b < len(tv) and 0 <= c < len(tv)):
            continue
        pa, pb, pc = project(tv[a]), project(tv[b]), project(tv[c])

        # face normal in view space for shading (from untransformed-but-rotated verts)
        ux, uy, uz = tv[b][0] - tv[a][0], tv[b][1] - tv[a][1], tv[b][2] - tv[a][2]
        vx, vy, vz = tv[c][0] - tv[a][0], tv[c][1] - tv[a][1], tv[c][2] - tv[a][2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        nlen = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        lam = abs((nx * light[0] + ny * light[1] + nz * light[2]) / nlen)
        shade = 0.25 + 0.75 * lam
        r = int(color[0] * shade); g = int(color[1] * shade); bl = int(color[2] * shade)

        # bounding box of the triangle on screen
        x0 = max(0, int(math.floor(min(pa[0], pb[0], pc[0]))))
        x1 = min(w - 1, int(math.ceil(max(pa[0], pb[0], pc[0]))))
        y0 = max(0, int(math.floor(min(pa[1], pb[1], pc[1]))))
        y1 = min(h - 1, int(math.ceil(max(pa[1], pb[1], pc[1]))))
        if x1 < x0 or y1 < y0:
            continue
        area = (pb[0] - pa[0]) * (pc[1] - pa[1]) - (pb[1] - pa[1]) * (pc[0] - pa[0])
        if abs(area) < 1e-9:
            continue
        inv_area = 1.0 / area
        drawn += 1

        for py in range(y0, y1 + 1):
            for px in range(x0, x1 + 1):
                fx, fy = px + 0.5, py + 0.5
                wa = ((pb[0] - fx) * (pc[1] - fy) - (pb[1] - fy) * (pc[0] - fx)) * inv_area
                wb = ((pc[0] - fx) * (pa[1] - fy) - (pc[1] - fy) * (pa[0] - fx)) * inv_area
                wc = 1.0 - wa - wb
                if wa < 0 or wb < 0 or wc < 0:
                    continue
                z = wa * pa[2] + wb * pb[2] + wc * pc[2]
                o = py * w + px
                if z < zbuf[o]:
                    zbuf[o] = z
                    j = o * 3
                    img[j] = r; img[j + 1] = g; img[j + 2] = bl
    return img


def main(argv):
    ap = argparse.ArgumentParser(description="Render an .obj to a shaded .png (stdlib only).")
    ap.add_argument("obj")
    ap.add_argument("png")
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=640)
    ap.add_argument("--yaw", type=float, default=35.0)
    ap.add_argument("--pitch", type=float, default=20.0)
    ap.add_argument("--bg", default="24,26,32")
    ap.add_argument("--color", default="200,205,215")
    ap.add_argument("--max-tris", type=int, default=200000)
    ap.add_argument("--up", choices=["none", "x", "y", "z"], default="none",
                    help="remap this model up-axis to +Y before rendering (use 'z' for a raw GW grab)")
    args = ap.parse_args(argv)

    verts, faces = load_stl(args.obj) if args.obj.lower().endswith(".stl") else load_obj(args.obj)
    if not verts:
        print("obj_render: no vertices in %s" % args.obj, file=sys.stderr)
        return 1
    verts = reorient(verts, args.up)
    bg = parse_triple(args.bg, (24, 26, 32))
    color = parse_triple(args.color, (200, 205, 215))
    img = render(verts, faces, args.width, args.height, args.yaw, args.pitch, bg, color, args.max_tris)
    if img is None:
        print("obj_render: nothing to draw", file=sys.stderr)
        return 1
    write_png(args.png, args.width, args.height, img)
    print("obj_render: %s -> %s (%d verts, %d tris)" % (args.obj, args.png, len(verts), len(faces)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
