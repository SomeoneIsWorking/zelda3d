#!/usr/bin/env python3
"""The MM3D fog-colour hunt must be able to say NO as well as yes.

`classify()` is the whole instrument, and it is pure precisely so this runs without the ROM. The
negative case is the one that matters here: the first version of the hunt reported "real candidate"
on a coincidental byte match, and the control that should have caught it (a triple the oracle never
reported) came back clean. Alignment against a known env colour is what actually separates a field
from a coincidence, so both directions of that decision are pinned.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mm3d_fogcolour_hunt import ENV_RECORD_STRIDE, classify  # noqa: E402


class FogColourHuntVerdictTests(unittest.TestCase):
    def test_absent_is_reported_absent(self) -> None:
        self.assertEqual(classify([], anchor=0x71F6), "absent")

    def test_slot_aligned_hits_are_a_field(self) -> None:
        anchor = 0x71F6
        aligned = [anchor + i * ENV_RECORD_STRIDE for i in range(3)]
        self.assertEqual(classify(aligned, anchor), "aligned")

    def test_off_stride_hits_are_a_coincidence(self) -> None:
        # The real measured case: +12008 and +12040 from the anchor, both 8 (mod 0x20).
        anchor = 0x71F6
        self.assertEqual(classify([anchor + 12008, anchor + 12040], anchor), "off-stride")

    def test_one_off_stride_hit_poisons_the_set(self) -> None:
        # All-or-nothing on purpose: a table's hits are either all slot-aligned or none are, so a
        # single misaligned member means the set is not describing that table.
        anchor = 0x1000
        self.assertEqual(classify([anchor, anchor + ENV_RECORD_STRIDE], anchor), "aligned")
        self.assertEqual(classify([anchor, anchor + ENV_RECORD_STRIDE + 8], anchor), "off-stride")

    def test_missing_anchor_refuses_to_guess(self) -> None:
        # No anchor means no way to tell a field from a coincidence, so it must not claim one.
        self.assertEqual(classify([0xA0DE, 0xA0FE], anchor=None), "unanchored")
        self.assertEqual(classify([0xA0DE, 0xA0FE], anchor=-1), "unanchored")

    def test_alignment_is_measured_from_the_anchor_not_the_start(self) -> None:
        # A hit at offset 0 of the file is aligned only if the anchor is itself 0 mod stride.
        self.assertEqual(classify([0x100], anchor=0x100), "aligned")
        self.assertEqual(classify([0x100], anchor=0x108), "off-stride")


if __name__ == "__main__":
    unittest.main()
