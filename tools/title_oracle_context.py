"""Shared deterministic title-clock and oracle-cache context."""

from __future__ import annotations

from collections.abc import MutableMapping
from harness_paths import TITLE_STATE

SAVESTATE = TITLE_STATE

# RE'd and measured in title_ab.py / debug_journal/2026-07-09-title-cs-phase-sync.md:
# the settled oracle state starts at title cs=88 and advances one title unit per
# two oracle retro_run calls.
ORACLE_INITIAL_TITLE_CS = 88
ORACLE_STEPS_PER_TITLE_CS = 2


def configure_vanilla_title_context(environment: MutableMapping[str, str]) -> None:
    """Pin the ROM-authored title inputs before constructing an OracleCache."""
    environment["ZELDA3D_HARNESS_TEXPACK"] = "off"


def oracle_frame_for_title_cs(title_cs: int) -> int:
    if title_cs < ORACLE_INITIAL_TITLE_CS:
        raise ValueError(
            f"title cs {title_cs} predates cached state cs={ORACLE_INITIAL_TITLE_CS}"
        )
    return (title_cs - ORACLE_INITIAL_TITLE_CS) * ORACLE_STEPS_PER_TITLE_CS
