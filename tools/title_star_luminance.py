#!/usr/bin/env python3
"""title_star_luminance.py — peak vs INTEGRATED star luminance, Az vs SoH.

Distinguishes "stars are dimmer" (peak AND integrated both low: a color/alpha-source bug)
from "stars are the same total brightness but spread over fewer/more pixels" (integrated
matches, peak differs: a point-size/rasterization-footprint difference) — see
oot3d-decomp/docs title_logo_actor.md-adjacent star-brightness residual
(debug_journal/2026-07-08-title-star-brightness-L8-decode.md fixed the L8 alpha-aliasing
bug; this tool answers what's left).

Usage: source .env && tools/title_ab.py ab <az> --soh <soh> --name star_probe
       tools/title_star_luminance.py scratch/title_ab/star_probe.az.ppm scratch/title_ab/star_probe.soh.ppm
Pick a night-sky (az,soh) pair whose star band (default y=[80,120], full width) is clear of
the moon disc and terrain horizon — check per-row max luminance first if unsure (the moon is
a large near-uniform bright blob, easy to spot as a jump in row-max over a wide y-range).
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


def analyze(path, y0=80, y1=120, bright_delta=15):
    img = read_ppm(path)
    lum = img.astype(np.float64).mean(axis=-1)
    band = lum[y0:y1, :]
    floor = float(np.percentile(band, 40))  # background sky level (this engine's own)
    excess = np.clip(band - floor, 0, None)
    return {
        "floor": floor,
        "peak": float(band.max()),
        "integrated_excess": float(excess.sum()),
        "n_bright_px": int((band > floor + bright_delta).sum()),
    }


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    results = {p: analyze(p) for p in argv}
    for p, r in results.items():
        print(f"{p}: floor={r['floor']:.1f} peak={r['peak']:.1f} "
              f"integrated_excess={r['integrated_excess']:.0f} n_bright_px={r['n_bright_px']}")
    if len(argv) == 2:
        a, b = results[argv[0]], results[argv[1]]
        print(f"\npeak ratio (2nd/1st):        {b['peak'] / a['peak']:.3f}")
        print(f"integrated ratio (2nd/1st):  {b['integrated_excess'] / a['integrated_excess']:.3f}")


if __name__ == "__main__":
    main(sys.argv[1:])
