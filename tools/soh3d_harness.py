#!/usr/bin/env python3
"""Build, prepare, and run the embedded OoT3D harness."""

from __future__ import annotations

import os
import sys
from pathlib import Path

from harness_allocator import select_scalable_allocator
from harness_build import HarnessBuild, HarnessBuildError, ensure_harness_build
from harness_headless_display import prepare_headless_display
from harness_paths import REPO_ROOT
from harness_rom_environment import provision_rom_environment
from harness_runtime_inputs import link_runtime_inputs
from repo_environment import apply_repo_environment

USAGE = """\
usage: soh3d_harness.py [-h | --help] [rom]

Build the embedded OoT3D harness (Azahar core linked as a library) and exec it.
Bare invocation provisions the ROM from the repo .env, builds if stale, and
boots the harness with its wire REPL on stdout.

options:
  -h, --help  show this message and exit (nothing is built or booted)
  rom         path to the OoT3D ROM (default: $ZELDA3D_OOT3D_ROM from .env)

The harness is driven over its wire REPL: tools/harness_cli.py
(repl|send|warp|boot-to-play|peek; run with --help) or
harness_gameplay.boot_to_gameplay() from Python. REPL command surface:
oot3d-decomp/docs/oracle.md.
"""


def main(arguments: list[str]) -> int:
    if "-h" in arguments or "--help" in arguments:
        print(USAGE, end="")
        return 0
    try:
        apply_repo_environment(REPO_ROOT, os.environ)
        if not arguments:
            provision_rom_environment(REPO_ROOT, os.environ)
        jobs = int(os.environ.get("ZELDA3D_BUILD_JOBS", "4"))
        if jobs < 1:
            raise HarnessBuildError("ZELDA3D_BUILD_JOBS must be a positive integer")
        shipping_build_dir = Path(
            os.environ.get(
                "ZELDA3D_SHIPPING_BUILD_DIR",
                REPO_ROOT / "Shipwright" / "build-cmake",
            )
        )
        os.environ["ZELDA3D_SHIPPING_BUILD_DIR"] = str(shipping_build_dir)
        build = HarnessBuild.for_repo(REPO_ROOT, jobs, shipping_build_dir)
        ensure_harness_build(build)
        link_runtime_inputs(build)
        prepare_headless_display(REPO_ROOT, os.environ)
        select_scalable_allocator(os.environ)
        os.chdir(REPO_ROOT)
        os.execvpe(str(build.binary), [str(build.binary), *arguments], os.environ)
    except (HarnessBuildError, RuntimeError, ValueError) as error:
        print(f"harness: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
