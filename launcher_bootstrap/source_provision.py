"""Provision only public, pinned sources required by the shipping target."""

from __future__ import annotations

import subprocess
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from .native_sources import LUCENT, require_pinned_checkout


class SourceProvisionError(RuntimeError):
    """Raised when required public build sources cannot be initialized safely."""


@dataclass(frozen=True)
class BuildSubmodule:
    path: Path
    postcondition: Path


BUILD_SUBMODULES = (
    BuildSubmodule(Path("Shipwright/ZAPDTR"), Path("ZAPD/CMakeLists.txt")),
    BuildSubmodule(
        Path("Shipwright/libultraship/extern/StormLib"), Path("CMakeLists.txt")
    ),
)

CommandRunner = Callable[[Sequence[str]], None]


def _run_checked(command: Sequence[str]) -> None:
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError as exc:
        raise SourceProvisionError("required executable is missing: git") from exc
    except subprocess.CalledProcessError as exc:
        raise SourceProvisionError(
            f"git could not initialize required build sources (exit {exc.returncode})"
        ) from exc


def _missing_submodules(repo: Path) -> list[BuildSubmodule]:
    return [
        submodule
        for submodule in BUILD_SUBMODULES
        if not (repo / submodule.path / submodule.postcondition).is_file()
    ]


def _ensure_build_submodules(repo: Path, runner: CommandRunner) -> None:
    """Initialize missing pinned build submodules without touching initialized trees."""
    missing = _missing_submodules(repo)
    if not missing:
        return
    for submodule in missing:
        directory = repo / submodule.path
        if directory.is_dir() and any(directory.iterdir()):
            raise SourceProvisionError(
                f"required build source is incomplete but non-empty; refusing to overwrite it: "
                f"{submodule.path}"
            )

    paths = [str(submodule.path) for submodule in missing]
    runner(
        [
            "git",
            "-C",
            str(repo),
            "submodule",
            "update",
            "--init",
            "--depth",
            "1",
            "--",
            *paths,
        ]
    )
    still_missing = _missing_submodules(repo)
    if still_missing:
        raise SourceProvisionError(
            "required build source(s) still missing after submodule initialization: "
            + ", ".join(str(submodule.path) for submodule in still_missing)
        )


def ensure_build_sources(repo: Path, runner: CommandRunner = _run_checked) -> None:
    """Restore public submodules and validate the exact shared runtime checkout."""
    repo = repo.resolve()
    _ensure_build_submodules(repo, runner)
    try:
        require_pinned_checkout(LUCENT, repo / "build" / "deps" / "lucent-source")
    except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        raise SourceProvisionError(
            f"cannot provision pinned Lucent source: {exc}"
        ) from exc
