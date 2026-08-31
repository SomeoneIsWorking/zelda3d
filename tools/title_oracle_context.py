"""Shared deterministic title-clock and oracle-cache context."""

from __future__ import annotations

from collections.abc import MutableMapping
import json

from harness_paths import TITLE_STATE, TITLE_STATE_METADATA, azahar_render_contract_marker

SAVESTATE = TITLE_STATE

ORACLE_STEPS_PER_TITLE_CS = 2


def initial_title_cs() -> int:
    """Read the cursor measured when the current checkpoint was created."""
    if not TITLE_STATE_METADATA.is_file():
        raise RuntimeError(
            f"missing title checkpoint metadata: {TITLE_STATE_METADATA}; "
            "create the checkpoint with tools/title_settle.py"
        )
    try:
        payload = json.loads(TITLE_STATE_METADATA.read_text())
        initial_cs = payload["initial_title_cs"]
        contract = payload["render_contract"]
        savestate = payload["savestate"]
    except (json.JSONDecodeError, KeyError, TypeError) as error:
        raise RuntimeError(f"invalid title checkpoint metadata: {TITLE_STATE_METADATA}") from error
    if contract != azahar_render_contract_marker() or savestate != SAVESTATE.name:
        raise RuntimeError(f"title checkpoint metadata does not match {SAVESTATE}")
    if not isinstance(initial_cs, int) or initial_cs < 0:
        raise RuntimeError(f"invalid initial title cursor in {TITLE_STATE_METADATA}")
    return initial_cs


def configure_vanilla_title_context(environment: MutableMapping[str, str]) -> None:
    """Pin the ROM-authored title inputs before constructing an OracleCache."""
    environment["ZELDA3D_HARNESS_TEXPACK"] = "off"


def oracle_frame_for_title_cs(title_cs: int) -> int:
    initial_cs = initial_title_cs()
    if title_cs < initial_cs:
        raise ValueError(
            f"title cs {title_cs} predates cached state cs={initial_cs}"
        )
    return (title_cs - initial_cs) * ORACLE_STEPS_PER_TITLE_CS
