#!/usr/bin/env python3
"""#21 HUD-corruption detector.

The injected raw-RGBA HUD textures (heart row top-left, rupee counter bottom-left)
nondeterministically render garbled on ~10% of cold launches and stay garbled the
whole session. The CPU-side texel bytes are deterministic (see issue #21 narrowing),
so the bytes fed to the HUD are byte-identical every launch — meaning on a fixed
camera vantage the HUD crops of *clean* launches are near-identical, and a *corrupted*
launch is a strong outlier.

This classifier exploits that: stack the HUD crops of every launch, take the per-pixel
MEDIAN as the clean baseline (corruption is the minority), then score each launch by
MSE-vs-baseline. Outliers (MSE >> the clean cluster) are flagged corrupted. No manual
labeling — it auto-bootstraps from the batch.

Usage: zelda3d_hud_detect.py <shot1.png> <shot2.png> ...
       (>= ~6 shots for a stable median.)
"""
import sys
import numpy as np
from PIL import Image

# HUD regions (1920x1006 screenshots), tuned on a clean shot.
HEARTS = (40, 20, 560, 140)      # red heart row, top-left
RUPEE  = (20, 900, 360, 1006)    # rupee counter, bottom-left


def crops(path):
    im = Image.open(path).convert("RGB")
    if im.size != (1920, 1006):
        im = im.resize((1920, 1006))
    h = np.asarray(im.crop(HEARTS), dtype=np.float32)
    r = np.asarray(im.crop(RUPEE), dtype=np.float32)
    return h, r


def main(paths):
    if len(paths) < 3:
        print("need >=3 shots", file=sys.stderr)
        return 2
    H = np.stack([crops(p)[0] for p in paths])  # (N,h,w,3)
    R = np.stack([crops(p)[1] for p in paths])
    baseH = np.median(H, axis=0)
    baseR = np.median(R, axis=0)
    mseH = ((H - baseH) ** 2).mean(axis=(1, 2, 3))
    mseR = ((R - baseR) ** 2).mean(axis=(1, 2, 3))
    score = mseH + mseR

    # Robust outlier threshold: median + k*MAD of the score distribution.
    med = np.median(score)
    mad = np.median(np.abs(score - med)) + 1e-6
    thr = med + 6.0 * 1.4826 * mad
    # Also require an absolute floor so a perfectly-clean batch flags nothing.
    thr = max(thr, med + 80.0)

    print(f"# baseline(median) score={med:.1f} MAD={mad:.1f} thr={thr:.1f}")
    print(f"{'idx':>3} {'heartsMSE':>10} {'rupeeMSE':>10} {'score':>9}  verdict  path")
    flagged = []
    for i, p in enumerate(paths):
        bad = score[i] > thr
        if bad:
            flagged.append(p)
        print(f"{i:>3} {mseH[i]:>10.1f} {mseR[i]:>10.1f} {score[i]:>9.1f}  "
              f"{'CORRUPT' if bad else 'clean ':>7}  {p}")
    print(f"\n# {len(flagged)}/{len(paths)} flagged corrupt")
    for p in flagged:
        print(f"CORRUPT: {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
