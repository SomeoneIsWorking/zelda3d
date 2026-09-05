#!/usr/bin/env python3
"""Provide pinned SDL3 for Linux CI hosts whose package repository predates SDL3."""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

from launcher_bootstrap.native_sources import SDL, build_source


def prepare(repo: Path) -> Path:
    return build_source(SDL, repo / "build" / "deps", repo / "build" / "deps" / "sdl")


if __name__ == "__main__":
    print(prepare(REPO))
