"""Tests for narrowly scoped public build-submodule provisioning."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from launcher_bootstrap import native_sources, source_provision

REPO = Path(__file__).resolve().parents[1]


class SourceProvisionTests(unittest.TestCase):
    def setUp(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.repo = Path(self.temporary.name)
        self.checkout_patcher = mock.patch.object(
            source_provision, "require_pinned_checkout"
        )
        self.checkout = self.checkout_patcher.start()
        self.addCleanup(self.checkout_patcher.stop)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _complete(self, relative: Path) -> None:
        # These fixtures reproduce each pinned upstream tree, independently of
        # the provisioner's postcondition declarations.
        build_file = {
            Path("Shipwright/ZAPDTR"): Path("ZAPD/CMakeLists.txt"),
            Path("Shipwright/libultraship/extern/StormLib"): Path("CMakeLists.txt"),
        }[relative]
        path = self.repo / relative / build_file
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()

    def test_zapd_root_cmake_file_does_not_satisfy_nested_upstream_layout(self) -> None:
        root = self.repo / "Shipwright/ZAPDTR"
        root.mkdir(parents=True)
        (root / "CMakeLists.txt").touch()
        with self.assertRaisesRegex(
            source_provision.SourceProvisionError, "refusing to overwrite"
        ):
            source_provision.ensure_build_sources(
                self.repo, lambda _command: self.fail("incomplete source")
            )
        self.checkout.assert_not_called()

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
            [Path(argument) for argument in commands[0][-2:]],
            [
                Path("Shipwright/ZAPDTR"),
                Path("Shipwright/libultraship/extern/StormLib"),
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

    def test_complete_submodules_still_provision_exact_lucent_source(self) -> None:
        for submodule in source_provision.BUILD_SUBMODULES:
            self._complete(submodule.path)
        commands = []
        source_provision.ensure_build_sources(
            self.repo, lambda command: commands.append(command)
        )
        self.assertEqual(commands, [])
        self.checkout.assert_called_once_with(
            native_sources.LUCENT, self.repo / "build/deps/lucent-source"
        )

    def test_wrong_or_dirty_lucent_checkout_refuses_through_native_source_owner(
        self,
    ) -> None:
        for submodule in source_provision.BUILD_SUBMODULES:
            self._complete(submodule.path)
        (self.repo / "build/deps/lucent-source").mkdir(parents=True)
        for outputs in (
            ("wrong-revision", ""),
            (native_sources.LUCENT.revision, " M CMakeLists.txt"),
        ):
            with (
                self.subTest(outputs=outputs),
                mock.patch.object(
                    source_provision,
                    "require_pinned_checkout",
                    native_sources.require_pinned_checkout,
                ),
                mock.patch.object(
                    native_sources.subprocess, "check_output", side_effect=outputs
                ),
                mock.patch.object(native_sources.subprocess, "run") as run,
                self.assertRaisesRegex(
                    source_provision.SourceProvisionError, "lucent source must be clean"
                ),
            ):
                source_provision.ensure_build_sources(
                    self.repo, lambda _command: self.fail("submodules are complete")
                )
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
