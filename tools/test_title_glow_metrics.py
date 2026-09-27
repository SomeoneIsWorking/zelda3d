#!/usr/bin/env python3
"""Tests for the title host-vs-oracle capture metrics.

Two metrics live here and they answer different questions, which is the point:

* `wordmark_metrics` counts gold pixels in the wordmark box. It is sensitive to how much of the
  wordmark each side covers and to absolute brightness.
* `glow_metrics` measures the fire-glow's channel RATIOS over the brightest tenth of the halo. The
  glow is additively blended, so a lost per-draw RGB tint drives all three channels to saturation
  and the glow turns white -- and a saturated white and a saturated orange have the same luminance,
  so no brightness metric can see the difference. The blue-over-red ratio can.

That distinction is not hypothetical: the host rendered the glow at 1 : 1.00 : 0.99 (all three
channels saturated, 3809 saturated pixels) where the oracle has 1 : 0.93 : 0.58, because the
unified path gated the per-draw modulation on `lit` and the fire-glow is submitted force-unlit.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from title_host_capture import glow_metrics, wordmark_metrics  # noqa: E402

WIDTH, HEIGHT = 800, 480


def write_png(path: Path, array: np.ndarray) -> Path:
    Image.fromarray(np.clip(array, 0, 255).astype(np.uint8)).save(path)
    return path


def halo_fill(red: float, green: float, blue: float) -> np.ndarray:
    """A frame whose glow box is one flat colour, so the ratios are exactly the inputs."""
    image = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float64)
    image[:, :] = (10.0, 12.0, 14.0)
    image[60:200, 250:430] = (red, green, blue)
    return image


class GlowRatiosSeeALostTintWhereBrightnessCannot(unittest.TestCase):
    def _metrics(self, host_rgb: tuple[float, float, float]) -> dict[str, float]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            oracle = write_png(root / "az.png", halo_fill(246.0, 229.0, 143.0))
            host = write_png(root / "soh.png", halo_fill(*host_rgb))
            return glow_metrics(oracle, host)

    def test_a_tinted_glow_reproduces_the_oracle_blue_ratio(self) -> None:
        # The post-fix host: blue at 0.72 of red, against the oracle's 0.58.
        got = self._metrics((247.0, 236.0, 177.0))
        self.assertAlmostEqual(got["oracle_glow_b_over_r"], 143.0 / 246.0, places=3)
        self.assertAlmostEqual(got["host_glow_b_over_r"], 177.0 / 247.0, places=3)
        self.assertLess(abs(got["glow_b_over_r_delta"]), 0.2)

    def test_an_untinted_glow_is_far_from_the_oracle(self) -> None:
        # The pre-fix host: white, blue at 0.99 of red. This is the case the ratio exists to catch.
        got = self._metrics((254.0, 255.0, 252.0))
        self.assertGreater(got["host_glow_b_over_r"], 0.9)
        self.assertGreater(abs(got["glow_b_over_r_delta"]), 0.3)

    def test_green_ratio_tracks_the_oracle_too(self) -> None:
        got = self._metrics((247.0, 236.0, 177.0))
        self.assertAlmostEqual(got["oracle_glow_g_over_r"], 229.0 / 246.0, places=3)
        self.assertAlmostEqual(got["host_glow_g_over_r"], 236.0 / 247.0, places=3)

    def test_saturated_pixel_count_separates_white_from_amber(self) -> None:
        white = self._metrics((254.0, 255.0, 252.0))
        amber = self._metrics((247.0, 236.0, 177.0))
        self.assertGreater(white["host_glow_saturated_px"], 0.0)
        self.assertEqual(amber["host_glow_saturated_px"], 0.0)

    def test_the_oracle_side_does_not_depend_on_the_host(self) -> None:
        """Both sides are read from their own file; a host change must not move the oracle numbers.

        Checked because the byte-identical cached oracle frame produced different reported oracle
        numbers in two runs, which is impossible unless a metric is reading the wrong array.
        """
        first = self._metrics((254.0, 255.0, 252.0))
        second = self._metrics((247.0, 236.0, 177.0))
        for key in ("oracle_glow_r", "oracle_glow_g_over_r", "oracle_glow_b_over_r",
                    "oracle_glow_saturated_px"):
            self.assertEqual(first[key], second[key], key)


class GlowMetricUsesItsOwnRegion(unittest.TestCase):
    def test_a_change_outside_the_halo_does_not_move_the_glow_ratios(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            base = halo_fill(246.0, 229.0, 143.0)
            moved = base.copy()
            moved[300:400, 100:200] = (255.0, 0.0, 0.0)  # far from the halo
            a = glow_metrics(write_png(root / "a.png", base), write_png(root / "b.png", base))
            b = glow_metrics(write_png(root / "c.png", moved), write_png(root / "d.png", moved))
            self.assertEqual(a["host_glow_b_over_r"], b["host_glow_b_over_r"])
            self.assertEqual(a["host_glow_saturated_px"], b["host_glow_saturated_px"])


class WordmarkMetricStillRefusesAnEmptyResult(unittest.TestCase):
    def test_no_gold_anywhere_raises_rather_than_reporting_zero(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            black = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float64)
            path = write_png(root / "black.png", black)
            with self.assertRaisesRegex(RuntimeError, "no gold pixels"):
                wordmark_metrics(path, path)

    def test_a_size_mismatch_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            small = np.zeros((240, 400, 3), dtype=np.float64)
            big = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float64)
            with self.assertRaisesRegex(RuntimeError, "capture size mismatch"):
                wordmark_metrics(write_png(root / "s.png", small), write_png(root / "b.png", big))


if __name__ == "__main__":
    unittest.main()
