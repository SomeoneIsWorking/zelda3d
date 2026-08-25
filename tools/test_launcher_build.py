#!/usr/bin/env python3
"""Tests for launcher-specific build inputs and artifacts."""

from __future__ import annotations

import stat
import sys
import tempfile
import unittest
from collections.abc import Sequence
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import launcher_build


class BuildFixture:
    def __init__(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.repo = Path(self.temporary.name)
        (self.repo / "Shipwright" / "libultraship").mkdir(parents=True)
        (self.repo / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.26)\n"
        )
        (self.repo / "Shipwright" / "libultraship" / "CMakeLists.txt").write_text(
            "project(fixture)\n"
        )
        self.build = launcher_build.BuildLayout.for_repo(self.repo, jobs=3)

    def close(self) -> None:
        self.temporary.cleanup()

    def write_toolchain(
        self,
        c: str = "/usr/bin/clang",
        cxx: str = "/usr/bin/clang++",
        c_id: str = "Clang",
        cxx_id: str = "Clang",
        generator: str = "Ninja",
        tests: str = "ON",
        python_executable: str | None = None,
    ) -> None:
        self.build.build_dir.mkdir(parents=True, exist_ok=True)
        (self.build.build_dir / "build.ninja").write_text("# fixture\n")
        cache = (
            f"CMAKE_GENERATOR:INTERNAL={generator}\n"
            f"CMAKE_C_COMPILER:FILEPATH={c}\n"
            f"CMAKE_CXX_COMPILER:FILEPATH={cxx}\n"
            f"LUS_BUILD_TESTS:BOOL={tests}\n"
        )
        if python_executable is not None:
            cache += f"Python3_EXECUTABLE:FILEPATH={python_executable}\n"
        (self.build.build_dir / "CMakeCache.txt").write_text(cache)
        metadata = self.build.build_dir / "CMakeFiles" / "fixture"
        metadata.mkdir(parents=True, exist_ok=True)
        (metadata / "CMakeCCompiler.cmake").write_text(
            f'set(CMAKE_C_COMPILER_ID "{c_id}")\n'
        )
        (metadata / "CMakeCXXCompiler.cmake").write_text(
            f'set(CMAKE_CXX_COMPILER_ID "{cxx_id}")\n'
        )

    def write_binary(self) -> None:
        self.build.binary.parent.mkdir(parents=True, exist_ok=True)
        self.build.binary.write_text("fixture\n")
        self.build.binary.chmod(self.build.binary.stat().st_mode | stat.S_IXUSR)

    def write_runtime_archives(self, contents: str = "fixture\n") -> None:
        for archive in self.build.runtime_archives:
            archive.parent.mkdir(parents=True, exist_ok=True)
            archive.write_text(contents)


class LauncherBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = BuildFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_valid_ninja_tree_builds_authoritative_unified_target(self) -> None:
        self.fixture.write_toolchain()
        commands: list[list[str]] = []

        def runner(command: Sequence[str]) -> None:
            commands.append(list(command))
            self.fixture.write_binary()
            self.fixture.write_runtime_archives()

        launcher_build.ensure_launcher_build(self.fixture.build, runner)

        self.assertEqual(
            commands, [launcher_build.target_command(self.fixture.build, "zelda3d_app")]
        )
        self.assertNotIn("-S", commands[0])

    def test_supported_compiler_ids_are_accepted_without_product_policy(self) -> None:
        for c, cxx, c_id, cxx_id in (
            ("/usr/bin/gcc", "/usr/bin/g++", "GNU", "GNU"),
            ("/usr/bin/clang", "/usr/bin/clang++", "Clang", "Clang"),
            (
                "/usr/bin/clang",
                "/usr/bin/clang++",
                "AppleClang",
                "AppleClang",
            ),
        ):
            with self.subTest(compiler_id=cxx_id):
                self.fixture.write_toolchain(c, cxx, c_id, cxx_id)
                self.assertTrue(
                    launcher_build.has_required_configuration(self.fixture.build)
                )

    def test_gcc_cache_is_reused_without_forcing_clang(self) -> None:
        self.fixture.write_toolchain(
            c="/usr/bin/gcc", cxx="/usr/bin/g++", c_id="GNU", cxx_id="GNU"
        )
        commands: list[list[str]] = []

        def runner(command: Sequence[str]) -> None:
            commands.append(list(command))
            if self.fixture.build.target in command:
                self.fixture.write_binary()
                self.fixture.write_runtime_archives()

        launcher_build.ensure_launcher_build(self.fixture.build, runner)

        self.assertEqual(
            commands,
            [launcher_build.target_command(self.fixture.build, "zelda3d_app")],
        )

    def test_first_configure_does_not_request_fresh(self) -> None:
        command = launcher_build.configure_command(self.fixture.build)
        self.assertNotIn("--fresh", command)

    def test_incomplete_cache_requests_fresh(self) -> None:
        self.fixture.build.build_dir.mkdir(parents=True)
        (self.fixture.build.build_dir / "CMakeCache.txt").write_text(
            "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++\n"
        )
        self.assertIn("--fresh", launcher_build.configure_command(self.fixture.build))

    def test_mixed_compiler_cache_is_not_policed_by_product_build(self) -> None:
        self.fixture.write_toolchain(
            c="/usr/bin/gcc", cxx="/usr/bin/clang++", c_id="GNU", cxx_id="Clang"
        )
        self.assertTrue(launcher_build.has_required_configuration(self.fixture.build))

    def test_tests_disabled_cache_is_reconfigured(self) -> None:
        self.fixture.write_toolchain(tests="OFF")
        self.assertFalse(launcher_build.has_required_configuration(self.fixture.build))
        self.assertIn(
            "-DLUS_BUILD_TESTS=ON", launcher_build.configure_command(self.fixture.build)
        )

    def test_configure_must_produce_complete_build_metadata(self) -> None:
        def runner(command: Sequence[str]) -> None:
            if "-S" in command:
                self.fixture.write_toolchain(
                    c="/usr/bin/gcc", cxx="", c_id="GNU", cxx_id=""
                )

        with self.assertRaisesRegex(
            launcher_build.LauncherBuildError, "complete Ninja metadata"
        ):
            launcher_build.ensure_launcher_build(self.fixture.build, runner)

    def test_configure_forwards_locked_python_without_forcing_a_compiler(self) -> None:
        command = launcher_build.configure_command(self.fixture.build, "/locked/python")

        self.assertIn("-DPython3_EXECUTABLE=/locked/python", command)
        self.assertFalse(any("CMAKE_C_COMPILER" in item for item in command))
        self.assertFalse(any("CMAKE_CXX_COMPILER" in item for item in command))

    def test_mismatched_cached_python_forces_fresh_locked_reconfigure(self) -> None:
        locked_python = str(self.fixture.repo / ".venv" / "bin" / "python")
        self.fixture.write_toolchain(python_executable="/usr/bin/python3")
        commands: list[list[str]] = []

        def runner(command: Sequence[str]) -> None:
            command = list(command)
            commands.append(command)
            if "-S" in command:
                self.fixture.write_toolchain(python_executable=locked_python)
            else:
                self.fixture.write_binary()
                self.fixture.write_runtime_archives()

        launcher_build.ensure_launcher_build(
            self.fixture.build,
            runner,
            python_executable=locked_python,
        )

        self.assertIn("--fresh", commands[0])
        self.assertIn(f"-DPython3_EXECUTABLE={locked_python}", commands[0])
        self.assertEqual(
            commands[1],
            launcher_build.target_command(self.fixture.build, "zelda3d_app"),
        )

    def test_missing_executable_after_target_build_fails(self) -> None:
        self.fixture.write_toolchain()
        with self.assertRaisesRegex(
            launcher_build.LauncherBuildError, "missing or not executable"
        ):
            launcher_build.ensure_launcher_build(
                self.fixture.build, lambda _command: None
            )

    def test_existing_runtime_archives_do_not_skip_authoritative_target(self) -> None:
        self.fixture.write_toolchain()
        self.fixture.write_binary()
        self.fixture.write_runtime_archives("stale\n")
        commands: list[list[str]] = []

        def runner(command: Sequence[str]) -> None:
            commands.append(list(command))
            self.fixture.write_runtime_archives("fresh\n")

        launcher_build.ensure_launcher_build(self.fixture.build, runner)

        self.assertEqual(
            commands,
            [
                launcher_build.target_command(
                    self.fixture.build, self.fixture.build.target
                )
            ],
        )
        self.assertEqual(
            [archive.read_text() for archive in self.fixture.build.runtime_archives],
            ["fresh\n", "fresh\n", "fresh\n"],
        )

    def test_missing_mm_runtime_archive_after_target_build_fails(self) -> None:
        self.fixture.write_toolchain()

        def runner(_command: Sequence[str]) -> None:
            self.fixture.write_binary()
            soh_archive, _mm_archive, _mm_custom_archive = (
                self.fixture.build.runtime_archives
            )
            soh_archive.parent.mkdir(parents=True, exist_ok=True)
            soh_archive.write_text("fixture\n")

        with self.assertRaisesRegex(launcher_build.LauncherBuildError, "mm/mm.o2r"):
            launcher_build.ensure_launcher_build(self.fixture.build, runner)

    def test_missing_mm_custom_runtime_archive_after_target_build_fails(self) -> None:
        self.fixture.write_toolchain()

        def runner(_command: Sequence[str]) -> None:
            self.fixture.write_binary()
            soh_archive, mm_archive, _mm_custom_archive = (
                self.fixture.build.runtime_archives
            )
            for archive in (soh_archive, mm_archive):
                archive.parent.mkdir(parents=True, exist_ok=True)
                archive.write_text("fixture\n")

        with self.assertRaisesRegex(launcher_build.LauncherBuildError, "mm/2ship.o2r"):
            launcher_build.ensure_launcher_build(self.fixture.build, runner)

    def test_missing_soh_runtime_archive_after_target_build_fails(self) -> None:
        self.fixture.write_toolchain()

        def runner(_command: Sequence[str]) -> None:
            self.fixture.write_binary()
            _soh_archive, mm_archive, mm_custom_archive = (
                self.fixture.build.runtime_archives
            )
            for archive in (mm_archive, mm_custom_archive):
                archive.parent.mkdir(parents=True, exist_ok=True)
                archive.write_text("fixture\n")

        with self.assertRaisesRegex(launcher_build.LauncherBuildError, "soh/soh.o2r"):
            launcher_build.ensure_launcher_build(self.fixture.build, runner)

    def test_missing_checkout_source_is_named_before_any_command(self) -> None:
        (self.fixture.repo / "Shipwright" / "libultraship" / "CMakeLists.txt").unlink()
        commands: list[list[str]] = []
        with self.assertRaisesRegex(
            launcher_build.LauncherBuildError, "Shipwright/libultraship/CMakeLists.txt"
        ):
            launcher_build.ensure_launcher_build(
                self.fixture.build, lambda command: commands.append(list(command))
            )
        self.assertEqual(commands, [])

    def test_missing_cmake_is_reported_without_a_traceback(self) -> None:
        with (
            mock.patch.object(
                launcher_build.cmake_build_policy.subprocess,
                "run",
                side_effect=FileNotFoundError,
            ),
            self.assertRaisesRegex(
                launcher_build.LauncherBuildError,
                "required executable is missing: cmake",
            ),
        ):
            launcher_build.run_command(["cmake", "--version"])


if __name__ == "__main__":
    unittest.main()
