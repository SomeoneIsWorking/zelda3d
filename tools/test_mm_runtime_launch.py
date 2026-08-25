#!/usr/bin/env python3
"""Majora launch-environment policy falsifiers."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from mm_runtime_launch import RuntimeLaunchProvisioner
from mm_runtime_test_fixture import runtime_paths


class RuntimeLaunchEnvironmentTests(unittest.TestCase):
    def test_environment_is_headless_correlated_and_extra_values_win(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            paths = runtime_paths(Path(directory))
            (paths.repo / ".env").write_text(
                "FROM_DOTENV=yes\nEXTRA=dotenv\n", encoding="utf-8"
            )
            provisioner = RuntimeLaunchProvisioner(paths)
            env = provisioner.environment(
                "0x5400",
                {"EXTRA": "argument"},
                environ={"WAYLAND_DISPLAY": "wayland-0"},
            )
            self.assertNotIn("WAYLAND_DISPLAY", env)
            self.assertEqual(env["DISPLAY"], paths.display)
            self.assertEqual(env["SHIP_SCRIPTED_FIFO"], str(paths.input_fifo))
            self.assertEqual(env["ZELDA3D_MM_REPL"], str(paths.repl_fifo))
            self.assertEqual(env["ZELDA3D_MM_ENTRANCE"], "0x5400")
            self.assertEqual(env["FROM_DOTENV"], "yes")
            self.assertEqual(env["EXTRA"], "argument")

    def test_rejects_invalid_entrance_and_display(self) -> None:
        scratch = REPO / "scratch"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            paths = runtime_paths(Path(directory))
            provisioner = RuntimeLaunchProvisioner(paths)
            with self.assertRaises(ValueError):
                provisioner.environment("not-an-integer", None, environ={})
            invalid_display = type(paths)(**{**paths.__dict__, "display": "wayland"})
            with self.assertRaisesRegex(RuntimeError, "display syntax"):
                RuntimeLaunchProvisioner(invalid_display).display_socket()


if __name__ == "__main__":
    unittest.main()
