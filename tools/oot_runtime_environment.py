"""Game environment construction and ROM policy for automated OoT sessions."""

from __future__ import annotations

import os
from collections.abc import Mapping

from oot_runtime_paths import OotRuntimePaths
from rom_provision import provision_n64_extraction_rom, resolve_rom_environment


def resolved_game_environment(
    paths: OotRuntimePaths,
    *,
    entrance: str,
    daytime: str,
    environment: Mapping[str, str] | None = None,
) -> dict[str, str]:
    base = dict(os.environ if environment is None else environment)
    resolved = resolve_rom_environment(paths.repo, base)
    provision_n64_extraction_rom(paths.game_dir, resolved)
    if not resolved.get("ZELDA3D_OOT3D_ROM"):
        raise RuntimeError(
            "no OoT3D .3ds found — set ZELDA3D_OOT3D_ROM, add ./.env, "
            f"or drop a *.3ds in {paths.repo}"
        )
    resolved.update(
        {
            "ZELDA3D_LAUNCHER": resolved.get("ZELDA3D_LAUNCHER", "0"),
            "ZELDA3D_WARP": resolved.get("ZELDA3D_WARP", "1"),
            "ZELDA3D_ENTRANCE": entrance,
            "ZELDA3D_TIME": daytime,
            "ZELDA3D_REPL": str(paths.repl_fifo),
        }
    )
    return resolved
