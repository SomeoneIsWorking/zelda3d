#!/usr/bin/env python3
"""Launcher-specific build inputs, targets, and artifact postconditions."""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

import cmake_build_policy


class LauncherBuildError(cmake_build_policy.CMakeBuildError):
    pass


@dataclass(frozen=True)
class BuildLayout:
    repo: Path
    build_dir: Path
    target: str
    binary: Path
    runtime_archives: tuple[Path, ...]
    jobs: int

    @classmethod
    def for_repo(cls, repo: Path, jobs: int | None = None) -> BuildLayout:
        repo = repo.resolve()
        build_dir = repo / "Shipwright" / "build-cmake"
        return cls(
            repo=repo,
            build_dir=build_dir,
            target="zelda3d_app",
            binary=build_dir / "zelda3d" / "zelda3d",
            runtime_archives=(
                build_dir / "soh" / "soh.o2r",
                build_dir / "mm" / "mm.o2r",
                build_dir / "mm" / "2ship.o2r",
            ),
            jobs=jobs if jobs is not None else (os.cpu_count() or 4),
        )


CommandRunner = Callable[[Sequence[str]], None]


def has_required_configuration(
    build: BuildLayout, python_executable: str | Path | None = None
) -> bool:
    required_cache = {"LUS_BUILD_TESTS": "ON"}
    if python_executable is not None:
        required_cache["Python3_EXECUTABLE"] = str(Path(python_executable).resolve())
    return cmake_build_policy.has_ninja_configuration(
        build.build_dir
    ) and cmake_build_policy.cache_matches(build.build_dir, required_cache)


def configure_command(
    build: BuildLayout, python_executable: str | Path | None = None
) -> list[str]:
    return cmake_build_policy.configure_command(
        build.repo,
        build.build_dir,
        options=(
            "-DCMAKE_BUILD_TYPE=Release",
            "-DLUS_BUILD_TESTS=ON",
        ),
        python_executable=python_executable,
    )


def target_command(build: BuildLayout, target: str) -> list[str]:
    return [
        "cmake",
        "--build",
        str(build.build_dir),
        "--target",
        target,
        f"-j{build.jobs}",
    ]


def run_command(command: Sequence[str]) -> None:
    cmake_build_policy.run_checked(command, error_type=LauncherBuildError)


def require_sources(build: BuildLayout) -> None:
    required = (
        build.repo / "CMakeLists.txt",
        build.repo / "Shipwright" / "libultraship" / "CMakeLists.txt",
    )
    missing = [path.relative_to(build.repo) for path in required if not path.is_file()]
    if missing:
        raise LauncherBuildError(
            "checkout is missing required source file(s): "
            + ", ".join(map(str, missing))
        )


def ensure_launcher_build(
    build: BuildLayout,
    runner: CommandRunner = run_command,
    *,
    python_executable: str | Path | None = None,
) -> None:
    require_sources(build)
    if not has_required_configuration(build, python_executable):
        print("configuring Zelda3D build metadata…", file=sys.stderr)
        runner(configure_command(build, python_executable))
        if not has_required_configuration(build, python_executable):
            raise LauncherBuildError(
                "CMake did not produce complete Ninja metadata with LUS_BUILD_TESTS=ON "
                "and the requested Python3_EXECUTABLE; inspect the configure output and "
                "build metadata"
            )

    print(
        f"building '{build.target}' (this can take a while the first time)…",
        file=sys.stderr,
    )
    runner(target_command(build, build.target))
    if not build.binary.is_file() or not os.access(build.binary, os.X_OK):
        raise LauncherBuildError(
            f"launcher binary is missing or not executable after build: {build.binary}"
        )

    # Bootstrap owns the independent MM extraction pipeline; zelda3d_app owns the SoH custom
    # archive. The unified product is launchable only when all three runtime inputs remain present.
    missing_archives = [
        archive for archive in build.runtime_archives if not archive.is_file()
    ]
    if missing_archives:
        raise LauncherBuildError(
            "runtime archive(s) missing after authoritative zelda3d_app build: "
            + ", ".join(map(str, missing_archives))
        )


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Zelda3D repository root",
    )
    parser.add_argument(
        "--jobs", type=int, help="parallel build jobs (default: detected CPU count)"
    )
    args = parser.parse_args(argv)
    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        ensure_launcher_build(
            BuildLayout.for_repo(args.repo, args.jobs),
            python_executable=sys.executable,
        )
        return 0
    except LauncherBuildError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
