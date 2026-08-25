"""Positive and negative tests for the focused harness command-line owner."""

from __future__ import annotations

import ast
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import harness_cli


class HarnessCliTests(unittest.TestCase):
    @mock.patch.object(harness_cli, "spawn")
    def test_send_uses_the_focused_process_owner(self, spawn: mock.Mock) -> None:
        session = spawn.return_value.__enter__.return_value
        session.send_multiline.return_value = ["ok scene 85"]

        with redirect_stdout(StringIO()):
            self.assertEqual(harness_cli.main(["send", "scene"]), 0)

        spawn.assert_called_once_with(save_state=None)
        session.send_multiline.assert_called_once_with("scene")

    @mock.patch.object(harness_cli, "spawn")
    def test_send_reads_labeled_multiline_response_without_command_allowlist(
        self, spawn: mock.Mock
    ) -> None:
        session = spawn.return_value.__enter__.return_value
        session.send_multiline.return_value = ["diag:", "frames=2", "ok diag"]

        with redirect_stdout(StringIO()) as stdout:
            self.assertEqual(harness_cli.main(["send", "diag"]), 0)

        self.assertEqual(stdout.getvalue(), "diag:\nframes=2\nok diag\n")

    @mock.patch.object(harness_cli, "spawn")
    def test_wire_error_returns_nonzero(self, spawn: mock.Mock) -> None:
        session = spawn.return_value.__enter__.return_value
        session.send_multiline.return_value = ["err unknown command"]

        with redirect_stdout(StringIO()):
            self.assertEqual(harness_cli.main(["send", "nope"]), 1)

    @mock.patch.object(harness_cli, "spawn")
    def test_timeout_returns_nonzero_without_escaping(self, spawn: mock.Mock) -> None:
        session = spawn.return_value.__enter__.return_value
        session.send_multiline.side_effect = TimeoutError("wire stalled")

        with redirect_stderr(StringIO()) as stderr:
            self.assertEqual(harness_cli.main(["send", "scene"]), 1)

        self.assertIn("wire stalled", stderr.getvalue())

    @mock.patch.object(harness_cli, "spawn", side_effect=RuntimeError("unavailable"))
    def test_spawn_failure_returns_nonzero(self, _spawn: mock.Mock) -> None:
        with redirect_stderr(StringIO()) as stderr:
            self.assertEqual(harness_cli.main(["send", "scene"]), 1)
        self.assertIn("unavailable", stderr.getvalue())


class HarnessCompatibilityRemovalTests(unittest.TestCase):
    def test_deleted_facade_has_no_python_consumers(self) -> None:
        self.assertFalse((TOOLS_DIR / "harness_ctl.py").exists())
        roots = (TOOLS_DIR, TOOLS_DIR.parent / "oot3d-decomp" / "tools")
        consumers: list[str] = []
        for root in roots:
            for path in root.glob("*.py"):
                tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
                for node in ast.walk(tree):
                    imported_module = None
                    if isinstance(node, ast.ImportFrom):
                        imported_module = node.module
                    elif isinstance(node, ast.Import):
                        for alias in node.names:
                            if alias.name == "harness_ctl":
                                consumers.append(
                                    str(path.relative_to(TOOLS_DIR.parent))
                                )
                    if imported_module == "harness_ctl":
                        consumers.append(str(path.relative_to(TOOLS_DIR.parent)))
        self.assertEqual(consumers, [])


if __name__ == "__main__":
    unittest.main()
