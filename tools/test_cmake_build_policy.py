#!/usr/bin/env python3
"""Focused tests for shared CMake build policy."""

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

import cmake_build_policy


class PolicyFixture:
    def __init__(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.root = Path(self.temporary.name)
        self.build_dir = self.root / "build"

    def close(self) -> None:
        self.temporary.cleanup()

    def write_toolchain(
        self,
        *,
        c: str = "/usr/bin/clang",
        cxx: str = "/usr/bin/clang++",
        c_id: str = "Clang",
        cxx_id: str = "Clang",
        generator: str = "Ninja",
    ) -> None:
        self.build_dir.mkdir(parents=True, exist_ok=True)
        (self.build_dir / "build.ninja").write_text("# fixture\n")
        (self.build_dir / "CMakeCache.txt").write_text(
            "// fixture cache\n"
            f"CMAKE_GENERATOR:INTERNAL={generator}\n"
            f"CMAKE_C_COMPILER:FILEPATH={c}\n"
            f"CMAKE_CXX_COMPILER:FILEPATH={cxx}\n"
            "FEATURE:BOOL=ON\n"
        )
        metadata = self.build_dir / "CMakeFiles/fixture"
        metadata.mkdir(parents=True, exist_ok=True)
        (metadata / "CMakeCCompiler.cmake").write_text(
            f'set(CMAKE_C_COMPILER_ID "{c_id}")\n'
        )
        (metadata / "CMakeCXXCompiler.cmake").write_text(
            f'set(CMAKE_CXX_COMPILER_ID "{cxx_id}")\n'
        )


class CMakeBuildPolicyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = PolicyFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_root_configuration_does_not_reject_non_clang_compilers(self) -> None:
        root_cmake = (REPO / "CMakeLists.txt").read_text()

        self.assertNotIn("Zelda3D requires Clang", root_cmake)

    def test_cache_parser_and_required_values_share_one_authority(self) -> None:
        self.fixture.write_toolchain()

        cache = cmake_build_policy.read_cmake_cache(
            self.fixture.build_dir / "CMakeCache.txt"
        )

        self.assertEqual(cache["CMAKE_GENERATOR"], "Ninja")
        self.assertTrue(
            cmake_build_policy.cache_matches(self.fixture.build_dir, {"FEATURE": "ON"})
        )

    def test_ninja_configuration_accepts_gcc(self) -> None:
        self.fixture.write_toolchain(c="/usr/bin/gcc", c_id="GNU")

        self.assertTrue(
            cmake_build_policy.has_ninja_configuration(self.fixture.build_dir)
        )

    def test_ninja_configuration_requires_each_language_compiler(self) -> None:
        self.fixture.write_toolchain(cxx="")

        self.assertFalse(
            cmake_build_policy.has_ninja_configuration(
                self.fixture.build_dir, languages=("CXX",)
            )
        )

    def test_configure_adds_fresh_only_for_existing_cache(self) -> None:
        first = cmake_build_policy.configure_command(
            self.fixture.root, self.fixture.build_dir, options=("-DFEATURE=ON",)
        )
        self.fixture.build_dir.mkdir()
        (self.fixture.build_dir / "CMakeCache.txt").write_text("partial\n")
        refresh = cmake_build_policy.configure_command(
            self.fixture.root,
            self.fixture.build_dir,
            options=("-DFEATURE=ON",),
            python_executable="/locked/python",
        )

        self.assertNotIn("--fresh", first)
        self.assertIn("--fresh", refresh)
        self.assertFalse(any("CMAKE_C_COMPILER" in item for item in refresh))
        self.assertFalse(any("CMAKE_CXX_COMPILER" in item for item in refresh))
        self.assertIn("-DFEATURE=ON", refresh)
        self.assertIn("-DPython3_EXECUTABLE=/locked/python", refresh)

    def test_missing_executable_is_named(self) -> None:
        with (
            mock.patch.object(
                cmake_build_policy.subprocess, "run", side_effect=FileNotFoundError
            ),
            self.assertRaisesRegex(
                cmake_build_policy.CMakeBuildError,
                "required executable is missing: cmake",
            ),
        ):
            cmake_build_policy.run_checked(["cmake", "--version"])

    def test_nonzero_command_is_rejected(self) -> None:
        result = subprocess.CompletedProcess(["cmake"], 7)
        with (
            mock.patch.object(
                cmake_build_policy.subprocess, "run", return_value=result
            ),
            self.assertRaisesRegex(cmake_build_policy.CMakeBuildError, "exit 7"),
        ):
            cmake_build_policy.run_checked(["cmake", "--version"])


if __name__ == "__main__":
    unittest.main()
