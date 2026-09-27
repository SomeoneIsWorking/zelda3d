#!/usr/bin/env python3
"""Tests for the title ambient parity check.

This tool exists because a divergence was reported that was not one. The host appeared to render a
neutral grey ambient where the oracle renders a saturated blue — a large hue difference on the term
that, per `spot00_field_lighting_ground_truth.md`, is the *entire* lighting result for OoT3D's largest
vertex-lit material class. It was an artifact twice over: the two engines sample the same authored
3DS title-palette ramp at different points of the dayTime cycle, and the probe initially read a field
that is not what the renderer submitted.

The properties pinned here are the ones that keep that from recurring:

* the palette is the ROM's, and the four slots are what `Zelda3D_TitleCsLoad` reads;
* a blend match SOLVES for the weight instead of scanning, because a bounded scan silently misses a
  span's interior and reports "not from the palette" — the opposite of the truth;
* `tolerance` exists and is used for the oracle, because the oracle reports a float uniform that can
  land one 8-bit step either side of the host's integer rounding;
* a value that is genuinely not on the ramp is rejected.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from title_ambient_parity import (  # noqa: E402
    TITLE_LIGHT_SCHEDULE,
    TITLE_PALETTE_AMBIENT,
    blend_for_daytime,
    match_palette_blend,
)

# The oracle's live amb0 at daytime 0x2d95, and the host's submission at title cs=1093.
ORACLE_AMBIENT = (48, 66, 111)
HOST_AMBIENT = (96, 99, 68)


class ThePaletteIsTheRomsFourSlots(unittest.TestCase):
    def test_four_slots_read_from_spot99(self) -> None:
        self.assertEqual(len(TITLE_PALETTE_AMBIENT), 4)
        self.assertEqual(TITLE_PALETTE_AMBIENT[3], (40, 61, 119))
        self.assertEqual(TITLE_PALETTE_AMBIENT[0], (104, 104, 61))

    def test_the_schedule_covers_the_whole_clock(self) -> None:
        self.assertEqual(TITLE_LIGHT_SCHEDULE[0][0], 0x0000)
        self.assertEqual(TITLE_LIGHT_SCHEDULE[-1][1], 0xFFFF)
        for (_, end, _, _), (next_start, _, _, _) in zip(
            TITLE_LIGHT_SCHEDULE, TITLE_LIGHT_SCHEDULE[1:]
        ):
            self.assertEqual(end, next_start, "schedule spans must be contiguous")


class BlendsAreFoundBySolvingNotScanning(unittest.TestCase):
    def test_the_hosts_submitted_ambient_is_a_palette_blend(self) -> None:
        """The regression: a 512-daytime scan window missed this and reported "not from the palette"."""
        match = match_palette_blend(HOST_AMBIENT)
        self.assertIsNotNone(match, "the host's (96,99,68) must resolve to a title-palette blend")
        assert match is not None
        sf, st, weight, daytimes = match
        self.assertEqual((sf, st), (3, 0))
        self.assertAlmostEqual(weight, 0.875, places=2)
        self.assertTrue(daytimes)
        # The match is deep inside its span, which is exactly what a bounded scan misses.
        self.assertGreater(daytimes[0], 0x3D00)

    def test_the_oracle_ambient_is_on_the_same_ramp_at_exact_tolerance(self) -> None:
        """The oracle reports a float uniform, so tolerance 0 is not the right bar for it.

        `blend_for_daytime` rounds the host's way and yields (49,67,111); the oracle reports
        (48,66,111), one 8-bit step away on two channels. Requiring an exact match would fail the
        oracle against the very palette it came from.
        """
        blend, weight, sf, st = blend_for_daytime(0x2D95)
        self.assertEqual(blend, (49, 67, 111))
        self.assertEqual((sf, st), (3, 0))
        self.assertAlmostEqual(weight, 0.136, places=2)
        self.assertIsNone(match_palette_blend(ORACLE_AMBIENT, tolerance=0))
        self.assertIsNotNone(match_palette_blend(ORACLE_AMBIENT, tolerance=1))

    def test_host_and_oracle_land_on_the_same_slot_pair(self) -> None:
        host = match_palette_blend(HOST_AMBIENT)
        oracle = match_palette_blend(ORACLE_AMBIENT, tolerance=1)
        assert host is not None and oracle is not None
        self.assertEqual((host[0], host[1]), (oracle[0], oracle[1]))
        # Different weights, i.e. different points on one ramp -- the whole finding.
        self.assertNotAlmostEqual(host[2], oracle[2], places=2)


class GenuinelyForeignValuesAreRejected(unittest.TestCase):
    def test_an_off_ramp_value_does_not_match(self) -> None:
        self.assertIsNone(match_palette_blend((7, 200, 3), tolerance=1))

    def test_a_pure_grey_does_not_match(self) -> None:
        """The value the bad probe reported. It must never be accepted as palette-derived."""
        self.assertIsNone(match_palette_blend((26, 26, 31), tolerance=1))

    def test_the_override_gated_init_value_does_not_match(self) -> None:
        self.assertIsNone(match_palette_blend((0, 0, 255), tolerance=1))

    def test_tolerance_does_not_grow_without_bound(self) -> None:
        """A large tolerance must not turn the check into 'matches anything'."""
        self.assertIsNone(match_palette_blend((7, 200, 3), tolerance=40))


if __name__ == "__main__":
    unittest.main()
