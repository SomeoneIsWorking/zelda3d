#!/usr/bin/env python3
"""Tests for the combiner-buffer safety walk in the TEV corpus survey.

`prevbuf_before_latch` decides whether the evaluator's `vec4(0)` for PICA's un-latched
`tev_combiner_buffer_color` is exact or an approximation. Getting that backwards is invisible in the
output -- both answers print a number -- so the walk is pinned here with cases that must be flagged
as well as cases that must not, including the per-channel case the corpus walk exists to catch.
"""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from tev_corpus_survey import PREVIOUS, PREVIOUS_BUFFER, Stage, prevbuf_before_latch

PRIMARY = 0x8577
TEX0 = 0x84C0
MODULATE = 0x2100
STAGE_BYTES = 0x28


def make_stage(
    *,
    rgb_src: tuple[int, int, int] = (PRIMARY, TEX0, PRIMARY),
    a_src: tuple[int, int, int] = (PRIMARY, TEX0, PRIMARY),
    rgb_op: int = MODULATE,
    a_op: int = MODULATE,
    buf_rgb: int = PREVIOUS_BUFFER,
    buf_a: int = PREVIOUS_BUFFER,
) -> Stage:
    """Build one 0x28-byte combiner stage from named fields."""
    raw = bytearray(STAGE_BYTES)

    def put16(offset: int, value: int) -> None:
        struct.pack_into("<H", raw, offset, value)

    put16(0x00, rgb_op)
    put16(0x02, a_op)
    put16(0x04, 1)  # rgb_scale
    put16(0x06, 1)  # a_scale
    put16(0x08, buf_rgb)
    put16(0x0A, buf_a)
    for index, value in enumerate(rgb_src):
        put16(0x0C + 2 * index, value)
    for index in range(3):
        put16(0x12 + 2 * index, 0x0300)  # rgb_mod: SRC_COLOR
    for index, value in enumerate(a_src):
        put16(0x18 + 2 * index, value)
    for index in range(3):
        put16(0x1E + 2 * index, 0x0302)  # a_mod: SRC_ALPHA
    return Stage(bytes(raw), 0)


class ReadBeforeLatch(unittest.TestCase):
    def test_read_with_no_latch_anywhere_is_unsafe(self) -> None:
        stages = [make_stage(rgb_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY))]
        self.assertEqual(prevbuf_before_latch(stages), [(0, "rgb")])

    def test_read_after_a_latch_is_safe(self) -> None:
        stages = [
            make_stage(buf_rgb=PREVIOUS),
            make_stage(rgb_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY)),
        ]
        self.assertEqual(prevbuf_before_latch(stages), [])

    def test_same_stage_latch_does_not_license_its_own_read(self) -> None:
        """A stage latches its OUTPUT; it cannot read what it has not written yet.

        Reading PREVBUF and setting buf_rgb on the same stage is the shape that would look safe to a
        chain-wide "does this material ever latch?" check while still reading the stale register.
        """
        stages = [
            make_stage(rgb_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY), buf_rgb=PREVIOUS),
        ]
        self.assertEqual(prevbuf_before_latch(stages), [(0, "rgb")])

    def test_channels_are_tracked_separately(self) -> None:
        """An RGB latch does not license an alpha read, and vice versa."""
        rgb_only_latch = [
            make_stage(buf_rgb=PREVIOUS),
            make_stage(a_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY)),
        ]
        self.assertEqual(prevbuf_before_latch(rgb_only_latch), [(1, "a")])

        alpha_only_latch = [
            make_stage(buf_a=PREVIOUS),
            make_stage(rgb_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY)),
        ]
        self.assertEqual(prevbuf_before_latch(alpha_only_latch), [(1, "rgb")])

    def test_both_channels_latched_makes_both_reads_safe(self) -> None:
        stages = [
            make_stage(buf_rgb=PREVIOUS, buf_a=PREVIOUS),
            make_stage(
                rgb_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY),
                a_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY),
            ),
        ]
        self.assertEqual(prevbuf_before_latch(stages), [])

    def test_a_later_latch_does_not_retroactively_sanitise_an_earlier_read(self) -> None:
        stages = [
            make_stage(rgb_src=(PREVIOUS_BUFFER, PRIMARY, PRIMARY)),
            make_stage(buf_rgb=PREVIOUS),
        ]
        self.assertEqual(prevbuf_before_latch(stages), [(0, "rgb")])

    def test_unused_source_slots_are_not_reads(self) -> None:
        """PREVBUF sitting in a slot the op does not consume is not a read.

        MODULATE uses two slots, so a PREVBUF in slot 3 must not be counted -- the same
        within-op arity rule the survey already applies when counting texture consumption.
        """
        stages = [
            make_stage(
                rgb_op=MODULATE,
                rgb_src=(PRIMARY, TEX0, PREVIOUS_BUFFER),
                a_op=MODULATE,
                a_src=(PRIMARY, TEX0, PREVIOUS_BUFFER),
            ),
        ]
        self.assertEqual(prevbuf_before_latch(stages), [])

    def test_empty_chain_is_safe(self) -> None:
        self.assertEqual(prevbuf_before_latch([]), [])

    def test_a_chain_that_never_reads_is_safe(self) -> None:
        stages = [make_stage(), make_stage(buf_rgb=PREVIOUS)]
        self.assertEqual(prevbuf_before_latch(stages), [])


if __name__ == "__main__":
    unittest.main()
