#!/usr/bin/env python3
"""Exact Linux process-identity falsifiers for Majora runtime ownership."""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from mm_process import ProcessIdentity, inspect_process, same_process


class ProcessIdentityTests(unittest.TestCase):
    def test_pid_generation_not_pid_alone_defines_identity(self) -> None:
        current = inspect_process(os.getpid())
        self.assertIsNotNone(current)
        assert current is not None
        self.assertTrue(same_process(current))
        reused = ProcessIdentity(
            current.pid, current.start_ticks + 1, current.executable, current.argv
        )
        self.assertFalse(same_process(reused))


if __name__ == "__main__":
    unittest.main()
