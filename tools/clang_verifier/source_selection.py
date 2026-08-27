"""First-party source classification and repository file selection."""

from __future__ import annotations

import subprocess
from collections.abc import Iterable, Sequence
from pathlib import Path

from . import VerificationError

SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx"})
FORMAT_SUFFIXES = SOURCE_SUFFIXES | frozenset({".h", ".hh", ".hpp", ".hxx"})
PYTHON_SUFFIXES = frozenset({".py"})
STRUCTURE_SUFFIXES = FORMAT_SUFFIXES | PYTHON_SUFFIXES

EXCLUDED_PREFIXES = (
    "2ship/assets/",
    "2ship/src/",  # generated MM decompilation
    "Azahar/",
    "Shipwright/ZAPDTR/",
    "Shipwright/build",
    "Shipwright/libultraship/extern/",
    "Shipwright/soh/assets/",
    "mm3d-decomp/",
    "oot3d-decomp/",
    "scratch/",
)
EXCLUDED_PARTS = frozenset({"_deps", "assets", "extern", "generated", "thirdparty"})
LEGACY_DECOMP_PREFIXES = ("2ship/src/", "Shipwright/soh/src/")


def run_git(
    repo: Path, args: Sequence[str], *, check: bool = True
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args], cwd=repo, check=check, text=True, capture_output=True
    )


def repo_relative(path: Path, repo: Path) -> str:
    try:
        return path.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError as exc:
        raise VerificationError(f"path is outside the repository: {path}") from exc


def is_first_party(relative: str) -> bool:
    if relative.startswith(EXCLUDED_PREFIXES):
        return False
    if relative.startswith("Shipwright/soh/src/") and not relative.startswith(
        "Shipwright/soh/src/zelda3d/"
    ):
        return False
    return not any(part in EXCLUDED_PARTS for part in Path(relative).parts)


def is_legacy_decomp_seam(relative: str) -> bool:
    if relative.startswith("Shipwright/soh/src/zelda3d/"):
        return False
    return relative.startswith(LEGACY_DECOMP_PREFIXES)


def repository_files(repo: Path, suffixes: frozenset[str]) -> list[Path]:
    tracked = run_git(repo, ["ls-files", "-z"]).stdout
    untracked = run_git(
        repo, ["ls-files", "--others", "--exclude-standard", "-z"]
    ).stdout
    paths = []
    for raw in (tracked + untracked).split("\0"):
        candidate = repo / raw
        if (
            raw
            and Path(raw).suffix.lower() in suffixes
            and is_first_party(raw)
            and candidate.is_file()
            and not candidate.is_symlink()
        ):
            paths.append(candidate)
    return sorted(set(paths))


def changed_files(repo: Path) -> list[Path]:
    tracked = run_git(
        repo, ["diff", "--name-only", "--diff-filter=ACMR", "-z", "HEAD", "--"]
    ).stdout
    untracked = run_git(
        repo, ["ls-files", "--others", "--exclude-standard", "-z"]
    ).stdout
    paths = set()
    for raw in (tracked + untracked).split("\0"):
        if raw and Path(raw).suffix.lower() in FORMAT_SUFFIXES and is_first_party(raw):
            paths.add(repo / raw)
    return sorted(paths)


def modified_legacy_decomp_files(repo: Path) -> list[Path]:
    modified = run_git(
        repo, ["diff", "--name-only", "--diff-filter=M", "-z", "HEAD", "--"]
    ).stdout
    return sorted(
        repo / raw
        for raw in modified.split("\0")
        if raw
        and Path(raw).suffix.lower() in FORMAT_SUFFIXES
        and is_legacy_decomp_seam(raw)
    )


def explicit_files(repo: Path, raw_paths: Iterable[str]) -> list[Path]:
    paths = []
    for raw in raw_paths:
        path = Path(raw)
        if not path.is_absolute():
            path = repo / path
        relative = repo_relative(path, repo)
        if not path.is_file():
            raise VerificationError(f"file does not exist: {relative}")
        if path.suffix.lower() not in FORMAT_SUFFIXES:
            raise VerificationError(f"not a C/C++ source or header: {relative}")
        if not is_first_party(relative):
            raise VerificationError(
                f"excluded vendor/generated file is not first-party: {relative}"
            )
        paths.append(path.resolve())
    return sorted(set(paths))
