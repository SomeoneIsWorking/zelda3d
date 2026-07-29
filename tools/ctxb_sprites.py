#!/usr/bin/env python3
"""Split a decoded CTXB atlas into its sprite islands (alpha connected components).

An OoT3D /menu CTXB holds ONE big texture that is an ATLAS; the individual HUD sprites
are alpha-separated islands inside it. This finds them so a port can quote exact UV
rectangles instead of eyeballing them.

  python3 tools/ctxb_sprites.py <romfs-or-file.ctxb> <outdir> [--min-area 24] [--alpha 8]

Writes <stem>_sprXX_x<X>_y<Y>_w<W>_h<H>.png per island plus <stem>_map.png (the atlas with
every island boxed + indexed) and prints the rectangle table.
"""
from __future__ import annotations
import os, sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from tools import ctxb


def islands(im, alpha_thresh=8, min_area=24):
    w, h = im.size
    a = im.split()[3].load()
    seen = bytearray(w * h)
    out = []
    for y0 in range(h):
        for x0 in range(w):
            if seen[y0 * w + x0] or a[x0, y0] < alpha_thresh:
                continue
            q = deque([(x0, y0)])
            seen[y0 * w + x0] = 1
            minx = maxx = x0
            miny = maxy = y0
            area = 0
            while q:
                x, y = q.popleft()
                area += 1
                if x < minx: minx = x
                if x > maxx: maxx = x
                if y < miny: miny = y
                if y > maxy: maxy = y
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and a[nx, ny] >= alpha_thresh:
                        seen[ny * w + nx] = 1
                        q.append((nx, ny))
            if area >= min_area:
                out.append((minx, miny, maxx - minx + 1, maxy - miny + 1, area))
    out.sort(key=lambda r: (r[1] // 8, r[0]))
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    from PIL import Image, ImageDraw
    path, outdir = argv[1], argv[2]
    alpha_t = int(argv[argv.index("--alpha") + 1]) if "--alpha" in argv else 8
    min_area = int(argv[argv.index("--min-area") + 1]) if "--min-area" in argv else 24
    os.makedirs(outdir, exist_ok=True)
    c = ctxb.load(path)
    im = c.image(0)
    stem = os.path.basename(path).replace(".ctxb", "")
    rects = islands(im, alpha_t, min_area)
    mp = Image.new("RGBA", im.size, (18, 18, 26, 255))
    mp.alpha_composite(im)
    dr = ImageDraw.Draw(mp)
    for i, (x, y, w, h, area) in enumerate(rects):
        im.crop((x, y, x + w, y + h)).save(
            os.path.join(outdir, "%s_spr%02d_x%d_y%d_w%d_h%d.png" % (stem, i, x, y, w, h)))
        dr.rectangle([x - 1, y - 1, x + w, y + h], outline=(255, 90, 90, 255))
        dr.text((x, max(0, y - 9)), str(i), fill=(255, 235, 120, 255))
        print("%-3d x=%-4d y=%-4d w=%-4d h=%-4d px=%d" % (i, x, y, w, h, area))
    mp.resize((im.size[0] * 2, im.size[1] * 2), Image.NEAREST).save(
        os.path.join(outdir, "%s_map.png" % stem))
    print("%d islands -> %s/%s_map.png" % (len(rects), outdir, stem))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
