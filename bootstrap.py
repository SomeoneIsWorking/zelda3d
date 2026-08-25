#!/usr/bin/env python3
"""Fresh-clone setup and shipping launcher composition for Zelda3D."""

from __future__ import annotations

import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent
TOOLS = REPO / "tools"
sys.path.insert(0, str(TOOLS))

import launcher_build
from rom_provision import (
    RomProvisionError,
    provision_n64_extraction_rom,
    require_oot3d_rom,
    resolve_rom_environment,
)

from launcher_bootstrap.arguments import parse_arguments
from launcher_bootstrap.layout import LauncherLayout
from launcher_bootstrap.mm_assets import (
    MM3D_ROM_NAME,
    MM_ROM_NAME,
    MmAssetError,
    MmAssetLayout,
    ensure_mm_runtime_archives,
    require_mm_roms,
    resolve_mm_rom_environment,
)
from launcher_bootstrap.native_dependencies import (
    MissingNativeDependencies,
    prepare_windows_toolchain_environment,
    propagate_cmake_environment,
    require_dependencies,
)
from launcher_bootstrap.runtime_environment import prepare_runtime_environment
from launcher_bootstrap.source_provision import (
    SourceProvisionError,
    ensure_build_sources,
)


def run(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    layout = LauncherLayout.for_repo(REPO)
    environment = resolve_mm_rom_environment(layout.repo, os.environ)
    environment = resolve_rom_environment(layout.repo, environment)
    environment = prepare_runtime_environment(environment)
    if sys.platform == "win32":
        environment = prepare_windows_toolchain_environment(environment)

    require_dependencies(environment)
    environment["ZELDA3D_OOT3D_ROM"] = str(require_oot3d_rom(layout.repo, environment))
    mm_rom, mm3d_rom = require_mm_roms(layout.repo, environment)
    environment[MM_ROM_NAME] = str(mm_rom)
    environment[MM3D_ROM_NAME] = str(mm3d_rom)
    if arguments.check_only:
        print(
            "Zelda3D bootstrap check passed: native dependencies and all game inputs found."
        )
        return 0

    propagate_cmake_environment(environment, os.environ)
    ensure_build_sources(layout.repo)
    build = launcher_build.BuildLayout.for_repo(layout.repo, arguments.jobs)
    ensure_mm_runtime_archives(
        MmAssetLayout.for_repo(layout.repo, build.build_dir),
        environment,
        python_executable=sys.executable,
        jobs=arguments.jobs,
    )
    provision_n64_extraction_rom(layout.oot_app_dir, environment)
    launcher_build.ensure_launcher_build(
        build,
        python_executable=sys.executable,
    )
    if arguments.prepare_only:
        print(f"Zelda3D is prepared at {layout.executable}")
        return 0

    os.chdir(layout.oot_app_dir)
    os.execvpe(
        str(layout.executable),
        [str(layout.executable), *arguments.product_arguments],
        environment,
    )
    raise AssertionError("os.execvpe returned unexpectedly")


def main(argv: list[str] | None = None) -> int:
    try:
        return run(argv)
    except (
        launcher_build.LauncherBuildError,
        MissingNativeDependencies,
        MmAssetError,
        RomProvisionError,
        SourceProvisionError,
    ) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
