"""Locate and invoke required Clang command-line tools."""

from __future__ import annotations

import shutil
import subprocess
import sys
from collections.abc import Iterable, Sequence
from pathlib import Path
from typing import TypeVar

from . import VerificationError

Item = TypeVar("Item")


def require_tool(explicit: str | None, default: str) -> str:
    tool = explicit or shutil.which(default)
    if not tool:
        raise VerificationError(
            f"required tool '{default}' is missing; on Fedora install it with: "
            "sudo dnf install clang clang-tools-extra"
        )
    return tool


def run_checked(command: Sequence[str], label: str, *, cwd: Path) -> None:
    result = subprocess.run(
        command, cwd=cwd, check=False, text=True, capture_output=True
    )
    if result.returncode != 0:
        if result.stdout:
            print(result.stdout, file=sys.stderr, end="")
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        raise VerificationError(f"{label} failed with exit {result.returncode}")


def batches(items: Sequence[Item], size: int) -> Iterable[Sequence[Item]]:
    for index in range(0, len(items), size):
        yield items[index : index + size]
