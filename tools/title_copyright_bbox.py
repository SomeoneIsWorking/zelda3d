#!/usr/bin/env python3
"""title_copyright_bbox.py — measure the copy_nintendo.cmb text-block bbox, Az vs SoH.

Isolates the "(c) 1998-2011 Nintendo / Codeveloped by GREZZO" text block in the bottom
screen quarter via a luminance/low-saturation mask (light-gray/white text over a darker
grass/ground background) and reports its pixel bbox in each pane, so a scale-factor bug
in the ortho overlay placement (Zelda3D_Overlay2D_PlaceModel, title_logo.cpp's
kCopyrightHeightFrac) can be measured directly instead of eyeballed.

Usage: source .env && tools/title_ab.py ab <az> --soh <soh> --name copyright_probe
       tools/title_copyright_bbox.py scratch/title_ab/copyright_probe.az.ppm scratch/title_ab/copyright_probe.soh.ppm
"""
import sys
import numpy as np


def read_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        maxv = int(f.readline())
        assert maxv == 255
        return np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)


def find_bbox(path, y0_frac=0.75, min_lum=120, max_sat=40):
    img = read_ppm(path)
    h, w, _ = img.shape
    y0 = int(h * y0_frac)
    region = img[y0:h, :, :].astype(np.float64)
    lum = region.mean(axis=-1)
    sat = region.max(axis=-1) - region.min(axis=-1)
    mask = (lum > min_lum) & (sat < max_sat)
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    x0, x1 = int(xs.min()), int(xs.max())
    yy0, yy1 = int(ys.min()) + y0, int(ys.max()) + y0
    return {
        "img_w": w, "img_h": h,
        "x0": x0, "x1": x1, "y0": yy0, "y1": yy1,
        "width": x1 - x0, "height": yy1 - yy0,
        "cx": (x0 + x1) / 2.0, "cy": (yy0 + yy1) / 2.0,
        "n_px": int(mask.sum()),
    }


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    results = {}
    for p in argv:
        r = find_bbox(p)
        results[p] = r
        if r is None:
            print(f"{p}: no mask hits (adjust thresholds / wrong frame)")
            continue
        print(f"{p}: bbox x[{r['x0']},{r['x1']}] y[{r['y0']},{r['y1']}] "
              f"w={r['width']} h={r['height']} center=({r['cx']:.1f},{r['cy']:.1f}) "
              f"({r['cx']/r['img_w']:.3f},{r['cy']/r['img_h']:.3f}) frac n_px={r['n_px']}")
    if len(argv) == 2 and all(results.values()):
        a, b = results[argv[0]], results[argv[1]]
        print(f"\nscale ratio (2nd/1st): width={b['width']/a['width']:.3f} "
              f"height={b['height']/a['height']:.3f}")
        print(f"center-frac delta (2nd-1st): x={b['cx']/b['img_w']-a['cx']/a['img_w']:+.4f} "
              f"y={b['cy']/b['img_h']-a['cy']/a['img_h']:+.4f}")


if __name__ == "__main__":
    main(sys.argv[1:])
