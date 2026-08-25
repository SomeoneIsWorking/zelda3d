"""Tests for narrowly scoped public build-submodule provisioning."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from launcher_bootstrap import source_provision

REPO = Path(__file__).resolve().parents[1]


class SourceProvisionTests(unittest.TestCase):
    def setUp(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.repo = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _complete(self, relative: Path) -> None:
        path = self.repo / relative
        path.mkdir(parents=True, exist_ok=True)
        (path / "CMakeLists.txt").touch()

    def test_missing_build_submodules_are_initialized_at_pinned_paths_only(
        self,
    ) -> None:
        commands: list[list[str]] = []

        def runner(command) -> None:
            commands.append(list(command))
            for submodule in source_provision.BUILD_SUBMODULES:
                self._complete(submodule.path)

        source_provision.ensure_build_sources(self.repo, runner)
        self.assertEqual(len(commands), 1)
        self.assertEqual(
            commands[0][-2:],
            [
                "Shipwright/ZAPDTR",
                "Shipwright/libultraship/extern/StormLib",
            ],
        )
        self.assertIn("--depth", commands[0])
        self.assertNotIn("oot3d-decomp", commands[0])
        self.assertNotIn("mm3d-decomp", commands[0])

    def test_initialized_dirty_dependency_is_not_touched(self) -> None:
        initialized = source_provision.BUILD_SUBMODULES[0].path
        self._complete(initialized)
        marker = self.repo / initialized / "local-edit.txt"
        marker.touch()
        commands: list[list[str]] = []

        def runner(command) -> None:
            commands.append(list(command))
            self._complete(source_provision.BUILD_SUBMODULES[1].path)

        source_provision.ensure_build_sources(self.repo, runner)
        self.assertTrue(marker.is_file())
        self.assertNotIn(str(initialized), commands[0])

    def test_incomplete_nonempty_dependency_is_refused(self) -> None:
        incomplete = self.repo / source_provision.BUILD_SUBMODULES[0].path
        incomplete.mkdir(parents=True)
        (incomplete / "local-edit.txt").touch()
        with self.assertRaisesRegex(
            source_provision.SourceProvisionError, "refusing to overwrite"
        ):
            source_provision.ensure_build_sources(self.repo, lambda _command: None)

    def test_runner_must_satisfy_every_source_postcondition(self) -> None:
        with self.assertRaisesRegex(
            source_provision.SourceProvisionError, "still missing"
        ):
            source_provision.ensure_build_sources(self.repo, lambda _command: None)

    def test_complete_sources_are_a_no_op(self) -> None:
        for submodule in source_provision.BUILD_SUBMODULES:
            self._complete(submodule.path)
        commands = []
        source_provision.ensure_build_sources(
            self.repo, lambda command: commands.append(command)
        )
        self.assertEqual(commands, [])


if __name__ == "__main__":
    unittest.main()
