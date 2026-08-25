"""Compilation-database loading and Clang compiler validation."""

from __future__ import annotations

import json
import re
import shlex
from collections.abc import Sequence
from pathlib import Path

from . import VerificationError
from .source_selection import SOURCE_SUFFIXES, is_first_party, repo_relative

CLANG_COMPILER_RE = re.compile(
    r"^clang(?:\+\+|-cl)?(?:-[0-9]+(?:\.[0-9]+)*)?(?:\.exe)?$"
)
COMPILER_CANDIDATE_RE = re.compile(
    r"^(?:cc|c\+\+|gcc|g\+\+|clang|clang\+\+|clang-cl)(?:-[0-9]+(?:\.[0-9]+)*)?(?:\.exe)?$"
)


def load_compile_commands(path: Path) -> list[dict[str, object]]:
    if not path.is_file():
        raise VerificationError(
            f"compilation database missing: {path}\n"
            "configure the Clang build first: cmake -S . -B Shipwright/build-cmake -G Ninja "
            "-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
        )
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise VerificationError(f"invalid compilation database {path}: {exc}") from exc
    if not isinstance(data, list) or not data:
        raise VerificationError(f"compilation database inspected nothing: {path}")
    return data


def command_arguments(entry: dict[str, object]) -> list[str]:
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and all(isinstance(item, str) for item in arguments):
        return arguments
    command = entry.get("command")
    if isinstance(command, str):
        return shlex.split(command)
    raise VerificationError(
        "compile_commands entry has neither string command nor string arguments"
    )


def compiler_name(entry: dict[str, object]) -> str:
    for argument in command_arguments(entry):
        name = Path(argument).name.lower()
        if COMPILER_CANDIDATE_RE.fullmatch(name):
            return name
    return "<unidentified>"


def verify_compilers(
    entries: Sequence[dict[str, object]], compile_commands: Path
) -> list[str]:
    failures = []
    for entry in entries:
        compiler = compiler_name(entry)
        if not CLANG_COMPILER_RE.fullmatch(compiler):
            failures.append(
                f"{entry.get('file', '<unknown>')}: non-Clang compiler {compiler}"
            )
            if len(failures) == 10:
                break

    cmake_root = compile_commands.parent / "CMakeFiles"
    for language in ("C", "CXX"):
        metadata = sorted(cmake_root.glob(f"*/CMake{language}Compiler.cmake"))
        if not metadata:
            continue
        match = re.search(
            rf'set\(CMAKE_{language}_COMPILER_ID "([^"]+)"\)', metadata[-1].read_text()
        )
        if not match or match.group(1) not in {"Clang", "AppleClang"}:
            compiler_id = match.group(1) if match else "<missing>"
            failures.append(
                f"CMake {language} compiler ID is {compiler_id}, expected Clang"
            )
    return failures


def compile_entry_path(entry: dict[str, object]) -> Path:
    raw = entry.get("file")
    if not isinstance(raw, str):
        raise VerificationError("compile_commands entry has no string file")
    path = Path(raw)
    if not path.is_absolute():
        directory = entry.get("directory")
        if not isinstance(directory, str):
            raise VerificationError(
                f"relative compile_commands file has no directory: {raw}"
            )
        path = Path(directory) / path
    return path.resolve()


def first_party_entries(
    repo: Path, entries: Sequence[dict[str, object]]
) -> dict[Path, dict[str, object]]:
    selected = {}
    for entry in entries:
        path = compile_entry_path(entry)
        try:
            relative = repo_relative(path, repo)
        except VerificationError:
            continue
        if path.suffix.lower() in SOURCE_SUFFIXES and is_first_party(relative):
            selected[path] = entry
    return selected
