#!/usr/bin/env python3
"""compare_render.py — quantitative checks on Zelda3D render dumps. NEVER eyeball.

Isolates the model's pixels by diffing an OoT3D render (SOH3D=1) against the N64
render (SOH3D=0) of the same scene/spawn, then reports the model-region mean colour
and compares it to the source texture's palette. Use this to verify a render instead
of looking at it.

Subcommands:
  mean   <png> [x0 y0 x1 y1]            mean RGB of a region (default whole image)
  diff   <a.png> <b.png>               # of differing pixels + bbox of the diff
  model  <n64.png> <s3d.png> [thr]     isolate model via A/B diff; mean colour of
                                       the model pixels in BOTH renders
  tex    <cmb> <out_base_unused>       (use cmb.py) -- not here
"""
import sys
from PIL import Image, ImageChops

def load(p): return Image.open(p).convert("RGB")

def region_mean(im, box=None):
    if box: im = im.crop(box)
    px = list(im.getdata())
    n = len(px) or 1
    r = sum(p[0] for p in px)/n; g = sum(p[1] for p in px)/n; b = sum(p[2] for p in px)/n
    return (r, g, b, n)

def diff_mask(a, b, thr=24):
    """Pixels where |a-b| exceeds thr on any channel. Returns (mask_pixels, bbox)."""
    d = ImageChops.difference(a, b)
    W, H = d.size
    dp = list(d.getdata())
    coords = [(i % W, i // W) for i, p in enumerate(dp) if max(p) > thr]
    if not coords:
        return [], None
    xs = [c[0] for c in coords]; ys = [c[1] for c in coords]
    return coords, (min(xs), min(ys), max(xs)+1, max(ys)+1)

def mean_at_coords(im, coords):
    px = im.load(); n = len(coords) or 1
    r = sum(px[x, y][0] for x, y in coords)/n
    g = sum(px[x, y][1] for x, y in coords)/n
    b = sum(px[x, y][2] for x, y in coords)/n
    return (r, g, b, n)

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "mean":
        im = load(sys.argv[2])
        box = tuple(map(int, sys.argv[3:7])) if len(sys.argv) >= 7 else None
        r, g, b, n = region_mean(im, box)
        print(f"mean RGB=({r:.1f},{g:.1f},{b:.1f}) over {n} px  box={box}")
    elif cmd == "diff":
        a, b = load(sys.argv[2]), load(sys.argv[3])
        thr = int(sys.argv[4]) if len(sys.argv) > 4 else 24
        coords, bbox = diff_mask(a, b, thr)
        print(f"differing px={len(coords)} ({100*len(coords)/(a.size[0]*a.size[1]):.2f}%) bbox={bbox} thr={thr}")
    elif cmd == "model":
        n64, s3d = load(sys.argv[2]), load(sys.argv[3])
        thr = int(sys.argv[4]) if len(sys.argv) > 4 else 24
        coords, bbox = diff_mask(n64, s3d, thr)
        if not coords:
            print("NO model pixels found (renders identical at thr) — model not drawn?"); return
        nr = mean_at_coords(n64, coords); sr = mean_at_coords(s3d, coords)
        print(f"model pixels={len(coords)} bbox={bbox}")
        print(f"  N64  model-region mean RGB=({nr[0]:.1f},{nr[1]:.1f},{nr[2]:.1f})")
        print(f"  OoT3D model-region mean RGB=({sr[0]:.1f},{sr[1]:.1f},{sr[2]:.1f})")
    else:
        print(__doc__); sys.exit(1)

if __name__ == "__main__":
    main()
