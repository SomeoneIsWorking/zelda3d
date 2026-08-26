"""Azahar-specific configure options and harness artifact postconditions."""

from __future__ import annotations

import os
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

import cmake_build_policy


class HarnessBuildError(cmake_build_policy.CMakeBuildError):
    """Raised when the harness cannot be configured or built correctly."""


CommandRunner = Callable[[Sequence[str]], None]


@dataclass(frozen=True)
class HarnessBuild:
    repo: Path
    source: Path
    build_dir: Path
    binary: Path
    wire_in: Path
    shipping_build_dir: Path
    jobs: int

    @classmethod
    def for_repo(
        cls, repo: Path, jobs: int, shipping_build_dir: Path | None = None
    ) -> HarnessBuild:
        source = repo / "Azahar"
        build_dir = source / "build-harness"
        return cls(
            repo=repo,
            source=source,
            build_dir=build_dir,
            binary=build_dir / "bin" / "Release" / "soh3d_harness",
            wire_in=repo / "tools" / "soh3d_harness" / "wire_in.cmake",
            shipping_build_dir=shipping_build_dir
            or repo / "Shipwright" / "build-cmake",
            jobs=jobs,
        )


def run_command(command: Sequence[str]) -> None:
    cmake_build_policy.run_checked(command, error_type=HarnessBuildError)


def has_required_configuration(build: HarnessBuild) -> bool:
    return cmake_build_policy.has_ninja_configuration(
        build.build_dir, languages=("CXX",)
    ) and (build.build_dir / "compile_commands.json").is_file()


def configure_command(
    build: HarnessBuild, python_executable: str | Path | None = None
) -> list[str]:
    return cmake_build_policy.configure_command(
        build.source,
        build.build_dir,
        options=(
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DENABLE_LIBRETRO=ON",
            f"-DCMAKE_PROJECT_citra_INCLUDE={build.wire_in}",
            f"-DZELDA3D_SHIPPING_BUILD_DIR={build.shipping_build_dir}",
            "-DENABLE_QT=OFF",
            "-DENABLE_SDL2=OFF",
            "-DENABLE_CUBEB=OFF",
            "-DENABLE_OPENAL=OFF",
            "-DENABLE_TESTS=OFF",
            "-DENABLE_VULKAN=ON",
            "-DENABLE_OPENGL=ON",
            "-DENABLE_SOFTWARE_RENDERER=ON",
            "-DENABLE_LTO=OFF",
            "-DUSE_SYSTEM_GLSLANG=ON",
            "-DENABLE_BUILTIN_KEYBLOB=ON",
        ),
        python_executable=python_executable,
    )


def ensure_harness_build(
    build: HarnessBuild,
    runner: CommandRunner = run_command,
    *,
    python_executable: str | Path | None = None,
) -> None:
    required = (build.source / "CMakeLists.txt", build.wire_in)
    missing = [path.relative_to(build.repo) for path in required if not path.is_file()]
    if missing:
        raise HarnessBuildError(f"missing harness build input: {missing[0]}")

    if not has_required_configuration(build):
        runner(configure_command(build, python_executable or sys.executable))
    if not has_required_configuration(build):
        raise HarnessBuildError(
            "harness configure did not produce complete Ninja C++ build metadata and compile commands"
        )
    runner(["ninja", "-C", str(build.build_dir), f"-j{build.jobs}", "soh3d_harness"])
    if not build.binary.is_file() or not os.access(build.binary, os.X_OK):
        raise HarnessBuildError(
            f"harness target completed but executable is missing or not executable: {build.binary}"
        )
