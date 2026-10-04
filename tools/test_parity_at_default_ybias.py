#!/usr/bin/env python3
"""Tests for tools/parity_at_default_ybias.py -- can the discriminator tell right from wrong?

The standing failure this file exists to prevent is an instrument that reports success while
measuring nothing, or that reports the same verdict whatever the port does. So each case asserts a
POSITIVE control (the recovered 400-per-authored-update producer PASSes) alongside the NEGATIVE
control it needs (a producer with the accumulator dropped, or with a wrong threshold, FAILS and names
the frame).

Both controls run through the same `check_trace` the live command uses, so a test cannot pass while
the live path disagrees.

Run: python3 tools/test_parity_at_default_ybias.py
"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent


def _load():
    spec = importlib.util.spec_from_file_location("parity_at_default_ybias", HERE / "parity_at_default_ybias.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


PAD = _load()
Sample = PAD.Sample


def _sample(frame, active, owned, rise, acc, ticks, walk=1, static=1, floor_type=0):
    return Sample(
        frame=frame,
        active=active,
        branch_owned=owned,
        rise=rise,
        accumulator=acc,
        walk_run=walk,
        get_item=0,
        floor_type=floor_type,
        static_floor=static,
        authored_updates=ticks,
        y_bias=acc * PAD.BIAS_SCALE if active else 0.0,
    )


def recovered_trajectory(rise=10.0):
    """The trace a correct port produces for one 10-unit rise on a flat static floor, with the 30:20
    accumulator in its 2,1,2,1 phase (the phase the live trace starts in)."""
    return [
        _sample(0, 0, 1, 0.0, 0.0, 2),
        _sample(1, 0, 1, 0.0, 0.0, 1),
        _sample(2, 1, 1, rise, rise * 100.0, 2),
        _sample(3, 1, 1, 0.0, rise * 100.0 - 400.0, 1),
        _sample(4, 0, 1, 0.0, 0.0, 2),
        _sample(5, 0, 1, 0.0, 0.0, 1),
    ]


def no_accumulator_trajectory(rise=10.0):
    """The same rise, but the decay is 400 per HOST update -- the accumulator dropped. This is the
    live control's shape: a port that looks right on the rise frame and wrong on the tail."""
    acc = rise * 100.0
    return [
        _sample(0, 0, 1, 0.0, 0.0, 2),
        _sample(1, 0, 1, 0.0, 0.0, 1),
        _sample(2, 1, 1, rise, acc, 2),
        _sample(3, 1, 1, 0.0, acc - 400.0, 1),
        _sample(4, 1, 1, 0.0, acc - 800.0, 2),
        _sample(5, 0, 1, 0.0, 0.0, 1),
    ]


class PositiveControlTests(unittest.TestCase):
    """The recovered producer must PASS, or every negative control below proves nothing."""

    def test_recovered_producer_passes(self):
        verdict = PAD.check_trace(recovered_trajectory())
        self.assertEqual(verdict.status, "PASS", verdict.discrepancies and [d.describe() for d in verdict.discrepancies])

    def test_recovered_producer_reports_the_expected_rate(self):
        verdict = PAD.check_trace(recovered_trajectory())
        self.assertEqual(len(verdict.events), 1)
        self.assertAlmostEqual(verdict.events[0]["decay_units_per_second"], 12000.0, delta=1.0)

    def test_latch_sets_and_clears(self):
        rows = recovered_trajectory()
        self.assertEqual(rows[1].active, 0)
        self.assertEqual(rows[2].active, 1)
        self.assertEqual(rows[4].active, 0)


class NegativeControlTests(unittest.TestCase):
    """Each case is a specific way the port could be wrong that the live run cannot show by eye."""

    def test_dropped_accumulator_fails_and_names_the_frame(self):
        verdict = PAD.check_trace(no_accumulator_trajectory())
        self.assertEqual(verdict.status, "FAIL")
        frames = {d.frame for d in verdict.discrepancies}
        self.assertIn(4, frames, "the accumulator-disagreeing frame must be reported")

    def test_dropped_accumulator_reports_the_wrong_rate(self):
        verdict = PAD.check_trace(no_accumulator_trajectory())
        self.assertEqual(verdict.events[0]["decay_units_per_second"], 8000.0)

    def test_wrong_rise_scale_fails(self):
        rows = recovered_trajectory()
        rows[2].accumulator = 10.0 * 10.0  # a 10x mistuned rise scale
        rows[2].y_bias = rows[2].accumulator * PAD.BIAS_SCALE
        verdict = PAD.check_trace(rows)
        self.assertEqual(verdict.status, "FAIL")

    def test_missing_threshold_fails(self):
        """A port that latched on any nonzero rise (the `>= 9` gate dropped) must be caught."""
        rows = recovered_trajectory(rise=10.0)
        rows.insert(3, _sample(3, 1, 1, 0.5, 10.0 * 100.0 - 400.0 + 50.0, 1))
        for offset, row in enumerate(rows):
            row.frame = offset
        verdict = PAD.check_trace(rows)
        self.assertEqual(verdict.status, "FAIL")
        self.assertTrue(any(d.field == "accumulator" for d in verdict.discrepancies))

    def test_slower_decay_fails(self):
        rows = recovered_trajectory()
        rows[4].accumulator = 400.0  # cleared a tick later than the recovered rule says
        rows[4].y_bias = 0.0
        verdict = PAD.check_trace(rows)
        self.assertEqual(verdict.status, "FAIL")

    def test_wrong_consumer_scale_fails(self):
        rows = recovered_trajectory()
        rows[3].y_bias = rows[3].accumulator * -0.02  # -0.02f instead of the recovered -0.01f
        verdict = PAD.check_trace(rows)
        self.assertEqual(verdict.status, "FAIL")
        self.assertTrue(any(d.field == "yBias" for d in verdict.discrepancies))

    def test_slope_floor_owned_by_the_stock_branch_is_not_a_rise_event(self):
        """Floor types 4/7/12 without the get-item action leave unk_6C4 to the stock slope path, so a
        big rise there must not be counted as a qualifying event for this producer."""
        rows = [
            _sample(0, 0, 0, 0.0, 0.0, 2, floor_type=4),
            _sample(1, 0, 0, 12.0, 0.0, 1, floor_type=4),
            _sample(2, 0, 0, 0.0, 0.0, 2, floor_type=4),
        ]
        verdict = PAD.check_trace(rows)
        self.assertEqual(verdict.qualifying_rises, 0)
        self.assertEqual(verdict.status, "INCONCLUSIVE")
        self.assertIn("action/floor terms", verdict.skipped_reason)


class InconclusiveTests(unittest.TestCase):
    """A trace that never exercises the predicate must never be reported as a pass."""

    def test_flat_walk_is_inconclusive(self):
        rows = [_sample(index, 0, 1, 0.0, 0.0, 1 + index % 2) for index in range(8)]
        verdict = PAD.check_trace(rows)
        self.assertEqual(verdict.status, "INCONCLUSIVE")
        self.assertIn("never ran", verdict.skipped_reason)

    def test_empty_trace_is_inconclusive(self):
        verdict = PAD.check_trace([])
        self.assertEqual(verdict.status, "INCONCLUSIVE")
        self.assertFalse(verdict.ok)


class CompareTests(unittest.TestCase):
    def test_compare_requires_both_sides(self):
        result = PAD.compare(recovered_trajectory(), no_accumulator_trajectory())
        self.assertEqual(result["expected_units_per_second"], 12000.0)
        self.assertEqual(result["host_units_per_second"], [12000.0])
        self.assertEqual(result["oracle_units_per_second"], [8000.0])
        self.assertFalse(result["agrees"])

    def test_compare_accepts_matching_rates_at_different_update_rates(self):
        """The whole point of the rate form: 20 Hz host and 30 Hz oracle observing the same 400-unit
        authored decay must land on the same units/second."""
        oracle_rows = [
            _sample(0, 0, 1, 0.0, 0.0, 1),
            _sample(1, 1, 1, 10.0, 1000.0, 1),
            _sample(2, 1, 1, 0.0, 600.0, 1),
            _sample(3, 0, 1, 0.0, 0.0, 1),
        ]
        result = PAD.compare(recovered_trajectory(), oracle_rows)
        self.assertTrue(result["agrees"], result)
        self.assertAlmostEqual(result["oracle_units_per_second"][0], 12000.0, delta=1.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)