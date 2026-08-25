#!/usr/bin/env python3
"""Majora exact-process manifest persistence and cleanup falsifiers."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from mm_process import inspect_process
from mm_runtime_manifest import RuntimeInstance, RuntimeManifest
from mm_runtime_test_fixture import runtime_paths


class RuntimeManifestTests(unittest.TestCase):
    def test_round_trips_exact_identity_and_cleans_owned_files(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            paths = runtime_paths(Path(directory))
            paths.runtime_dir.mkdir(parents=True)
            identity = inspect_process(os.getpid())
            assert identity is not None
            instance = RuntimeInstance(
                identity, identity, str(paths.binary), paths.display
            )
            manifest = RuntimeManifest(paths)
            manifest.write(instance)
            self.assertEqual(manifest.read(), instance)
            self.assertEqual(
                paths.pid_file.read_text(encoding="utf-8"), f"{identity.pid}\n"
            )
            manifest.cleanup()
            self.assertIsNone(manifest.read())
            self.assertFalse(paths.pid_file.exists())


if __name__ == "__main__":
    unittest.main()
