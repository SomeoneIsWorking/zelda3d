"""Authoritative parser and caller-precedence policy for the repo ``.env`` file."""

from __future__ import annotations

import re
from collections.abc import MutableMapping
from pathlib import Path

ASSIGNMENT_RE = re.compile(r"^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$")


class RepoEnvironmentError(ValueError):
    """Raised when a repo environment file uses unsupported syntax."""


def _parse_value(raw_value: str, line_number: int) -> str:
    value = raw_value.strip()
    if not value or value[0] not in "\"'":
        return value
    quote = value[0]
    if len(value) < 2 or value[-1] != quote:
        raise RepoEnvironmentError(
            f".env line {line_number} has an unterminated quoted value"
        )
    return value[1:-1]


def read_repo_environment(path: Path) -> dict[str, str]:
    """Parse simple shell-style assignments without executing the file."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        return {}

    values: dict[str, str] = {}
    for line_number, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = ASSIGNMENT_RE.fullmatch(line)
        if match is None:
            raise RepoEnvironmentError(
                f".env line {line_number} is not a supported NAME=value assignment"
            )
        name, raw_value = match.groups()
        values[name] = _parse_value(raw_value, line_number)
    return values


def apply_repo_environment(
    repo: Path, environment: MutableMapping[str, str]
) -> MutableMapping[str, str]:
    """Add repo defaults while preserving every caller-provided value."""
    for name, value in read_repo_environment(repo / ".env").items():
        environment.setdefault(name, value)
    return environment
