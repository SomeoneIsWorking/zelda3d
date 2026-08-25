#!/usr/bin/env python3
"""CLI-only falsifiers for the MM phase-tour composition entry point."""

from __future__ import annotations

import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from mm_phase_tour import main


class PhaseTourCliTests(unittest.TestCase):
    def test_lists_scenes_without_starting_runtime(self) -> None:
        output = StringIO()
        with redirect_stdout(output):
            result = main(["--list-scenes"])
        self.assertEqual(result, 0)
        self.assertIn("scene_id=", output.getvalue())

    def test_rejects_nonpositive_timing_before_starting_runtime(self) -> None:
        errors = StringIO()
        with redirect_stderr(errors):
            result = main(["--dwell", "0"])
        self.assertEqual(result, 2)
        self.assertIn("must be positive", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
