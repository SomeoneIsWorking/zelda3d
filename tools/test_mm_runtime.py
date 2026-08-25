#!/usr/bin/env python3
"""Majora lifecycle orchestration ownership falsifier."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from mm_process import inspect_process
from mm_runtime_errors import RuntimeBusy
from mm_runtime_lifecycle import MMRuntime
from mm_runtime_manifest import RuntimeInstance, RuntimeManifest
from mm_runtime_test_fixture import runtime_paths


class RuntimeLifecycleTests(unittest.TestCase):
    def test_start_refuses_live_owned_pid_without_signaling_it(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            paths = runtime_paths(Path(directory))
            runtime = MMRuntime(paths)
            current = inspect_process(os.getpid())
            assert current is not None
            paths.runtime_dir.mkdir(parents=True)
            RuntimeManifest(paths).write(
                RuntimeInstance(current, current, str(paths.binary), paths.display)
            )
            with (
                runtime.lease() as lease,
                patch.object(runtime._launch, "validate_prerequisites"),
                patch("os.kill") as kill,
            ):
                with self.assertRaisesRegex(RuntimeBusy, "already exists"):
                    runtime.start(lease, None)
                kill.assert_not_called()


if __name__ == "__main__":
    unittest.main()
