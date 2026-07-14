#!/usr/bin/env python3
"""Quantitative close-test metrics for the cs464/cs1093 title divergences.

Measures, for a matched az/soh PPM pair from title_sbs_verify.py:
  1. wordmark-alpha proxy: mean saturation+value of the red ZELDA letters
     inside the wordmark bbox (red mask), per half. If SoH's fade alpha is
     lower, its red mask is paler (blends with grass) -> lower mean sat.
  2. global translation offset between the halves, estimated by integer
     cross-correlation of grayscale gradients over the full frame (dominated
     by terrain), searched over +/-24 px. Reports (dx, dy) that best maps
     SoH content onto Az content.

Usage: title_cs464_measure.py <base>  (expects <base>.az.ppm + <base>.soh.ppm)
"""
import sys
import numpy as np


def read_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split())
        assert f.readline().strip() == b"255"
        data = np.frombuffer(f.read(w * h * 3), dtype=np.uint8)
    return data.reshape(h, w, 3).astype(np.float32)


def red_mask_stats(img):
    """Red-letter mask: R dominant over G and B."""
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    mask = (r > 90) & (r > g * 1.6) & (r > b * 1.6)
    n = int(mask.sum())
    if n == 0:
        return n, 0.0, 0.0
    mx = img.max(axis=2)
    mn = img.min(axis=2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    return n, float(sat[mask].mean()), float(r[mask].mean())


def gold_mask_stats(img):
    """Gold/warm glow mask: R and G high, B low (fireglow + gold trim)."""
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    mask = (r > 120) & (g > 80) & (r > b * 1.5) & (g > b * 1.2) & (g < r)
    n = int(mask.sum())
    if n == 0:
        return n, 0.0
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    return n, float(lum[mask].mean())


def gray(img):
    return img @ np.array([0.299, 0.587, 0.114], dtype=np.float32)


def best_shift(a, b, rng=24):
    """(dx,dy) shift of b that best matches a, by SSD on gradient magnitude."""
    ga, gb = gray(a), gray(b)
    # gradient magnitude to be illumination-invariant
    def grad(x):
        gx = np.abs(np.diff(x, axis=1))[:-1, :]
        gy = np.abs(np.diff(x, axis=0))[:, :-1]
        return gx + gy
    Ga, Gb = grad(ga), grad(gb)
    h, w = Ga.shape
    best = (0, 0, np.inf)
    m = rng
    ca = Ga[m:h - m, m:w - m]
    for dy in range(-rng, rng + 1):
        for dx in range(-rng, rng + 1):
            cb = Gb[m + dy:h - m + dy, m + dx:w - m + dx]
            d = float(((ca - cb) ** 2).mean())
            if d < best[2]:
                best = (dx, dy, d)
    return best


def main():
    base = sys.argv[1]
    az = read_ppm(base + ".az.ppm")
    soh = read_ppm(base + ".soh.ppm")
    for name, img in (("az", az), ("soh", soh)):
        n, sat, rmean = red_mask_stats(img)
        gn, glum = gold_mask_stats(img)
        print(f"{name}: red_mask_px={n} red_sat_mean={sat:.4f} red_R_mean={rmean:.1f} "
              f"gold_mask_px={gn} gold_lum_mean={glum:.1f}")
    dx, dy, ssd = best_shift(az, soh)
    print(f"best shift of SoH onto Az: dx={dx} dy={dy} (ssd={ssd:.2f})")


if __name__ == "__main__":
    main()
