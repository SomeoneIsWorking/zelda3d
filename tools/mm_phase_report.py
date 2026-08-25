#!/usr/bin/env python3
"""Parse and validate the shipping MM3D animation phase report."""

from __future__ import annotations

import re
from collections.abc import Mapping
from dataclasses import dataclass


class PhaseReportError(ValueError):
    pass


@dataclass(frozen=True)
class PhasePair:
    status: str
    model: int
    clip: str
    low: float
    high: float
    duration: float
    samples: int
    phase_mode: str
    morph_samples: int
    morph_max: float
    actors: int

    @property
    def enough_samples(self) -> bool:
        return self.samples >= max(self.actors, 1) * 2

    @property
    def moved(self) -> bool:
        return self.high - self.low > 1e-3

    @property
    def expected_status(self) -> str:
        if not self.enough_samples:
            return "THIN"
        return "MOVED" if self.moved else "STUCK"


@dataclass(frozen=True)
class PhaseReport:
    pair_count: int
    pairs: tuple[PhasePair, ...]
    morph_samples: int
    stated_stuck: int
    stuck_denominator: int

    @property
    def moved_pairs(self) -> tuple[PhasePair, ...]:
        return tuple(
            pair
            for pair in self.pairs
            if pair.status == "MOVED" and pair.enough_samples and pair.moved
        )

    @property
    def phase_locked_pairs(self) -> tuple[PhasePair, ...]:
        return tuple(pair for pair in self.pairs if pair.phase_mode == "phase-locked")


DENOM_RE = re.compile(
    r"^\[MM3D-PHASE\] (\d+) \(model,clip\) pair\(s\) sampled$", re.MULTILINE
)
PAIR_RE = re.compile(
    r"^\[MM3D-PHASE\]\s+(MOVED|STUCK|THIN)\s+model=(-?\d+)\s+(\S+)\s+"
    r"f\s+([-+0-9.eE]+)\.\.([-+0-9.eE]+)\s+dur=([-+0-9.eE]+)\s+n=(\d+)\s+"
    r"(phase-locked|free-run)\s+morph=(\d+)/([-+0-9.eE]+)\s+actors=(\d+)$",
    re.MULTILINE,
)
MORPH_RE = re.compile(
    r"^\[MM3D-PHASE\] morph path fired on (\d+) sample\(s\) across all pairs\.$",
    re.MULTILINE,
)
STUCK_RE = re.compile(
    r"^\[MM3D-PHASE\] (\d+) of (\d+) pair\(s\) with >=2 samples PER ACTOR never advanced\.",
    re.MULTILINE,
)
UNMAPPED_RE = re.compile(r"^\[MM3D-ANIM\].*\bunmapped\b.*$", re.MULTILINE)


def parse_phase_reports(log_text: str) -> tuple[PhaseReport, ...]:
    """Return complete reports; startup may legitimately flush an empty block."""
    starts = list(DENOM_RE.finditer(log_text))
    reports: list[PhaseReport] = []
    for index, denominator in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(log_text)
        block = log_text[denominator.start() : end]
        stuck_match = STUCK_RE.search(block)
        morph_match = MORPH_RE.search(block)
        if stuck_match is None or morph_match is None:
            continue
        pairs = tuple(
            PhasePair(
                status=match.group(1),
                model=int(match.group(2)),
                clip=match.group(3),
                low=float(match.group(4)),
                high=float(match.group(5)),
                duration=float(match.group(6)),
                samples=int(match.group(7)),
                phase_mode=match.group(8),
                morph_samples=int(match.group(9)),
                morph_max=float(match.group(10)),
                actors=int(match.group(11)),
            )
            for match in PAIR_RE.finditer(block)
        )
        reports.append(
            PhaseReport(
                pair_count=int(denominator.group(1)),
                pairs=pairs,
                morph_samples=int(morph_match.group(1)),
                stated_stuck=int(stuck_match.group(1)),
                stuck_denominator=int(stuck_match.group(2)),
            )
        )
    return tuple(reports)


def validate_phase_log(
    log_text: str,
    *,
    require_phase_locked: bool = True,
    phase_mode_baseline: Mapping[tuple[int, str], str] | None = None,
) -> PhaseReport:
    reports = parse_phase_reports(log_text)
    if not reports:
        raise PhaseReportError("no complete [MM3D-PHASE] report was emitted")
    report = reports[-1]
    if report.pair_count == 0:
        raise PhaseReportError("final phase report sampled zero (model,clip) pairs")
    if len(report.pairs) != report.pair_count:
        raise PhaseReportError(
            f"phase report denominator is {report.pair_count}, but {len(report.pairs)} pair rows parsed"
        )
    if report.stuck_denominator != report.pair_count:
        raise PhaseReportError(
            f"stuck denominator is {report.stuck_denominator}, expected {report.pair_count}"
        )

    inconsistent = [
        pair for pair in report.pairs if pair.status != pair.expected_status
    ]
    if inconsistent:
        names = ", ".join(
            f"model={pair.model}/{pair.clip} says {pair.status}, expected {pair.expected_status}"
            for pair in inconsistent
        )
        raise PhaseReportError(f"phase status classification mismatch: {names}")
    if all(pair.status == "THIN" for pair in report.pairs):
        raise PhaseReportError("final phase report classified every pair as THIN")

    unmapped = UNMAPPED_RE.findall(log_text)
    if unmapped:
        preview = "\n  ".join(unmapped[:5])
        raise PhaseReportError(
            f"{len(unmapped)} unmapped animation line(s):\n  {preview}"
        )

    static = [pair for pair in report.pairs if pair.enough_samples and not pair.moved]
    if static:
        names = ", ".join(
            f"model={pair.model}/{pair.clip} n={pair.samples}" for pair in static
        )
        raise PhaseReportError(
            f"{len(static)} sufficiently sampled clip(s) never advanced: {names}"
        )
    if report.stated_stuck != len(static):
        raise PhaseReportError(
            f"phase report says {report.stated_stuck} stuck pair(s), recomputation found {len(static)}"
        )
    if not report.moved_pairs:
        raise PhaseReportError(
            "no sufficiently sampled (model,clip) pair genuinely moved"
        )
    moved_phase_locked = [
        pair for pair in report.moved_pairs if pair.phase_mode == "phase-locked"
    ]
    if require_phase_locked and not moved_phase_locked:
        raise PhaseReportError(
            "no sufficiently sampled phase-locked pair genuinely moved"
        )
    if phase_mode_baseline:
        regressed = [
            pair
            for pair in report.pairs
            if phase_mode_baseline.get((pair.model, pair.clip)) == "phase-locked"
            and pair.phase_mode != "phase-locked"
        ]
        if regressed:
            names = ", ".join(f"model={pair.model}/{pair.clip}" for pair in regressed)
            raise PhaseReportError(
                f"phase-mode baseline regressed to free-run: {names}"
            )
    return report


def phase_lines(log_text: str) -> str:
    lines = [
        line
        for line in log_text.splitlines()
        if line.startswith(("[MM3D-PHASE]", "[MM3D-ANIM]"))
    ]
    return "\n".join(lines) + ("\n" if lines else "")
