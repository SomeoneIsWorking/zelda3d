#!/usr/bin/env python3
"""Tests for cached oracle fragment reduction."""

from __future__ import annotations

import io
import unittest

from oracle_fragment_summary import parse_fragment, summarize


def pixel(draw: int, x: int, depth: float, combined: str) -> str:
    return (
        f"PIXEL draw={draw} tex0=180bde00 xy=({x},20) depth={depth} "
        "texcol=(10,20,30,255) tex1col=(40,50,60,255) "
        f"primary=(70,80,90,255) combined=({combined})\n"
    )


class OracleFragmentSummaryTests(unittest.TestCase):
    def test_parser_ignores_unrelated_lines(self) -> None:
        self.assertIsNone(parse_fragment("draw n=29 nv=537\n"))

    def test_summary_keeps_nearest_fragment_per_pixel(self) -> None:
        log = io.StringIO(
            pixel(29, 10, 0.9, "100,110,120,255")
            + pixel(29, 10, 0.8, "200,210,220,255")
            + pixel(29, 11, 0.7, "50,60,70,255")
            + pixel(30, 99, 0.1, "1,2,3,255")
        )

        result = summarize(log, 29)

        self.assertEqual(result["generated_fragments"], 3)
        self.assertEqual(result["unique_pixels"], 2)
        self.assertEqual(result["discarded_occluded_fragments"], 1)
        self.assertEqual(
            result["framebuffer_bbox"],
            {"min_x": 10, "min_y": 20, "max_x": 11, "max_y": 20},
        )
        self.assertEqual(result["colors"]["combined"]["r"]["mean"], 125.0)

    def test_summary_refuses_missing_draw(self) -> None:
        with self.assertRaisesRegex(ValueError, "draw 4"):
            summarize(io.StringIO(pixel(3, 10, 0.8, "1,2,3,255")), 4)


if __name__ == "__main__":
    unittest.main()
