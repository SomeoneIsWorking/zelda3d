"""clang-tidy execution against the authoritative compilation database."""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path

from .clang_tool_runner import batches, run_checked


def verify_tidy(
    files: Sequence[Path], executable: str, compile_commands: Path, *, repo: Path
) -> None:
    for batch in batches(files, 64):
        run_checked(
            [
                executable,
                f"-p={compile_commands.parent}",
                f"--config-file={repo / '.clang-tidy'}",
                "--warnings-as-errors=*",
                *map(str, batch),
            ],
            "clang-tidy",
            cwd=repo,
        )
