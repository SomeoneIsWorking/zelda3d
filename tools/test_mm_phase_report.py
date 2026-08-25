#!/usr/bin/env python3
"""Offline falsifiers for the shipping MM phase-report gate."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from mm_phase_report import PhaseReportError, validate_phase_log


def report_block(
    *,
    status: str = "MOVED",
    low: float = 0.0,
    high: float = 11.5,
    samples: int = 24,
    actors: int = 1,
    count: int = 1,
    stuck_count: int | None = None,
    stuck_denominator: int | None = None,
    phase_mode: str = "phase-locked",
) -> str:
    stuck = int(status == "STUCK") if stuck_count is None else stuck_count
    denominator = count if stuck_denominator is None else stuck_denominator
    pair = (
        ""
        if count == 0
        else (
            f"[MM3D-PHASE] {status:<6} model=18 kamome_fly                  "
            f"f {low:.2f}..{high:.2f} dur=20 n={samples} {phase_mode} morph=3/0.75 actors={actors}\n"
        )
    )
    return (
        f"[MM3D-PHASE] {count} (model,clip) pair(s) sampled\n"
        + pair
        + "[MM3D-PHASE] morph path fired on 3 sample(s) across all pairs.\n"
        + f"[MM3D-PHASE] {stuck} of {denominator} pair(s) with >=2 samples PER ACTOR never advanced.  "
        "(THIN = too few samples per actor to say either way.)\n"
    )


class PhaseReportTests(unittest.TestCase):
    def test_uses_final_complete_report_after_empty_startup_flush(self) -> None:
        result = validate_phase_log(report_block(count=0) + report_block())
        self.assertEqual(result.pair_count, 1)
        self.assertEqual(result.pairs[0].clip, "kamome_fly")

    def test_rejects_zero_final_denominator(self) -> None:
        with self.assertRaisesRegex(PhaseReportError, "sampled zero"):
            validate_phase_log(report_block(count=0))

    def test_rejects_stuck_denominator_mismatch(self) -> None:
        with self.assertRaisesRegex(PhaseReportError, "stuck denominator"):
            validate_phase_log(report_block(stuck_denominator=99))

    def test_rejects_unmapped_animation_even_when_phase_moved(self) -> None:
        log = (
            "[MM3D-ANIM] model=7 unmapped n64='foo' -> default 'foo_wait'\n"
            + report_block()
        )
        with self.assertRaisesRegex(PhaseReportError, "unmapped animation"):
            validate_phase_log(log)

    def test_rejects_static_clip_with_enough_samples_per_actor(self) -> None:
        with self.assertRaisesRegex(PhaseReportError, "never advanced"):
            validate_phase_log(report_block(status="STUCK", high=0.0))

    def test_rejects_all_thin_report_without_observed_motion(self) -> None:
        with self.assertRaisesRegex(PhaseReportError, "every pair as THIN"):
            validate_phase_log(
                report_block(status="THIN", high=0.0, samples=4, actors=4)
            )

    def test_rejects_thin_status_when_samples_and_motion_prove_moved(self) -> None:
        with self.assertRaisesRegex(PhaseReportError, "classification mismatch"):
            validate_phase_log(
                report_block(status="THIN", high=11.5, samples=24, actors=1)
            )

    def test_rejects_all_free_run_mode(self) -> None:
        with self.assertRaisesRegex(
            PhaseReportError, "no sufficiently sampled phase-locked"
        ):
            validate_phase_log(report_block(phase_mode="free-run"))

    def test_rejects_regression_against_prior_phase_mode(self) -> None:
        baseline = {(18, "kamome_fly"): "phase-locked"}
        with self.assertRaisesRegex(PhaseReportError, "baseline regressed"):
            validate_phase_log(
                report_block(phase_mode="free-run"),
                require_phase_locked=False,
                phase_mode_baseline=baseline,
            )


if __name__ == "__main__":
    unittest.main()
