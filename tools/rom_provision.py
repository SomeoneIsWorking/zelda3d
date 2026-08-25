#!/usr/bin/env python3
"""Resolve user-supplied Zelda ROMs and expose N64 extraction input safely."""

from __future__ import annotations

import argparse
import os
import shlex
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path

from repo_environment import apply_repo_environment
from rom_identity import is_oot3d_rom, is_oot_n64_rom

OOT3D_NAME = "ZELDA3D_OOT3D_ROM"
OOT_NAME = "ZELDA3D_OOT_ROM"
OOT3D_PATTERNS = ("oot3d.3ds", "*.3ds")
OOT_PATTERNS = ("oot.z64", "*.z64", "*.n64", "*.v64")
N64_ARCHIVE_PATTERNS = ("oot*.o2r", "oot*.otr")
N64_ROM_PATTERNS = ("*.z64", "*.n64", "*.v64")


class RomProvisionError(RuntimeError):
    """Raised when a ROM input cannot be exposed without overwriting local data."""


def _first_file(
    repo: Path,
    patterns: Sequence[str],
    predicate=lambda _path: True,
) -> Path | None:
    seen: set[Path] = set()
    matches: list[Path] = []
    for pattern in patterns:
        for candidate in sorted(repo.glob(pattern)):
            if candidate in seen:
                continue
            seen.add(candidate)
            if candidate.is_file() and predicate(candidate):
                matches.append(candidate.resolve())
        if matches:
            break
    if len(matches) > 1:
        raise RomProvisionError(
            "multiple Ocarina of Time ROM drop-ins match; set the corresponding "
            "ZELDA3D_OOT* environment variable explicitly"
        )
    return matches[0] if matches else None


def resolve_rom_environment(
    repo: Path, environment: Mapping[str, str] | None = None
) -> dict[str, str]:
    """Apply caller > .env > canonical/drop-in priority without executing `.env`."""
    repo = repo.resolve()
    resolved = dict(os.environ if environment is None else environment)
    caller_values = {name for name in (OOT3D_NAME, OOT_NAME) if resolved.get(name)}
    apply_repo_environment(repo, resolved)
    for name in (OOT3D_NAME, OOT_NAME):
        if name in caller_values or not resolved.get(name):
            continue
        path = Path(resolved[name]).expanduser()
        resolved[name] = str(path if path.is_absolute() else (repo / path).resolve())
    if not resolved.get(OOT3D_NAME):
        oot3d = _first_file(repo, OOT3D_PATTERNS, is_oot3d_rom)
        if oot3d is not None:
            resolved[OOT3D_NAME] = str(oot3d)
    if not resolved.get(OOT_NAME):
        oot = _first_file(repo, OOT_PATTERNS, is_oot_n64_rom)
        if oot is not None:
            resolved[OOT_NAME] = str(oot)
    return resolved


def _contains_file(directory: Path, patterns: Sequence[str]) -> bool:
    return any(
        candidate.is_file()
        for pattern in patterns
        for candidate in directory.glob(pattern)
    )


def provision_n64_extraction_rom(
    app_dir: Path, environment: Mapping[str, str]
) -> Path | None:
    """Symlink the discovered N64 ROM only when first-run extraction still needs it."""
    rom_value = environment.get(OOT_NAME)
    if not rom_value:
        return None
    rom = Path(rom_value).expanduser().resolve()
    if not rom.is_file():
        raise RomProvisionError(f"{OOT_NAME} does not name an existing file: {rom}")
    if not is_oot_n64_rom(rom):
        raise RomProvisionError(
            f"{OOT_NAME} is not a recognized Ocarina of Time ROM: {rom}"
        )
    app_dir.mkdir(parents=True, exist_ok=True)
    if _contains_file(app_dir, N64_ARCHIVE_PATTERNS) or _contains_file(
        app_dir, N64_ROM_PATTERNS
    ):
        return None

    destination = app_dir / f"zelda3d-source{rom.suffix.lower()}"
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() and destination.resolve() == rom:
            return destination
        raise RomProvisionError(
            f"refusing to replace existing extraction input: {destination}"
        )
    destination.symlink_to(rom)
    return destination


def require_oot3d_rom(repo: Path, environment: Mapping[str, str]) -> Path:
    value = environment.get(OOT3D_NAME)
    if not value:
        raise RomProvisionError(
            "no OoT3D .3ds found — set ZELDA3D_OOT3D_ROM, add ./.env, "
            f"or drop a *.3ds into {repo}"
        )
    rom = Path(value).expanduser()
    if not rom.is_file():
        raise RomProvisionError(f"{OOT3D_NAME} does not name an existing file: {rom}")
    if not is_oot3d_rom(rom):
        raise RomProvisionError(
            f"{OOT3D_NAME} is not a recognized decrypted Ocarina of Time 3D ROM: {rom}"
        )
    return rom.resolve()


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--app-dir", type=Path)
    parser.add_argument(
        "--shell", action="store_true", help="print shell-safe resolved assignments"
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        environment = resolve_rom_environment(args.repo)
        require_oot3d_rom(args.repo, environment)
        if args.app_dir is not None:
            provision_n64_extraction_rom(args.app_dir, environment)
    except RomProvisionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if args.shell:
        for name in (OOT3D_NAME, OOT_NAME):
            if environment.get(name):
                print(f"export {name}={shlex.quote(environment[name])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
