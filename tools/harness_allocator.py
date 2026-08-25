"""Allocator selection policy for the Azahar software rasterizer."""

from __future__ import annotations

import glob
from collections.abc import MutableMapping
from pathlib import Path

ALLOCATOR_PATTERNS = (
    "/usr/lib64/libjemalloc.so.2",
    "/usr/lib/x86_64-linux-gnu/libjemalloc.so.2",
    "/usr/lib*/libjemalloc.so.2",
    "/lib*/libjemalloc.so.2",
    "/usr/lib*/libtcmalloc_minimal.so.4",
)


def select_scalable_allocator(
    environment: MutableMapping[str, str],
    patterns: tuple[str, ...] = ALLOCATOR_PATTERNS,
) -> None:
    """Preserve an explicit preload or select the first installed allocator."""
    if environment.get("LD_PRELOAD"):
        return
    for pattern in patterns:
        for candidate in sorted(glob.glob(pattern)):
            if Path(candidate).is_file():
                environment["LD_PRELOAD"] = candidate
                return
