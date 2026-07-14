#!/usr/bin/env python3
"""test_fireglow_extent.py — TDD close-test for the cs1093 fire-glow
letters-excluded gold-mask EXTENT residual
(debug_journal/2026-07-14-title-cs464-composition-exonerated-fireglow-remeasure.md,
Divergence 3).

Metric: box-scoped (x110-300 / y40-190, same box as tools/fireglow_ab.py's
glow_stats) GOLD-hue pixel count MINUS the strict-red wordmark-letter mask
(so filled letter glyphs, which also fall in the gold-hue band once the D1
sphere-map decoration fix landed, are excluded from the glow-wash count).
This isolates the additive g_title.cmb flame-wash footprint from the
wordmark's own lettering. Masks reused verbatim from tools/title_cs464_measure.py
(gold_mask_stats / red_mask_stats definitions) for consistency with the prior
manual measurement this test formalizes.

Usage:
    tools/test_fireglow_extent.py <base>      # base.az.ppm / base.soh.ppm pair
    tools/test_fireglow_extent.py --min-ratio 0.90 <base>

Exit 0 if ratio (soh_extent / az_extent) >= --min-ratio, else 1. Also prints
raw pixel counts and per-pane luminance so a future session can tell an
extent regression apart from a luminance regression at a glance.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from title_cs464_measure import read_ppm  # noqa: E402

BOX = (slice(40, 190), slice(110, 300))  # y, x — same box as fireglow_ab.py


def gold_mask(img: np.ndarray) -> np.ndarray:
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    return (r > 120) & (g > 80) & (r > b * 1.5) & (g > b * 1.2) & (g < r)


def strict_red_mask(img: np.ndarray) -> np.ndarray:
    r, g, b = img[..., 0], img[..., 1], img[..., 2]
    return (r > 90) & (r > g * 1.6) & (r > b * 1.6)


def extent_and_lum(img: np.ndarray) -> tuple[int, float]:
    box = img[BOX]
    mask = gold_mask(box) & ~strict_red_mask(box)
    n = int(mask.sum())
    if n == 0:
        return 0, 0.0
    r, g, b = box[..., 0], box[..., 1], box[..., 2]
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    return n, float(lum[mask].mean())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base", help="path prefix; expects <base>.az.ppm + <base>.soh.ppm")
    ap.add_argument("--min-ratio", type=float, default=0.90,
                     help="minimum soh/az extent ratio to PASS (default 0.90 — see "
                          "header derivation in the paired debug_journal entry)")
    args = ap.parse_args()

    az = read_ppm(args.base + ".az.ppm")
    soh = read_ppm(args.base + ".soh.ppm")
    az_n, az_lum = extent_and_lum(az)
    soh_n, soh_lum = extent_and_lum(soh)
    ratio = (soh_n / az_n) if az_n > 0 else 0.0

    print(f"az:  extent_px={az_n:5d}  lum={az_lum:6.1f}")
    print(f"soh: extent_px={soh_n:5d}  lum={soh_lum:6.1f}")
    print(f"ratio (soh/az): {ratio:.3f}  threshold: {args.min_ratio}")
    ok = ratio >= args.min_ratio
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
