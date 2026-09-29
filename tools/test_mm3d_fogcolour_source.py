#!/usr/bin/env python3
"""The fog-colour source search must be able to fail, and must say which failure it is.

The failure this guards against is specific: a search over 5.7 MB that reports "0 hits" looks
identical whether the colour is genuinely absent or the search is broken. So the control is pinned
in BOTH directions -- a real image must pass it, and a corrupted one must refuse with a distinct
message rather than a clean "absent". A tool that cannot report its own blindness must not be
allowed to report a negative.
"""

from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from mm3d_fogcolour_source import (  # noqa: E402
    BUILD_PATH_MARKER,
    DEFAULT_RGB,
    IMAGE_BASE,
    IMAGE_SIZE,
    Hit,
    encodings,
    find_all,
    run_controls,
    search,
)

# A stand-in image carrying the two INDEPENDENT anchors the control asserts: the documented size
# and MM3D's build-path marker. Both are properties of the game, not of this file, which is the
# point -- a control derived from the input can never fail.
FAKE = (b"\x00\x01\x02\x03" + b"\\sources\\" + b"\xff" * 64).ljust(IMAGE_SIZE, b"\x00")


class EncodingTests(unittest.TestCase):
    def test_the_register_word_encoding_matches_azahars_bit_layout(self) -> None:
        # fog_color is r@0-7, g@8-15, b@16-23 (regs_texturing.h:428-433). If this drifts the whole
        # search silently tests the wrong thing, and every result still looks clean.
        packed = encodings((40, 140, 220))["register word r|g<<8|b<<16"]
        self.assertEqual(int.from_bytes(packed, "little"), 40 | (140 << 8) | (220 << 16))

    def test_every_encoding_is_non_empty_and_distinct_for_the_target(self) -> None:
        table = encodings(DEFAULT_RGB)
        self.assertGreaterEqual(len(table), 6)
        for name, pattern in table.items():
            self.assertTrue(pattern, name)
        self.assertEqual(len(set(table.values())), len(table), "two encodings are the same pattern")

    def test_no_encoding_is_short_enough_to_match_by_chance(self) -> None:
        # A 2-byte rgb565 matched twice in the real 5.7 MB image. A 2-byte needle in 5.7 MB
        # expects ~91 coincidental hits, so a hit from one is arithmetic, not evidence. Every
        # encoding must be at least 3 bytes, which expects ~0.001 false hits in this image.
        for name, pattern in encodings(DEFAULT_RGB).items():
            self.assertGreaterEqual(len(pattern), 3, f"{name} is too short to be distinctive")

    def test_channel_order_is_actually_reversed(self) -> None:
        a = encodings((40, 140, 220))["register word r|g<<8|b<<16"]
        b = encodings((40, 140, 220))["register word b|g<<8|r<<16"]
        self.assertNotEqual(a, b)

    def test_a_grey_colour_is_order_independent_and_says_so(self) -> None:
        # (10,10,10) packs the same both ways. A search reporting a "distinctive" hit for a grey
        # colour in a reversed order is finding coincidence, not evidence.
        self.assertEqual(
            encodings((10, 10, 10))["register word r|g<<8|b<<16"],
            encodings((10, 10, 10))["register word b|g<<8|r<<16"],
        )


class ControlTests(unittest.TestCase):
    def test_a_correct_image_passes_both_controls(self) -> None:
        self.assertEqual(run_controls(FAKE), [])
        self.assertIn(BUILD_PATH_MARKER, FAKE)
        self.assertEqual(len(FAKE), IMAGE_SIZE)

    def test_a_truncated_image_fails_the_size_control(self) -> None:
        failures = run_controls(FAKE[: len(FAKE) // 2])
        self.assertTrue(any("truncated" in f or "expected" in f for f in failures), failures)

    def test_an_image_without_the_build_paths_is_refused(self) -> None:
        # Same size, no build paths: this is a well-formed file that is NOT MM3D's code, and a
        # search over it would report "the colour is absent" for the wrong reason entirely.
        wrong = b"\x5a" * IMAGE_SIZE
        self.assertEqual(len(wrong), IMAGE_SIZE)
        failures = run_controls(wrong)
        self.assertTrue(any("not MM3D" in f for f in failures), failures)

    def test_a_corrupted_image_fails_the_control_instead_of_reporting_absence(self) -> None:
        # The failure that would otherwise print a clean zero: right size, no marker.
        corrupted = b"\xaa" * IMAGE_SIZE
        with self.assertRaises(ValueError) as caught:
            search(corrupted, DEFAULT_RGB)
        self.assertIn("CONTROL FAILED", str(caught.exception))

    def test_the_control_is_not_derived_from_the_input(self) -> None:
        # The regression this pins: an earlier control searched for the image's own first four
        # bytes, so it could never fail. Build two different valid images and require that the
        # control's verdict does not depend on their contents.
        self.assertEqual(run_controls(FAKE), [])
        other = (b"\xff\xff\xff\xff" + b"\\sources\\" + b"\x00" * 64).ljust(IMAGE_SIZE, b"\x11")
        self.assertEqual(run_controls(other), [])
        self.assertNotEqual(FAKE[:4], other[:4])


class SearchTests(unittest.TestCase):
    def test_a_colour_that_is_absent_reports_zero_hits_with_a_passing_control(self) -> None:
        hits, tried, controls = search(FAKE, DEFAULT_RGB)
        self.assertEqual(hits, [])
        self.assertGreaterEqual(tried, 6)
        self.assertEqual(len(controls), 2)

    def test_a_colour_that_is_present_is_found_and_addressed_from_the_image_base(self) -> None:
        needle = encodings((0x2A, 0x2B, 0x2C))["register word r|g<<8|b<<16"]
        blob = bytearray(FAKE)
        offset = len(FAKE) - 64
        blob[offset : offset + 4] = needle
        hits, _tried, _controls = search(bytes(blob), (0x2A, 0x2B, 0x2C))
        self.assertEqual(len(hits), 1)
        self.assertEqual(hits[0].offset, offset)
        # The reported ADDRESS is what a follow-up memory read needs; an offset alone would send a
        # reader to the wrong place, which is how the earlier "candidate source" mislabel happened.
        self.assertEqual(hits[0].address, IMAGE_BASE + offset)

    def test_hits_name_the_encoding_that_matched(self) -> None:
        needle = encodings((0x2A, 0x2B, 0x2C))["rgba8 r,g,b,ff"]
        blob = bytearray(FAKE)
        offset = len(FAKE) - 64
        blob[offset : offset + 4] = needle
        hits, _tried, _controls = search(bytes(blob), (0x2A, 0x2B, 0x2C))
        self.assertEqual([h.encoding for h in hits], ["rgba8 r,g,b,ff"])

    def test_repeated_values_are_all_reported_not_just_the_first(self) -> None:
        needle = encodings((0x2A, 0x2B, 0x2C))["register word r|g<<8|b<<16"]
        blob = bytearray(FAKE)
        first, second = len(FAKE) - 64, len(FAKE) - 40
        blob[first : first + 4] = needle
        blob[second : second + 4] = needle
        hits, _tried, _controls = search(bytes(blob), (0x2A, 0x2B, 0x2C))
        self.assertEqual(len(hits), 2)

    def test_find_all_stops_at_its_cap_and_says_nothing_loudly(self) -> None:
        # A capped search must not pretend it was exhaustive.
        self.assertEqual(len(find_all(b"ab" * 100, b"ab", limit=5)), 5)

    def test_hit_is_immutable_so_a_reported_address_cannot_be_edited_in_place(self) -> None:
        hit = Hit(0x10, IMAGE_BASE + 0x10, "x")
        with self.assertRaises(Exception):
            hit.address = 0  # type: ignore[misc]


if __name__ == "__main__":
    unittest.main()
