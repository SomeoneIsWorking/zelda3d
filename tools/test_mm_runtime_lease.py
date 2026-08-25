#!/usr/bin/env python3
"""Global Majora lifecycle-lease exclusion falsifier."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from mm_runtime_errors import RuntimeBusy
from mm_runtime_lease import RuntimeLease
from mm_runtime_test_fixture import runtime_paths


class RuntimeLeaseTests(unittest.TestCase):
    def test_global_lock_cannot_be_bypassed_by_runtime_directory(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            first_paths = runtime_paths(Path(directory) / "first")
            second_paths = runtime_paths(Path(directory) / "second")
            with (
                RuntimeLease(first_paths),
                self.assertRaises(RuntimeBusy),
                RuntimeLease(second_paths, blocking=False),
            ):
                self.fail("second lifecycle lease unexpectedly acquired")


if __name__ == "__main__":
    unittest.main()
