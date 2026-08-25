#!/usr/bin/env python3
"""Scratch-scope and global-lock path policy falsifiers."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from mm_runtime_errors import RuntimeOwnershipError
from mm_runtime_paths import RuntimePaths, require_scratch_file, validate_runtime_paths
from mm_runtime_test_fixture import runtime_paths


class RuntimePathTests(unittest.TestCase):
    def test_cleanup_refuses_path_outside_scratch(self) -> None:
        with self.assertRaisesRegex(RuntimeOwnershipError, "outside scratch"):
            require_scratch_file(REPO / "README.md")

    def test_runtime_rejects_artifacts_outside_scratch(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            paths = runtime_paths(Path(directory))
            invalid = RuntimePaths(**{**paths.__dict__, "log": REPO / "run.log"})
            with self.assertRaisesRegex(RuntimeOwnershipError, "under scratch"):
                validate_runtime_paths(invalid)

    def test_runtime_rejects_non_global_lock_inside_scratch(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            paths = runtime_paths(Path(directory))
            invalid = RuntimePaths(
                **{**paths.__dict__, "lock": paths.runtime_dir / "local.lock"}
            )
            with self.assertRaisesRegex(RuntimeOwnershipError, "global path"):
                validate_runtime_paths(invalid)


if __name__ == "__main__":
    unittest.main()
