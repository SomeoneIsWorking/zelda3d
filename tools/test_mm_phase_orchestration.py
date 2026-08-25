#!/usr/bin/env python3
"""Pre-launch policy falsifier for MM phase-tour orchestration."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from mm_phase_artifacts import PhaseArtifactError
from mm_phase_catalog import SCENES
from mm_phase_orchestration import run_tour


class PhaseOrchestrationTests(unittest.TestCase):
    def test_rejects_artifact_directory_outside_scratch_before_launch(self) -> None:
        outside_scratch = TOOLS.parent / "docs" / "must-not-write"
        with self.assertRaisesRegex(PhaseArtifactError, "must stay under scratch"):
            run_tour((SCENES[0],), 1.0, 1.0, 1.0, outside_scratch)


if __name__ == "__main__":
    unittest.main()
