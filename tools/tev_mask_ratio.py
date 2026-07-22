#!/usr/bin/env python3
"""Per-draw mask brightness ratio: ours vs the oracle, at a MATCHED camera.

Consumes a tools/oracle_draw_isolate.py output dir (base.az.ppm + masks.npz, the
oracle's per-draw screen footprints) and one of OUR screenshots captured at the same
camera/time, and prints, per oracle draw, the mean-RGB luminance of both frames inside
that draw's own isolation mask and the ratio ours/oracle. This is the deliverable
metric of render.multi-stage-tev (Zora offenders 0.62-0.88 before; Kokiri 0.94-1.13 is
the regression gate).

Usage: python3 tools/tev_mask_ratio.py <drawiso_dir> <our_screenshot.png> [minpx]
"""
import sys

import numpy as np
from PIL import Image


def read_ppm(path):
    return np.asarray(Image.open(path)).astype(np.float64)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    d = sys.argv[1].rstrip("/")
    ours_path = sys.argv[2]
    minpx = int(sys.argv[3]) if len(sys.argv) > 3 else 500

    oracle = read_ppm(f"{d}/base.az.ppm")
    ours = np.asarray(Image.open(ours_path).convert("RGB")).astype(np.float64)
    if oracle.shape != ours.shape:
        sys.exit(f"shape mismatch: oracle {oracle.shape} vs ours {ours.shape} — not a matched capture")
    masks = np.load(f"{d}/masks.npz")

    lum = lambda img: img @ np.array([0.299, 0.587, 0.114])
    lo, lu = lum(oracle), lum(ours)

    rows = []
    for key in masks.keys():
        m = masks[key].astype(bool)
        n = int(m.sum())
        if n < minpx:
            continue
        mo = lo[m].mean()
        mu = lu[m].mean()
        co = oracle[m].mean(axis=0)
        cu = ours[m].mean(axis=0)
        rows.append((key, n, mo, mu, mu / max(mo, 1e-6), co, cu))
    rows.sort(key=lambda r: r[4])
    print(f"{'draw':>5} {'px':>7} {'oracle':>7} {'ours':>7} {'ratio':>6}  oracleRGB -> oursRGB")
    for key, n, mo, mu, r, co, cu in rows:
        print(
            f"{key:>5} {n:7d} {mo:7.1f} {mu:7.1f} {r:6.3f}  "
            f"({co[0]:.0f},{co[1]:.0f},{co[2]:.0f}) -> ({cu[0]:.0f},{cu[1]:.0f},{cu[2]:.0f})"
        )


if __name__ == "__main__":
    main()
