#!/usr/bin/env python3
"""Build MM's redistributable custom O2R with the authoritative GAME_MM exporter."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from launcher_bootstrap.mm_assets import (
    MmAssetError,
    MmAssetLayout,
    build_mm_custom_archive,
)


def source_cache_options(root: Path) -> tuple[str, ...]:
    resolved_root = root.resolve()
    if not resolved_root.is_dir():
        raise MmAssetError(f"FetchContent source cache is missing: {resolved_root}")
    sources = sorted(
        path
        for path in resolved_root.iterdir()
        if path.is_dir() and path.name.endswith("-src")
    )
    if not sources:
        raise MmAssetError(
            f"FetchContent source cache contains no dependency sources: {resolved_root}"
        )
    return tuple(
        f"-DFETCHCONTENT_SOURCE_DIR_{source.name[:-4].replace('-', '_').upper()}={source}"
        for source in sources
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--fetchcontent-source-cache",
        type=Path,
        help="reuse pinned dependency sources while keeping this build tree isolated",
    )
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 12))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")

    layout = MmAssetLayout.for_repo(ROOT, extraction_build_dir=args.build_dir)
    try:
        output = build_mm_custom_archive(
            layout,
            args.output,
            python_executable=sys.executable,
            jobs=args.jobs,
            configure_options=(
                source_cache_options(args.fetchcontent_source_cache)
                if args.fetchcontent_source_cache is not None
                else ()
            ),
        )
    except MmAssetError as exc:
        raise SystemExit(f"mm-custom-archive: {exc}") from exc
    print(f"mm-custom-archive: verified {output} ({output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
