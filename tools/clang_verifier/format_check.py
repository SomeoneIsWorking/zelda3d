"""Non-mutating format checks for tracked edits and new files."""

from __future__ import annotations

import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path

from . import VerificationError
from .clang_tool_runner import batches, run_checked


def split_tracked_files(
    files: Sequence[Path], repo: Path
) -> tuple[list[Path], list[Path]]:
    relative_paths = []
    by_relative = {}
    for path in files:
        try:
            relative = path.resolve().relative_to(repo.resolve()).as_posix()
        except ValueError as exc:
            raise VerificationError(
                f"format path is outside the repository: {path}"
            ) from exc
        relative_paths.append(relative)
        by_relative[relative] = path
    if not relative_paths:
        return [], []
    result = subprocess.run(
        ["git", "-C", str(repo), "ls-files", "-z", "--", *relative_paths],
        check=True,
        text=True,
        capture_output=True,
    )
    tracked_names = set(result.stdout.split("\0"))
    tracked = [by_relative[name] for name in relative_paths if name in tracked_names]
    untracked = [
        by_relative[name] for name in relative_paths if name not in tracked_names
    ]
    return tracked, untracked


def verify_format(files: Sequence[Path], executable: str, *, repo: Path) -> None:
    tracked, untracked = split_tracked_files(files, repo)
    for batch in batches(tracked, 128):
        relative_batch = [
            path.resolve().relative_to(repo.resolve()).as_posix() for path in batch
        ]
        result = subprocess.run(
            [
                "git",
                "-C",
                str(repo),
                "clang-format",
                "--diff",
                "--quiet",
                "--binary",
                executable,
                "HEAD",
                "--",
                *relative_batch,
            ],
            check=False,
            text=True,
            capture_output=True,
        )
        # git-clang-format deliberately exits 1 when --diff emits a patch.
        # Inspect that patch before treating the status as a tool failure.
        if result.stdout.strip():
            print(result.stdout, file=sys.stderr, end="")
            raise VerificationError(
                "git clang-format found formatting changes in edited lines"
            )
        if result.returncode != 0:
            if result.stderr:
                print(result.stderr, file=sys.stderr, end="")
            raise VerificationError(
                f"git clang-format failed with exit {result.returncode}"
            )

    for batch in batches(untracked, 128):
        run_checked(
            [executable, "--dry-run", "--Werror", "--style=file", *map(str, batch)],
            "clang-format",
            cwd=repo,
        )
