#!/usr/bin/env python3
"""Tests for Azahar-specific harness build inputs and artifacts."""

from __future__ import annotations

import stat
import sys
import tempfile
import unittest
from collections.abc import Sequence
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import harness_build
import soh3d_harness


class Fixture:
    def __init__(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.repo = Path(self.temporary.name)
        (self.repo / "Azahar").mkdir()
        (self.repo / "Azahar" / "CMakeLists.txt").write_text("project(fixture)\n")
        wire = self.repo / "tools" / "soh3d_harness"
        wire.mkdir(parents=True)
        (wire / "wire_in.cmake").write_text("# fixture\n")
        self.build = harness_build.HarnessBuild.for_repo(self.repo, 3)

    def close(self) -> None:
        self.temporary.cleanup()

    def write_toolchain(
        self, compiler: str = "/usr/bin/clang++", compiler_id: str = "Clang"
    ) -> None:
        self.build.build_dir.mkdir(parents=True, exist_ok=True)
        (self.build.build_dir / "build.ninja").write_text("# fixture\n")
        (self.build.build_dir / "CMakeCache.txt").write_text(
            f"CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_CXX_COMPILER:FILEPATH={compiler}\n"
        )
        (self.build.build_dir / "compile_commands.json").write_text("[]\n")
        metadata = self.build.build_dir / "CMakeFiles" / "fixture"
        metadata.mkdir(parents=True, exist_ok=True)
        (metadata / "CMakeCXXCompiler.cmake").write_text(
            f'set(CMAKE_CXX_COMPILER_ID "{compiler_id}")\n'
        )

    def write_binary(self) -> None:
        self.build.binary.parent.mkdir(parents=True, exist_ok=True)
        self.build.binary.write_text("fixture\n")
        self.build.binary.chmod(self.build.binary.stat().st_mode | stat.S_IXUSR)


class HarnessBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = Fixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_complete_ninja_tree_builds_without_reconfigure(self) -> None:
        self.fixture.write_toolchain()
        self.fixture.write_binary()
        commands: list[list[str]] = []
        harness_build.ensure_harness_build(
            self.fixture.build, lambda command: commands.append(list(command))
        )
        self.assertEqual(
            commands,
            [
                [
                    "ninja",
                    "-C",
                    str(self.fixture.build.build_dir),
                    "-j3",
                    "soh3d_harness",
                ]
            ],
        )

    def test_gnu_tree_is_reused_without_forcing_clang(self) -> None:
        self.fixture.write_toolchain("/usr/bin/g++", "GNU")
        self.fixture.write_binary()
        commands: list[list[str]] = []

        harness_build.ensure_harness_build(
            self.fixture.build, lambda command: commands.append(list(command))
        )
        self.assertEqual(
            commands,
            [
                [
                    "ninja",
                    "-C",
                    str(self.fixture.build.build_dir),
                    "-j3",
                    "soh3d_harness",
                ]
            ],
        )

    def test_configure_preserves_headless_azahar_options(self) -> None:
        command = harness_build.configure_command(self.fixture.build)

        self.assertIn("-DENABLE_LIBRETRO=ON", command)
        self.assertIn("-DENABLE_QT=OFF", command)
        self.assertIn("-DENABLE_VULKAN=ON", command)
        self.assertIn("-DENABLE_SOFTWARE_RENDERER=ON", command)
        self.assertIn(
            f"-DCMAKE_PROJECT_citra_INCLUDE={self.fixture.build.wire_in}", command
        )
        self.assertIn(
            f"-DZELDA3D_SHIPPING_BUILD_DIR={self.fixture.build.shipping_build_dir}",
            command,
        )

    def test_configure_forwards_python_without_forcing_a_compiler(self) -> None:
        command = harness_build.configure_command(self.fixture.build, "/locked/python")

        self.assertIn("-DPython3_EXECUTABLE=/locked/python", command)
        self.assertFalse(any("CMAKE_CXX_COMPILER" in item for item in command))

    def test_configure_rejects_incomplete_metadata(self) -> None:
        def runner(command: Sequence[str]) -> None:
            if "-S" in command:
                self.fixture.write_toolchain("", "")

        with self.assertRaisesRegex(
            harness_build.HarnessBuildError, "complete Ninja"
        ):
            harness_build.ensure_harness_build(self.fixture.build, runner)

    def test_missing_compile_commands_reconfigures(self) -> None:
        self.fixture.write_toolchain()
        (self.fixture.build.build_dir / "compile_commands.json").unlink()
        self.fixture.write_binary()
        commands: list[list[str]] = []

        def runner(command: Sequence[str]) -> None:
            commands.append(list(command))
            if "-S" in command:
                (self.fixture.build.build_dir / "compile_commands.json").write_text("[]\n")

        harness_build.ensure_harness_build(self.fixture.build, runner)
        self.assertIn("-S", commands[0])
        self.assertEqual(commands[-1][-1], "soh3d_harness")

    def test_missing_binary_after_target_is_an_error(self) -> None:
        self.fixture.write_toolchain()
        with self.assertRaisesRegex(
            harness_build.HarnessBuildError, "missing or not executable"
        ):
            harness_build.ensure_harness_build(
                self.fixture.build, lambda _command: None
            )


class HarnessLauncherTests(unittest.TestCase):
    @mock.patch.object(soh3d_harness.os, "execvpe")
    @mock.patch.object(soh3d_harness, "select_scalable_allocator")
    @mock.patch.object(soh3d_harness, "prepare_headless_display")
    @mock.patch.object(soh3d_harness, "link_runtime_inputs")
    @mock.patch.object(soh3d_harness, "ensure_harness_build")
    @mock.patch.object(soh3d_harness, "apply_repo_environment")
    def test_explicit_rom_is_forwarded_without_default_provisioning(
        self,
        load_environment: mock.Mock,
        ensure_build: mock.Mock,
        link_inputs: mock.Mock,
        ensure_headless: mock.Mock,
        ensure_malloc: mock.Mock,
        exec_process: mock.Mock,
    ) -> None:
        shipping_build = REPO / "scratch" / "custom-shipping-build"
        with (
            mock.patch.object(soh3d_harness, "provision_rom_environment") as provision,
            mock.patch.dict(
                soh3d_harness.os.environ,
                {
                    "ZELDA3D_BUILD_JOBS": "2",
                    "ZELDA3D_SHIPPING_BUILD_DIR": str(shipping_build),
                },
                clear=False,
            ),
            mock.patch.object(soh3d_harness.os, "chdir"),
        ):
            self.assertEqual(soh3d_harness.main(["game.3ds", "--probe"]), 0)
            launched_environment = dict(exec_process.call_args.args[2])

        provision.assert_not_called()
        load_environment.assert_called_once_with(REPO, soh3d_harness.os.environ)
        ensure_build.assert_called_once()
        link_inputs.assert_called_once()
        ensure_headless.assert_called_once_with(REPO, soh3d_harness.os.environ)
        ensure_malloc.assert_called_once_with(soh3d_harness.os.environ)
        build = ensure_build.call_args.args[0]
        self.assertEqual(build.shipping_build_dir, shipping_build)
        self.assertEqual(
            launched_environment["ZELDA3D_SHIPPING_BUILD_DIR"],
            str(shipping_build),
        )
        executable = str(build.binary)
        exec_process.assert_called_once_with(
            executable, [executable, "game.3ds", "--probe"], soh3d_harness.os.environ
        )

    @mock.patch.object(soh3d_harness, "apply_repo_environment")
    def test_invalid_job_count_returns_failure(
        self, _load_environment: mock.Mock
    ) -> None:
        with (
            mock.patch.dict(
                soh3d_harness.os.environ, {"ZELDA3D_BUILD_JOBS": "0"}, clear=False
            ),
            mock.patch.object(soh3d_harness, "ensure_harness_build") as ensure_build,
            redirect_stderr(StringIO()),
        ):
            self.assertEqual(soh3d_harness.main(["game.3ds"]), 1)
        ensure_build.assert_not_called()

    @mock.patch.object(soh3d_harness, "provision_rom_environment")
    @mock.patch.object(soh3d_harness, "apply_repo_environment")
    def test_help_prints_usage_without_building_or_booting(
        self,
        _load_environment: mock.Mock,
        provision: mock.Mock,
    ) -> None:
        for flag in ("-h", "--help"):
            with (
                self.subTest(flag=flag),
                mock.patch.object(soh3d_harness, "ensure_harness_build") as ensure_build,
                mock.patch.object(
                    soh3d_harness.os, "execvpe"
                ) as exec_process,
                redirect_stdout(StringIO()) as printed,
            ):
                self.assertEqual(soh3d_harness.main([flag]), 0)
                self.assertIn("usage: soh3d_harness.py", printed.getvalue())
                self.assertIn("harness_cli.py", printed.getvalue())
            provision.assert_not_called()
            ensure_build.assert_not_called()
            exec_process.assert_not_called()


    @mock.patch.object(soh3d_harness.os, "execvpe")
    @mock.patch.object(soh3d_harness, "select_scalable_allocator")
    @mock.patch.object(soh3d_harness, "prepare_headless_display")
    @mock.patch.object(soh3d_harness, "link_runtime_inputs")
    @mock.patch.object(soh3d_harness, "ensure_harness_build")
    @mock.patch.object(soh3d_harness, "provision_rom_environment")
    @mock.patch.object(soh3d_harness, "apply_repo_environment")
    def test_default_launch_prepares_each_owner_exactly_once(
        self,
        apply_environment: mock.Mock,
        provision_rom: mock.Mock,
        ensure_build: mock.Mock,
        link_inputs: mock.Mock,
        prepare_display: mock.Mock,
        select_allocator: mock.Mock,
        _exec_process: mock.Mock,
    ) -> None:
        with (
            mock.patch.dict(
                soh3d_harness.os.environ, {"ZELDA3D_BUILD_JOBS": "1"}, clear=False
            ),
            mock.patch.object(soh3d_harness.os, "chdir"),
        ):
            self.assertEqual(soh3d_harness.main([]), 0)

        apply_environment.assert_called_once_with(REPO, soh3d_harness.os.environ)
        provision_rom.assert_called_once_with(REPO, soh3d_harness.os.environ)
        ensure_build.assert_called_once()
        link_inputs.assert_called_once()
        prepare_display.assert_called_once_with(REPO, soh3d_harness.os.environ)
        select_allocator.assert_called_once_with(soh3d_harness.os.environ)


if __name__ == "__main__":
    unittest.main()
