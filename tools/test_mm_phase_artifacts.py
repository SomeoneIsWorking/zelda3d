#!/usr/bin/env python3
"""Persistence and scratch-scope falsifiers for MM phase-tour artifacts."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from mm_phase_artifacts import PhaseArtifactError, PhaseArtifactStore


class PhaseArtifactTests(unittest.TestCase):
    def test_rejects_directory_outside_scratch(self) -> None:
        with self.assertRaisesRegex(PhaseArtifactError, "must stay under scratch"):
            PhaseArtifactStore.under_scratch(REPO / "docs" / "must-not-write")

    def test_reads_valid_baseline_pairs_and_ignores_malformed_entries(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            store = PhaseArtifactStore.under_scratch(Path(directory))
            (store.output / "phase_mode_baseline.json").write_text(
                json.dumps(
                    {
                        "pairs": [
                            {"model": 7, "clip": "walk", "phase_mode": "locked"},
                            {"model": "bad"},
                        ]
                    }
                ),
                encoding="utf-8",
            )
            self.assertEqual(store.load_phase_mode_baseline(), {(7, "walk"): "locked"})

    def test_writes_transcript_and_error_summary_through_production_store(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            store = PhaseArtifactStore.under_scratch(Path(directory))
            store.write(
                "unrelated log line\n",
                [{"command": "start", "reply": "ok"}],
                None,
                "failed",
                Path(directory) / "missing-live.log",
            )
            summary = json.loads(
                (store.output / "summary.json").read_text(encoding="utf-8")
            )
            transcript = json.loads(
                (store.output / "transcript.json").read_text(encoding="utf-8")
            )
            self.assertEqual(summary, {"error": "failed", "report": None})
            self.assertEqual(transcript[0]["reply"], "ok")


if __name__ == "__main__":
    unittest.main()
