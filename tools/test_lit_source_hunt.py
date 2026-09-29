#!/usr/bin/env python3
"""The source hunt must not report a rare window as a source without a control behind it.

This session has already produced two false positives from weak controls -- a clean never-reported
triple that did not catch a coincidental byte match, and a marker signature that matched nothing real
-- so both verdict functions here are pinned in the direction that REFUSES. The claim available from
testing ~1200 windows in a 116 MB image is only ever relative: the object's rare-window rate against
the same statistic for windows sampled elsewhere in the same image.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lit_source_hunt import (  # noqa: E402
    RARE_PARTNERS,
    distinctive,
    locate,
    verdict,
    window_verdict,
)


class SourceHuntVerdictTests(unittest.TestCase):
    def test_whole_object_match_needs_a_clean_control(self) -> None:
        self.assertEqual(verdict(1, 0, 0.24)[0], 0)
        code, text = verdict(0, 0, 0.24)
        self.assertEqual(code, 1)
        self.assertIn("clean control", text)

    def test_whole_object_control_matching_makes_it_unreliable(self) -> None:
        code, text = verdict(0, 7, 0.24)
        self.assertEqual(code, 1)
        self.assertIn("unreliable", text)

    def test_ambiguous_whole_object_matches_are_not_a_source(self) -> None:
        code, text = verdict(3, 0, 0.24)
        self.assertEqual(code, 1)
        self.assertIn("ambiguous", text)

    def test_indistinctive_object_is_refused_not_called_ambiguous(self) -> None:
        code, text = verdict(0, 0, 0.001)
        self.assertEqual(code, 2)
        self.assertIn("not distinctive", text)

    def test_window_claim_requires_beating_the_control(self) -> None:
        # The measured shape: one or two rare windows in the object, and the control produces the same
        # rate, so they are chance.
        code, text = window_verdict(1, 1, 1193)
        self.assertEqual(code, 1)
        self.assertIn("chance", text)
        self.assertIn("no window of this object is identified", text)

    def test_window_claim_is_possible_when_richer_than_the_control(self) -> None:
        code, text = window_verdict(12, 0, 1193)
        self.assertEqual(code, 0)
        self.assertIn("may be a real source", text)

    def test_no_rare_windows_means_no_source(self) -> None:
        code, text = window_verdict(0, 0, 1193)
        self.assertEqual(code, 1)
        self.assertIn("none is a source", text)

    def test_zero_denominators_do_not_divide_by_zero(self) -> None:
        self.assertEqual(window_verdict(0, 0, 0)[0], 1)
        # A zero-density object is REFUSED rather than reported as a negative: "not distinctive enough
        # to search for" is a different statement from "searched, found nothing".
        code, text = verdict(0, 0, 0.0)
        self.assertEqual(code, 2)
        self.assertIn("not distinctive", text)

    def test_distinctiveness_rejects_mostly_zero_and_accepts_real_data(self) -> None:
        self.assertFalse(distinctive(bytes(32), 0.10))  # all NUL
        self.assertFalse(distinctive(b"", 0.10))
        self.assertTrue(distinctive(bytes(range(32)), 0.10))
        # An all-FF window is dense but degenerate; density alone is not distinctiveness, which is why
        # the partner test carries the weight instead.
        self.assertTrue(distinctive(bytes([0xFF]) * 32, 0.10))

    def test_rare_partner_threshold_is_narrow(self) -> None:
        # Above a couple of partners a window is just common data, so it cannot name a source.
        self.assertLessEqual(RARE_PARTNERS, 2)

    def test_locate_maps_an_offset_inside_a_chunk_to_its_address(self) -> None:
        # The bug this exists for: membership tested with the OBJECT size instead of the CHUNK size,
        # so every hit in the middle of a chunk failed to resolve and the object's own address was
        # reported as a candidate source.
        spans = [(0x08000000, 0, 0x10000), (0x0A000000, 0x20000, 0x8000)]
        self.assertEqual(locate(spans, 0x0), 0x08000000)
        # Near the END of a 0x10000 chunk -- the case the old OBJECT-size membership test got wrong.
        self.assertEqual(locate(spans, 0xFFF8), 0x0800FFF8)  # base + offset, not base + size
        self.assertEqual(locate(spans, 0x20000), 0x0A000000)
        self.assertEqual(locate(spans, 0x27FF8), 0x0A007FF8)  # near the end of the second chunk
        self.assertIsNone(locate(spans, 0x1FFFF))  # in the gap between regions


if __name__ == "__main__":
    unittest.main()
