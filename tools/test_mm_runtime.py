#!/usr/bin/env python3
"""Majora lifecycle orchestration ownership falsifier."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from mm_process import ProcessIdentity, inspect_process
from mm_runtime_errors import RuntimeBusy
from mm_runtime_lease import HeldLeaseProof
from mm_runtime_lifecycle import MMRuntime
from mm_runtime_manifest import RuntimeInstance, RuntimeManifest
from mm_runtime_test_fixture import runtime_paths


class RuntimeLifecycleTests(unittest.TestCase):
    def test_start_clears_stale_game_log_before_child_spawn(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            paths = runtime_paths(Path(directory))
            paths.runtime_dir.mkdir(parents=True)
            paths.log.write_text("stale run\n", encoding="utf-8")
            runtime = MMRuntime(paths)

            with (
                patch.object(runtime, "_prepare_start"),
                patch.object(runtime._launch, "environment", return_value={}),
                patch.object(
                    runtime._launch,
                    "display_socket",
                    return_value=paths.runtime_dir / "missing-display",
                ),
                patch(
                    "mm_runtime_lifecycle.subprocess.Popen",
                    side_effect=RuntimeError("spawn failed"),
                ),
                self.assertRaisesRegex(RuntimeError, "spawn failed"),
            ):
                runtime.start(HeldLeaseProof(), None)

            self.assertEqual(paths.log.read_text(encoding="utf-8"), "")

    def test_capture_waits_for_expected_argv_after_child_exec(self) -> None:
        paths = runtime_paths(REPO / "scratch" / "mm_runtime_capture_test")
        runtime = MMRuntime(paths)
        expected_argv = ("Xvfb", ":199", "-screen", "0", "1280x960x24")
        before_exec = ProcessIdentity(4312, 100, "/usr/bin/Xvfb", ())
        after_exec = ProcessIdentity(4312, 100, "/usr/bin/Xvfb", expected_argv)
        process = SimpleNamespace(pid=4312, poll=lambda: None)

        with patch(
            "mm_runtime_lifecycle.inspect_process",
            side_effect=(before_exec, after_exec),
        ):
            captured = runtime._capture_started_process(
                process,  # type: ignore[arg-type]
                "Xvfb",
                expected_argv,
            )

        self.assertEqual(captured, after_exec)

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
