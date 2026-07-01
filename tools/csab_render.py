#!/usr/bin/env python3
"""Quick software rasterizer for an animated (or bind-pose) OoT3D model — the
Python-side visual oracle for CSAB skinning, before the C++/GL path.

Orthographic front view (project X→col, Y→row), z-buffered, flat-shaded by the
triangle's view-space normal.z (no textures). Purpose: confirm the skinned POSE
is sane and measure it (filled-pixel count, centroid, bbox) — NOT final quality.

Usage: ZELDA3D_OOT3D_ROM=... python3 tools/csab_render.py [--anim ge1_s_wait] \
         [--frame N] [--out scratch/render/x.ppm] [--size 256x384] [--rotx 180]
"""
from __future__ import annotations
import sys, os, math, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from ctr_romfs import CtrRom
from zar import Zar


def render(tris, W, H, rotx_deg=0.0):
    # optional X rotation (model is authored head-down; 180 flips upright for viewing)
    a = math.radians(rotx_deg); ca, sa = math.cos(a), math.sin(a)

    def rot(p):
        return (p[0], ca * p[1] - sa * p[2], sa * p[1] + ca * p[2])

    pts = []
    xs = []; ys = []; zs = []
    for _, _, t in tris:
        tp = [rot(p) for p, _, _ in t]
        pts.append((tp, [n for _, n, _ in t]))
        for p in tp:
            xs.append(p[0]); ys.append(p[1]); zs.append(p[2])
    if not xs:
        return None, 0
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    ext = max(maxx - minx, maxy - miny) or 1.0
    sc = 0.9 * min(W, H) / ext
    cx, cy = (minx + maxx) / 2, (miny + maxy) / 2

    def proj(p):
        col = W / 2 + (p[0] - cx) * sc
        row = H / 2 - (p[1] - cy) * sc  # screen Y down; flip so +Y is up
        return col, row, p[2]

    img = bytearray([20, 20, 28] * (W * H))   # dark bg
    zbuf = [-1e30] * (W * H)
    filled = 0
    for tp, _ in pts:
        s = [proj(p) for p in tp]
        # face normal in view space (after rot+proj-flip) for flat shade
        ux, uy, uz = (s[1][0] - s[0][0], s[1][1] - s[0][1], 0)
        vx, vy, vz = (s[2][0] - s[0][0], s[2][1] - s[0][1], 0)
        # geometry normal from world positions
        e1 = tuple(tp[1][k] - tp[0][k] for k in range(3))
        e2 = tuple(tp[2][k] - tp[0][k] for k in range(3))
        nz = e1[0] * e2[1] - e1[1] * e2[0]
        nlen = (sum(c * c for c in (e1[1] * e2[2] - e1[2] * e2[1],
                                    e1[2] * e2[0] - e1[0] * e2[2], nz)) ** 0.5) or 1.0
        shade = 0.35 + 0.65 * min(1.0, abs(nz) / nlen)
        col = int(210 * shade)
        x0 = max(0, int(min(p[0] for p in s))); x1 = min(W - 1, int(max(p[0] for p in s)))
        y0 = max(0, int(min(p[1] for p in s))); y1 = min(H - 1, int(max(p[1] for p in s)))
        ax, ay = s[0][0], s[0][1]; bx, by = s[1][0], s[1][1]; cx2, cy2 = s[2][0], s[2][1]
        det = (by - cy2) * (ax - cx2) + (cx2 - bx) * (ay - cy2)
        if abs(det) < 1e-9:
            continue
        for py in range(y0, y1 + 1):
            for px in range(x0, x1 + 1):
                l1 = ((by - cy2) * (px + 0.5 - cx2) + (cx2 - bx) * (py + 0.5 - cy2)) / det
                l2 = ((cy2 - ay) * (px + 0.5 - cx2) + (ax - cx2) * (py + 0.5 - cy2)) / det
                l3 = 1 - l1 - l2
                if l1 < 0 or l2 < 0 or l3 < 0:
                    continue
                z = l1 * s[0][2] + l2 * s[1][2] + l3 * s[2][2]
                o = py * W + px
                if z > zbuf[o]:
                    zbuf[o] = z
                    img[o * 3] = col; img[o * 3 + 1] = col; img[o * 3 + 2] = int(col * 0.92)
                    filled += 1
    return img, filled


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zar", default="/actor/zelda_ge1.zar")
    ap.add_argument("--cmb", default="Model/geldwoman.cmb")
    ap.add_argument("--anim", default="ge1_s_wait")  # "" or "rest" for bind pose
    ap.add_argument("--frame", type=float, default=0.0)
    ap.add_argument("--rotx", type=float, default=180.0)
    ap.add_argument("--size", default="256x384")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    W, H = (int(x) for x in args.size.split("x"))

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(args.zar)))
    model = C.Cmb(z.read([f for f in z.files if f.name == args.cmb][0]))
    csab = None
    if args.anim and args.anim != "rest":
        # Accept a bare base ("ge1_s_wait" -> "Anim/ge1_s_wait.csab"), an "Anim/..." path, or any
        # verbatim zar-relative .csab path (link CSABs live under "boy/anim/"/"child/anim/", not "Anim/").
        if args.anim.startswith("Anim/") or args.anim.endswith(".csab"):
            nm = args.anim
        else:
            nm = "Anim/%s.csab" % args.anim
        # Fall back to a basename match if the exact path isn't present (resolves boy/ vs child/ dir).
        cand = [f for f in z.files if f.name == nm]
        if not cand:
            base = (nm.rsplit("/", 1)[-1])
            cand = [f for f in z.files if f.name.endswith("/" + base) or f.name == base]
        csab = A.Csab(z.read(cand[0]))

    tris = list(A.skinned_triangles(model, csab, args.frame))
    img, filled = render(tris, W, H, args.rotx)
    out = args.out or ("scratch/render/anim_%s_f%g.ppm" % (args.anim or "rest", args.frame))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(bytes(img))
    print(f"wrote {out}  ({filled} filled px, {len(tris)} tris)")


if __name__ == "__main__":
    main()
