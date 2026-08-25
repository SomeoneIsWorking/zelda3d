#!/usr/bin/env python3
"""Focused tests for OoT runtime paths and launch environment policy."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import oot_game_runtime
import oot_runtime_environment
import oot_runtime_paths
import zelda3d_game


class RuntimePathTests(unittest.TestCase):
    def test_default_instance_is_headless_display_99(self) -> None:
        paths = oot_runtime_paths.OotRuntimePaths.from_environment({})
        self.assertEqual(paths.instance, "")
        self.assertEqual(paths.display, ":99")
        self.assertEqual(paths.repl_fifo.name, "zelda3d.ctl")
        self.assertEqual(paths.pid_file.name, "zelda3d.pid")

    def test_parallel_instance_has_isolated_artifacts(self) -> None:
        paths = oot_runtime_paths.OotRuntimePaths.from_environment(
            {"ZELDA3D_INSTANCE": "4"}
        )
        self.assertEqual(paths.display, ":95")
        self.assertEqual(paths.repl_fifo.name, "zelda3d.4.ctl")
        self.assertEqual(paths.pid_file.name, "zelda3d.4.pid")

    def test_parallel_instance_rejects_invalid_number(self) -> None:
        with self.assertRaisesRegex(ValueError, "positive integer"):
            oot_runtime_paths.OotRuntimePaths.from_environment(
                {"ZELDA3D_INSTANCE": "not-a-number"}
            )


class GameEnvironmentTests(unittest.TestCase):
    def test_runtime_uses_shared_rom_policy_and_preserves_overrides(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            paths = oot_runtime_paths.OotRuntimePaths(
                repo=root,
                binary=root / "build/zelda3d/zelda3d",
                game_dir=root / "build/soh",
                instance="",
                display=":99",
                log=oot_runtime_paths.SCRATCH / "logs/test.log",
                repl_fifo=oot_runtime_paths.SCRATCH / "test.ctl",
                pid_file=oot_runtime_paths.SCRATCH / "test.pid",
                xvfb_log=oot_runtime_paths.SCRATCH / "logs/xvfb-test.log",
            )
            resolved = {
                "ZELDA3D_OOT3D_ROM": str(root / "oot3d.3ds"),
                "ZELDA3D_WARP": "",
                "ZELDA3D_LAUNCHER": "1",
            }
            with (
                mock.patch.object(
                    oot_runtime_environment,
                    "resolve_rom_environment",
                    return_value=resolved.copy(),
                ) as resolve,
                mock.patch.object(
                    oot_runtime_environment, "provision_n64_extraction_rom"
                ) as provision,
            ):
                environment = oot_runtime_environment.resolved_game_environment(
                    paths,
                    entrance="0xEE",
                    daytime="0x6000",
                    environment={"CALLER": "kept"},
                )
            resolve.assert_called_once_with(root, {"CALLER": "kept"})
            provision.assert_called_once_with(paths.game_dir, mock.ANY)
            self.assertEqual(environment["ZELDA3D_WARP"], "")
            self.assertEqual(environment["ZELDA3D_LAUNCHER"], "1")
            self.assertEqual(environment["ZELDA3D_ENTRANCE"], "0xEE")
            self.assertEqual(environment["ZELDA3D_TIME"], "0x6000")
            self.assertEqual(environment["ZELDA3D_REPL"], str(paths.repl_fifo))


class MigrationStructureTests(unittest.TestCase):
    def test_shell_rom_interfaces_are_removed(self) -> None:
        repo = Path(__file__).resolve().parent.parent
        retired = (
            "rom_provision" + ".sh",
            "zelda3d_game" + ".sh",
            "zelda3d_gpu_launch" + ".sh",
        )
        for name in retired:
            self.assertFalse((repo / "tools" / name).exists(), name)

    def test_shipping_python_entry_points_are_executable(self) -> None:
        repo = Path(__file__).resolve().parent.parent
        for name in (
            "rom_provision.py",
            "zelda3d_game.py",
            "zelda3d_gpu_launch.py",
        ):
            path = repo / "tools" / name
            self.assertTrue(path.exists(), name)
            self.assertTrue(os.access(path, os.X_OK), name)


class CliContractTests(unittest.TestCase):
    def test_start_defaults_follow_environment(self) -> None:
        with mock.patch.dict(
            os.environ,
            {"ZELDA3D_ENTRANCE": "0x123", "ZELDA3D_TIME": "0x4567"},
        ):
            self.assertEqual(
                zelda3d_game._start_arguments([]), ("0x123", "0x4567")
            )

    def test_too_many_start_arguments_are_usage_error(self) -> None:
        with self.assertRaises(zelda3d_game.UsageError):
            zelda3d_game._start_arguments(["1", "2", "3"])


if __name__ == "__main__":
    unittest.main()
