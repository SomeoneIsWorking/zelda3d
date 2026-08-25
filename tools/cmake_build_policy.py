"""Shared CMake cache, configure, and subprocess policy."""

from __future__ import annotations

import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path


class CMakeBuildError(RuntimeError):
    """Raised when a required CMake operation cannot complete correctly."""


def read_cmake_cache(cache_path: Path) -> dict[str, str]:
    if not cache_path.is_file():
        return {}
    values = {}
    for raw_line in cache_path.read_text(
        encoding="utf-8", errors="replace"
    ).splitlines():
        line = raw_line.strip()
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        values[key_and_type.split(":", 1)[0]] = value
    return values


def has_ninja_configuration(
    build_dir: Path, *, languages: Sequence[str] = ("C", "CXX")
) -> bool:
    """Return whether CMake produced complete Ninja metadata for each language."""
    cache = read_cmake_cache(build_dir / "CMakeCache.txt")
    if cache.get("CMAKE_GENERATOR") != "Ninja":
        return False
    if not (build_dir / "build.ninja").is_file():
        return False
    for language in languages:
        if language not in {"C", "CXX"}:
            raise ValueError(f"unsupported CMake compiler language: {language}")
        if not cache.get(f"CMAKE_{language}_COMPILER"):
            return False
    return True


def cache_matches(build_dir: Path, required: dict[str, str]) -> bool:
    cache = read_cmake_cache(build_dir / "CMakeCache.txt")
    return all(cache.get(key) == value for key, value in required.items())


def needs_fresh_configure(build_dir: Path) -> bool:
    """A pre-existing cache must be discarded before policy reconfiguration."""
    return (build_dir / "CMakeCache.txt").is_file()


def configure_command(
    source_dir: Path,
    build_dir: Path,
    *,
    options: Sequence[str] = (),
    python_executable: str | Path | None = None,
) -> list[str]:
    command = ["cmake"]
    if needs_fresh_configure(build_dir):
        command.append("--fresh")
    command.extend(
        [
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            *options,
        ]
    )
    if python_executable is not None:
        command.append(f"-DPython3_EXECUTABLE={python_executable}")
    return command


def run_checked(
    command: Sequence[str],
    *,
    error_type: type[CMakeBuildError] = CMakeBuildError,
) -> None:
    try:
        result = subprocess.run(command, check=False, stdout=sys.stderr)
    except FileNotFoundError as exc:
        raise error_type(f"required executable is missing: {command[0]}") from exc
    if result.returncode != 0:
        raise error_type(
            f"command failed with exit {result.returncode}: {' '.join(command)}"
        )
