#!/usr/bin/env python3
"""Build selected pinned native dependencies into an explicit prefix."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from launcher_bootstrap.native_sources import SOURCES, build_source


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("sources", nargs="+", choices=tuple(SOURCES))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    for name in args.sources:
        build_source(
            SOURCES[name], args.root.resolve(), args.prefix.resolve(), args.jobs
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
