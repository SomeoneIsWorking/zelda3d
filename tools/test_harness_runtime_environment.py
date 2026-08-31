#!/usr/bin/env python3
"""Focused tests for harness ROM, display, allocator, and spawn ownership."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import harness_process
import harness_transport
from harness_allocator import select_scalable_allocator
from harness_build import HarnessBuild
from harness_headless_display import prepare_headless_display
from harness_rom_environment import provision_rom_environment
from harness_runtime_inputs import link_runtime_inputs

SCRATCH_TESTS = REPO / "scratch" / "tests"
SCRATCH_TESTS.mkdir(parents=True, exist_ok=True)


class HarnessRomEnvironmentTests(unittest.TestCase):
    def test_canonical_drop_in_rom_is_selected(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            rom = repo / "oot3d.3ds"
            rom.write_bytes(b"fixture")
            environment: dict[str, str] = {}

            provision_rom_environment(repo, environment)

        self.assertEqual(environment["ZELDA3D_OOT3D_ROM"], str(rom))

    def test_missing_explicit_rom_is_rejected(self) -> None:
        environment = {"ZELDA3D_OOT3D_ROM": str(REPO / "missing.3ds")}
        with self.assertRaisesRegex(RuntimeError, "does not exist"):
            provision_rom_environment(REPO, environment)


class HarnessHeadlessDisplayTests(unittest.TestCase):
    def test_ready_display_updates_the_child_environment_without_spawning(self) -> None:
        environment = {"WAYLAND_DISPLAY": "wayland-0"}
        process_factory = mock.Mock()

        prepare_headless_display(
            REPO,
            environment,
            runner=mock.Mock(),
            process_factory=process_factory,
        )

        process_factory.assert_not_called()
        self.assertEqual(environment["DISPLAY"], ":99")
        self.assertNotIn("WAYLAND_DISPLAY", environment)

    def test_display_startup_timeout_is_an_error(self) -> None:
        runner = mock.Mock(side_effect=subprocess.CalledProcessError(1, "xdpyinfo"))
        with (
            tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory,
            self.assertRaisesRegex(RuntimeError, "failed to come up"),
        ):
            prepare_headless_display(
                Path(directory),
                {},
                runner=runner,
                process_factory=mock.Mock(),
                sleeper=mock.Mock(),
            )


class HarnessAllocatorTests(unittest.TestCase):
    def test_installed_allocator_is_selected(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            allocator = Path(directory) / "liballocator.so"
            allocator.write_bytes(b"fixture")
            environment: dict[str, str] = {}

            select_scalable_allocator(environment, (str(allocator),))

        self.assertEqual(environment["LD_PRELOAD"], str(allocator))

    def test_explicit_allocator_wins(self) -> None:
        environment = {"LD_PRELOAD": "caller.so"}
        select_scalable_allocator(environment, (str(REPO / "missing.so"),))
        self.assertEqual(environment["LD_PRELOAD"], "caller.so")


class HarnessRuntimeInputTests(unittest.TestCase):
    def test_existing_runtime_input_is_linked_next_to_harness(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            source = repo / "Shipwright" / "build-cmake" / "soh"
            source.mkdir(parents=True)
            archive = source / "soh.o2r"
            archive.write_bytes(b"fixture")
            build = HarnessBuild.for_repo(repo, 1)

            link_runtime_inputs(build)

            link = build.binary.parent / "soh.o2r"
            self.assertTrue(link.is_symlink())
            self.assertEqual(link.resolve(), archive.resolve())

    def test_regular_runtime_input_is_not_overwritten(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            source = repo / "Shipwright" / "build-cmake" / "soh"
            source.mkdir(parents=True)
            (source / "soh.o2r").write_bytes(b"source")
            build = HarnessBuild.for_repo(repo, 1)
            build.binary.parent.mkdir(parents=True)
            destination = build.binary.parent / "soh.o2r"
            destination.write_bytes(b"owned")

            link_runtime_inputs(build)

            self.assertFalse(destination.is_symlink())
            self.assertEqual(destination.read_bytes(), b"owned")

    def test_runtime_inputs_follow_the_configured_shipping_build(self) -> None:
        with tempfile.TemporaryDirectory(dir=SCRATCH_TESTS) as directory:
            repo = Path(directory)
            shipping = repo / "custom-build"
            source = shipping / "soh"
            source.mkdir(parents=True)
            archive = source / "soh.o2r"
            archive.write_bytes(b"fixture")
            build = HarnessBuild.for_repo(repo, 1, shipping)

            link_runtime_inputs(build)

            self.assertEqual((build.binary.parent / "soh.o2r").resolve(), archive.resolve())


class HarnessProcessTests(unittest.TestCase):
    @mock.patch.object(harness_process, "Harness")
    def test_spawn_only_constructs_process_and_loads_state(
        self, harness_type: mock.Mock
    ) -> None:
        session = harness_type.return_value
        session.send.return_value = "ok loadstate"

        self.assertIs(harness_process.spawn("fixture.state"), session)

        harness_type.assert_called_once_with([str(harness_process.HARNESS_LAUNCHER)], None)
        session.send.assert_called_once_with("loadstate fixture.state")

    @mock.patch.object(harness_process, "Harness")
    def test_failed_loadstate_closes_the_owned_process(
        self, harness_type: mock.Mock
    ) -> None:
        session = harness_type.return_value
        session.send.return_value = "err missing"

        with self.assertRaisesRegex(RuntimeError, "loadstate failed"):
            harness_process.spawn("missing.state")

        session.close.assert_called_once_with()


class HarnessTransportTests(unittest.TestCase):
    def test_boot_wait_covers_the_launcher_build_phase(self) -> None:
        process = mock.Mock()
        with (
            mock.patch.object(harness_transport.subprocess, "Popen", return_value=process),
            mock.patch.object(
                harness_transport.Harness, "_readline", return_value="boot succeeded"
            ) as readline,
        ):
            harness_transport.Harness(["fixture-harness"])

        readline.assert_called_once_with(timeout=harness_transport.BOOT_TIMEOUT_SECONDS)


if __name__ == "__main__":
    unittest.main()
