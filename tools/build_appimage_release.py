#!/usr/bin/env python3
"""Build and verify Zelda3D's Linux release on the Ubuntu 22.04 ABI baseline."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from launcher_bootstrap.source_provision import ensure_build_sources

CONTAINERFILE = ROOT / "packaging/AppImage.Containerfile"
OUTPUT = ROOT / "scratch/release/Zelda3D-x86_64.AppImage"
MM_CUSTOM_ARCHIVE = ROOT / "scratch/release-inputs/2ship.o2r"
IMAGE = "localhost/zelda3d-appimage-builder:ubuntu22"
CONTAINER_BUILD = "/src/build/appimage-ubuntu22"
CONTAINER_MM_BUILD = "/src/build/appimage-mm-ubuntu22"
CONTAINER_MM_ARCHIVE = "/src/scratch/release-inputs/2ship.o2r"
CONTAINER_OUTPUT = "/src/scratch/release/Zelda3D-x86_64.AppImage"
CONTAINER_PYTHON = "/src/build/appimage-container-venv/bin/python"
CONTEXT_INPUTS = (
    Path("packaging/AppImage.Containerfile"),
    Path("launcher_bootstrap/__init__.py"),
    Path("launcher_bootstrap/native_sources.py"),
    Path("tools/prepare_native_sources.py"),
)


def refuse(message: str) -> None:
    raise SystemExit(f"appimage-release: {message}")


def run(command: Sequence[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    try:
        subprocess.run(list(command), cwd=ROOT, check=True)
    except OSError as exc:
        refuse(f"cannot run {command[0]}: {exc}")
    except subprocess.CalledProcessError as exc:
        refuse(f"command failed with exit code {exc.returncode}: {' '.join(command)}")


def require_file(path: Path, label: str) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        refuse(f"{label} is missing: {path}")
    return path


def prepare_build_context(repo: Path) -> Path:
    """Stage only authored builder inputs; never expose the checkout's game assets."""
    context = repo / "build" / "appimage-container-context"
    if context.is_symlink():
        refuse(f"container context cannot be a symlink: {context}")
    context.mkdir(parents=True, exist_ok=True)
    for existing in context.rglob("*"):
        if existing.is_symlink() or (
            existing.is_file() and existing.relative_to(context) not in CONTEXT_INPUTS
        ):
            refuse(f"unexpected file in bounded container context: {existing}")
    for relative in CONTEXT_INPUTS:
        source = require_file(repo / relative, "container build input")
        destination = context / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    return context


def container_command(podman: str, command: Sequence[str]) -> list[str]:
    return [
        podman,
        "run",
        "--rm",
        "--userns=keep-id",
        "--security-opt",
        "label=disable",
        "--env",
        "HOME=/src/build/appimage-container-home",
        "--env",
        "CCACHE_DIR=/src/build/appimage-ccache",
        "--env",
        "UV_CACHE_DIR=/src/build/appimage-uv-cache",
        "--env",
        "UV_PROJECT_ENVIRONMENT=/src/build/appimage-container-venv",
        "--env",
        "APPIMAGE_EXTRACT_AND_RUN=1",
        "--env",
        "CC=clang",
        "--env",
        "CXX=clang++",
        "--volume",
        f"{ROOT}:/src:rw",
        "--workdir",
        "/src",
        IMAGE,
        *command,
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 12))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    podman = shutil.which("podman")
    if podman is None:
        refuse("podman is required to build the old-glibc release container")
    require_file(CONTAINERFILE, "AppImage container recipe")
    ensure_build_sources(ROOT)
    context = prepare_build_context(ROOT)

    run(
        [
            podman,
            "build",
            "--tag",
            IMAGE,
            "--file",
            str(context / "packaging" / "AppImage.Containerfile"),
            str(context),
        ]
    )
    run(container_command(podman, ["uv", "sync", "--frozen"]))
    configure = [
        "cmake",
        "-S",
        "/src",
        "-B",
        CONTAINER_BUILD,
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_CXX_COMPILER=clang++",
        "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
        "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
        "-DLUS_BUILD_TESTS=ON",
        f"-DPython3_EXECUTABLE={CONTAINER_PYTHON}",
    ]
    run(container_command(podman, configure))
    run(
        container_command(
            podman,
            [
                "cmake",
                "--build",
                CONTAINER_BUILD,
                "--target",
                "zelda3d_app",
                "lus_tests",
                "--parallel",
                str(args.jobs),
            ],
        )
    )
    run(
        container_command(
            podman,
            [
                "uv",
                "run",
                "--frozen",
                "python",
                "/src/tools/build_mm_custom_archive.py",
                "--build-dir",
                CONTAINER_MM_BUILD,
                "--output",
                CONTAINER_MM_ARCHIVE,
                "--fetchcontent-source-cache",
                f"{CONTAINER_BUILD}/_deps",
                "--jobs",
                str(args.jobs),
            ],
        )
    )
    require_file(MM_CUSTOM_ARCHIVE, "fresh Majora custom archive")
    run(
        container_command(
            podman,
            [
                f"{CONTAINER_BUILD}/libultraship/tests/lus_tests",
                "--gtest_brief=1",
            ],
        )
    )
    package = [
        "uv",
        "run",
        "--frozen",
        "python",
        "/src/tools/package_appimage.py",
        "--build-dir",
        CONTAINER_BUILD,
        "--mm-port-archive",
        CONTAINER_MM_ARCHIVE,
        "--output",
        CONTAINER_OUTPUT,
        "--linuxdeploy",
        "/usr/local/bin/linuxdeploy",
        "--appimagetool",
        "/usr/local/bin/appimagetool",
    ]
    run(container_command(podman, package))
    run(
        container_command(
            podman,
            [
                "uv",
                "run",
                "--frozen",
                "python",
                "/src/tools/package_appimage.py",
                "--verify",
                "--output",
                CONTAINER_OUTPUT,
            ],
        )
    )
    require_file(OUTPUT, "verified AppImage")
    print(f"appimage-release: verified artifact at {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
