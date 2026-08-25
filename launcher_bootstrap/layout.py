"""Filesystem contract for the unified Zelda3D launcher."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class LauncherLayout:
    repo: Path
    build_dir: Path
    oot_app_dir: Path
    executable: Path

    @classmethod
    def for_repo(cls, repo: Path) -> LauncherLayout:
        resolved_repo = repo.resolve()
        build_dir = resolved_repo / "Shipwright" / "build-cmake"
        return cls(
            repo=resolved_repo,
            build_dir=build_dir,
            oot_app_dir=build_dir / "soh",
            executable=build_dir / "zelda3d" / "zelda3d",
        )
