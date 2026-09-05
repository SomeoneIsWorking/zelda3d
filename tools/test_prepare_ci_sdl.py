"""Refusal checks for pinned Linux CI SDL source preparation."""

from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

from launcher_bootstrap import native_sources
from tools import prepare_ci_sdl


class CiSdlTests(unittest.TestCase):
    def test_wrong_revision_or_dirty_source_refuses_before_build(self) -> None:
        for outputs in (
            ("wrong\n", ""),
            (native_sources.SDL.revision, " M CMakeLists.txt"),
        ):
            with (
                self.subTest(outputs=outputs),
                mock.patch.object(Path, "exists", return_value=True),
                mock.patch.object(
                    native_sources.subprocess, "check_output", side_effect=outputs
                ),
                mock.patch.object(native_sources.subprocess, "run") as run,
                self.assertRaisesRegex(RuntimeError, "sdl source must be clean"),
            ):
                prepare_ci_sdl.prepare(Path.cwd())
            run.assert_not_called()

    def test_release_dependencies_refuse_wrong_revision_before_cmake(self) -> None:
        for source in (native_sources.TINYXML2, native_sources.GLSLANG):
            with (
                self.subTest(source=source.name),
                mock.patch.object(Path, "exists", return_value=True),
                mock.patch.object(
                    native_sources.subprocess, "check_output", side_effect=("wrong", "")
                ),
                mock.patch.object(native_sources.subprocess, "run") as run,
                self.assertRaisesRegex(RuntimeError, "source must be clean"),
            ):
                native_sources.build_source(source, Path.cwd(), Path.cwd())
            run.assert_not_called()

    def test_verified_source_builds_and_installs_using_declared_policy(self) -> None:
        with (
            mock.patch.object(Path, "exists", return_value=True),
            mock.patch.object(
                native_sources.subprocess,
                "check_output",
                side_effect=(native_sources.SDL.revision, ""),
            ),
            mock.patch.object(native_sources.subprocess, "run") as run,
        ):
            prefix = prepare_ci_sdl.prepare(Path.cwd())
        self.assertEqual(prefix, Path.cwd() / "build/deps/sdl")
        commands = [call.args[0] for call in run.call_args_list]
        self.assertTrue(
            all(option in commands[0] for option in native_sources.SDL.options)
        )
        self.assertEqual(commands[1][-3:], ["--target", "install", "-j2"])
