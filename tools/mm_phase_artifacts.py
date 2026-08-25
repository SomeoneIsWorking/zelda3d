"""Scratch-scoped persistence for MM animation phase-tour evidence."""

from __future__ import annotations

import json
import shutil
from collections.abc import Iterable, Mapping
from dataclasses import asdict, dataclass
from pathlib import Path

from mm_phase_report import PhaseReport, phase_lines

REPO = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPO / "scratch" / "mm_phase_tour"


class PhaseArtifactError(RuntimeError):
    """The requested artifact destination violates phase-tour persistence policy."""


@dataclass(frozen=True)
class PhaseArtifactStore:
    output: Path

    @classmethod
    def under_scratch(cls, output: Path) -> PhaseArtifactStore:
        resolved = output.resolve()
        scratch = (REPO / "scratch").resolve()
        if not resolved.is_relative_to(scratch):
            raise PhaseArtifactError(
                f"phase-tour artifacts must stay under scratch: {resolved}"
            )
        return cls(resolved)

    def load_phase_mode_baseline(self) -> Mapping[tuple[int, str], str]:
        path = self.output / "phase_mode_baseline.json"
        try:
            pairs = json.loads(path.read_text(encoding="utf-8"))["pairs"]
        except (FileNotFoundError, json.JSONDecodeError, KeyError, TypeError):
            return {}
        baseline: dict[tuple[int, str], str] = {}
        for pair in pairs:
            try:
                baseline[(int(pair["model"]), str(pair["clip"]))] = str(
                    pair["phase_mode"]
                )
            except (KeyError, TypeError, ValueError):
                continue
        return baseline

    def write(
        self,
        log_text: str,
        transcript: Iterable[dict[str, object]],
        report: PhaseReport | None,
        error: str | None,
        live_log: Path,
    ) -> None:
        self.output.mkdir(parents=True, exist_ok=True)
        (self.output / "phase_report.txt").write_text(
            phase_lines(log_text), encoding="utf-8"
        )
        (self.output / "transcript.json").write_text(
            json.dumps(list(transcript), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        (self.output / "summary.json").write_text(
            json.dumps(
                {"error": error, "report": asdict(report) if report else None},
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        if live_log.exists():
            shutil.copyfile(live_log, self.output / "run_mm.log")
        if report is not None:
            baseline = {
                "pairs": [
                    {
                        "model": pair.model,
                        "clip": pair.clip,
                        "phase_mode": pair.phase_mode,
                    }
                    for pair in report.pairs
                ]
            }
            (self.output / "phase_mode_baseline.json").write_text(
                json.dumps(baseline, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
