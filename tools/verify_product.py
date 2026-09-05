#!/usr/bin/env python3
"""Build and execute Zelda3D's complete Linux product boundary without game files."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(TOOLS))

import launcher_build
from launcher_bootstrap.source_provision import ensure_build_sources


def verification_commands(
    build: launcher_build.BuildLayout, lint_base: str | None = None
) -> tuple[list[str], ...]:
    """Use the same targets as shipping, then exercise their asset-free boundaries."""
    # GitHub's new-branch push event has no before commit. It must inspect the
    # complete source instead of treating an empty working tree as no changed code.
    lint = [
        sys.executable,
        str(build.repo / "tools" / "verify_clang.py"),
        "--compile-commands",
        str(build.build_dir / "compile_commands.json"),
    ]
    lint_commands = (
        ([*lint, "--all", "--format-only"], [*lint, "--compiled", "--tidy-only"])
        if lint_base == "0" * 40
        else ([*lint, *([] if lint_base is None else ["--base", lint_base])],)
    )
    return (
        ["cmake", "--build", str(build.build_dir), f"-j{build.jobs}"],
        [
            "ctest",
            "--test-dir",
            str(build.build_dir),
            "--output-on-failure",
            "--no-tests=error",
        ],
        [str(build.binary), "--probe-cores"],
        *lint_commands,
    )


def verify(repo: Path, jobs: int, lint_base: str | None = None) -> None:
    if sys.platform != "linux":
        raise RuntimeError(
            "complete product verification currently requires Linux; see S009/S010"
        )
    ensure_build_sources(repo)
    build = launcher_build.BuildLayout.for_repo(
        repo, jobs, build_dir=repo / "build" / "ci-product"
    )
    launcher_build.ensure_product_build(build, python_executable=sys.executable)
    for relative in ("soh/libsoh_core.so", "mm/libmm_core.so", "soh/soh.o2r"):
        if not (build.build_dir / relative).is_file():
            raise RuntimeError(f"product artifact missing after build: {relative}")
    for command in verification_commands(build, lint_base):
        subprocess.run(command, cwd=repo, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--lint-base", help="committed change base for clean hosted checkouts")
    arguments = parser.parse_args()
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    verify(REPO, arguments.jobs, arguments.lint_base)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
