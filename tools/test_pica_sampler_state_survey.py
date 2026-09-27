#!/usr/bin/env python3
"""Tests for the sampler-state coverage survey.

The survey answers "which texture filter and wrap enums do both retail games use, and does the host
resolve each?". It exists because sampler filtering and wrapping are named scope next to formats and,
like formats before the LA4 fix, had no coverage evidence at all.

Two things are pinned here, and both are about the tool not being able to lie:

* An unnamed enum is a FINDING, not a formatting problem. Retail content carrying a value outside the
  known GL enum set must be reported as `UNNAMED`, never quietly omitted or mapped to something
  plausible -- a guessed enum name is exactly how a real gap would get hidden inside a clean report.
* A scan that found nothing must refuse. Zero bound texture units means the walk is broken (a shifted
  material pointer, a wrong stride), not that the content is clean. The MM3D version>=7 header shift
  produced exactly that silent zero once already, and it read as "no sampler state at all".
"""

from __future__ import annotations

import sys
import unittest
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

import pica_sampler_state_survey as survey  # noqa: E402

MIPMAP_LINEAR_NEAREST = 0x2702
LINEAR_MIPMAP_LINEAR = 0x2703
GL_REPEAT = 0x2901
GL_CLAMP_TO_EDGE = 0x812F
GL_MIRRORED_REPEAT = 0x8370


def _report(**kwargs) -> survey.SamplerStateReport:
    return survey.SamplerStateReport(game="test", **kwargs)


class UnnamedEnumsAreReportedNotGuessed(unittest.TestCase):
    def test_a_value_outside_the_known_enum_set_is_unnamed(self) -> None:
        report = _report(min_filter=Counter({0xBEEF: 4}))
        self.assertEqual(dict(report.unnamed()), {0xBEEF: 4})

    def test_a_named_enum_is_not_unnamed(self) -> None:
        report = _report(wrap_s=Counter({GL_MIRRORED_REPEAT: 2}))
        self.assertEqual(dict(report.unnamed()), {})

    def test_an_unnamed_value_in_any_axis_is_caught(self) -> None:
        for axis in ("min_filter", "mag_filter", "wrap_s", "wrap_t"):
            with self.subTest(axis=axis):
                report = _report(**{axis: Counter({0xDEAD: 1})})
                self.assertEqual(dict(report.unnamed()), {0xDEAD: 1})

    def test_the_text_reports_an_unnamed_enum_rather_than_a_name(self) -> None:
        text = survey.format_report(_report(wrap_s=Counter({0xBEEF: 1})))
        self.assertIn("UNNAMED", text)
        self.assertNotIn("GL_REPEAT", text)


class UncoveredEnumsAreSeparatedFromUnnamed(unittest.TestCase):
    def test_a_known_enum_the_host_does_not_cover_is_uncovered_not_unnamed(self) -> None:
        """A named GL enum outside the host's tables is coverage gap, not a naming gap.

        Collapsing the two would make `UNNAMED` read as "we do not understand this value" and hide the
        case that actually matters: we know exactly what it means and still do not implement it.
        """
        report = _report(wrap_s=Counter({0x812D: 5}))  # GL_CLAMP_TO_BORDER: named, not covered
        self.assertEqual(dict(report.unnamed()), {})
        self.assertEqual(dict(report.uncovered()), {0x812D: 5})

    def test_retail_values_for_both_games_are_all_covered(self) -> None:
        """The measured corpus domains, restated: every value is named AND covered."""
        oot = _report(
            min_filter=Counter({0x2600: 13, 0x2601: 4750, 0x2701: 7150, 0x2702: 2, 0x2703: 973}),
            mag_filter=Counter({0x2600: 13, 0x2601: 12875}),
            wrap_s=Counter({GL_REPEAT: 12200, GL_CLAMP_TO_EDGE: 455, GL_MIRRORED_REPEAT: 233}),
            wrap_t=Counter({GL_REPEAT: 12229, GL_CLAMP_TO_EDGE: 447, GL_MIRRORED_REPEAT: 212}),
        )
        mm = _report(
            min_filter=Counter({0x2600: 22, 0x2601: 3911, 0x2700: 1, 0x2701: 5108, 0x2703: 83}),
            mag_filter=Counter({0x2600: 20, 0x2601: 9105}),
            wrap_s=Counter({GL_REPEAT: 8301, GL_MIRRORED_REPEAT: 550, GL_CLAMP_TO_EDGE: 274}),
            wrap_t=Counter({GL_REPEAT: 8345, GL_MIRRORED_REPEAT: 452, GL_CLAMP_TO_EDGE: 328}),
        )
        for report in (oot, mm):
            with self.subTest(game=report.game):
                self.assertEqual(dict(report.unnamed()), {})
                self.assertEqual(dict(report.uncovered()), {})

    def test_all_four_axes_are_checked_for_coverage(self) -> None:
        for axis in ("min_filter", "mag_filter", "wrap_s", "wrap_t"):
            with self.subTest(axis=axis):
                report = _report(**{axis: Counter({0x812D: 1})})
                self.assertEqual(dict(report.uncovered()), {0x812D: 1})


class CountsAreSummedAcrossTextureUnits(unittest.TestCase):
    def test_the_unit_count_is_what_the_caller_aggregates(self) -> None:
        """Units 1 and 2 are included on purpose: multi-stage TEV binds them.

        Zora's water is the three-stage case, and a per-unit sampler bug would only be visible if the
        walk reaches units 1 and 2 rather than stopping after the first.
        """
        self.assertEqual(survey.TEXTURE_UNITS, 3)
        self.assertEqual(survey.BINDING_STRIDE, 0x18)

    def test_the_report_text_names_the_unit_count(self) -> None:
        text = survey.format_report(
            _report(materials_with_tex0=2, units_bound=5, mag_filter=Counter({0x2601: 5}))
        )
        self.assertIn("5 bound texture units", text)
        self.assertIn("3 units each", text)


class EmptyScanRefuses(unittest.TestCase):
    def test_an_unknown_game_is_rejected_with_the_valid_set(self) -> None:
        with self.assertRaisesRegex(ValueError, "unknown corpus"):
            survey.scan("no-such-game")

    def test_the_refusal_message_names_the_broken_scan(self) -> None:
        import inspect

        source = inspect.getsource(survey.scan)
        self.assertIn("the scan is broken, not clean", source)

    def test_the_refusal_fires_when_no_unit_is_bound(self) -> None:
        """A material walk that reaches zero bound units must raise, not return an empty report."""

        class _Empty:
            def __call__(self):
                yield ("nothing", b"\x00" * 64)

        original = survey.iter_corpus
        try:
            survey.iter_corpus = lambda game: _Empty()
            with self.assertRaisesRegex(RuntimeError, "the scan is broken, not clean"):
                survey.scan("oot")
        finally:
            survey.iter_corpus = original


class TheRetailEnumSetIsStillTheOneTheCPlusPlusResolves(unittest.TestCase):
    """The demand table and the supply table must agree, or the verdict is vacuous.

    `HOST_COVERED_*` is transcribed here, which is normally the thing this project refuses to do. It is
    acceptable for exactly one reason: the C++ side is pinned independently by
    `Zelda3DSamplerFilterResolution.*` over the enum space, and the check below fails if the two drift
    apart -- a value the survey calls uncovered while C++ resolves it (or the reverse) is a broken
    claim, and a broken claim is worse than no claim.
    """

    def test_host_covered_min_is_exactly_the_gl_min_domain(self) -> None:
        self.assertEqual(
            survey.HOST_COVERED_MIN,
            {0x2600, 0x2601, 0x2700, 0x2701, MIPMAP_LINEAR_NEAREST, LINEAR_MIPMAP_LINEAR},
        )

    def test_host_covered_mag_excludes_every_mipmapped_enum(self) -> None:
        self.assertFalse(survey.HOST_COVERED_MAG & {0x2700, 0x2701, 0x2702, 0x2703})

    def test_host_covered_wrap_is_a_subset_of_the_named_wrap_enums(self) -> None:
        named = {v for v, name in survey.GL_NAMES.items() if "CLAMP" in name or "REPEAT" in name}
        self.assertTrue(survey.HOST_COVERED_WRAP <= named, "a covered wrap enum must have a name")


if __name__ == "__main__":
    unittest.main()
