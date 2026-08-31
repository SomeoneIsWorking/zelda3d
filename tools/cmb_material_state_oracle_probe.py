#!/usr/bin/env python3
"""Cache whether OoT3D's direct CMB material-state submit path is reached."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Protocol

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_cache import OracleCache
from harness_gameplay import boot_to_gameplay, set_time_of_day
from harness_paths import GAMEPLAY_STATE
from harness_process import spawn
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 2
# `FUN_003fbba8` gates a material state at runtime offset +0x174, configures
# PICA state, then calls the direct material-stage submitter `FUN_003fb5ec`.
MATERIAL_STATE_FUNCTION = 0x003FBBA8
MATERIAL_SUBMIT_FUNCTION = 0x003FB5EC
# `FUN_003fcc70` receives a target object at context+4 and its virtual table at
# context+8, invokes table+4, then invokes table+8 with the target object.
VIRTUAL_DISPATCH_FUNCTION = 0x003FCC70
VIRTUAL_TARGET_OFFSET = 0x04
VIRTUAL_TABLE_OFFSET = 0x08
VIRTUAL_PREPARE_SLOT_OFFSET = 0x04
VIRTUAL_DRAW_SLOT_OFFSET = 0x08
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
TRACE_RUN_FRAMES = 2

PC_HITS_RE = re.compile(r"^ok pchits (?P<count>\d+)$")
REGISTER_RE = re.compile(r"\br(?P<register>[0-3])=0x(?P<value>[0-9a-fA-F]{8})\b")
WORD_RE = re.compile(r"^ok 0x(?P<word>[0-9a-fA-F]{8})$")


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> Any | None: ...

    def put_probe(
        self, name: str, frame: int, args: dict[str, Any], data: Any
    ) -> None: ...


def target_function(target: str) -> int:
    if target == "material-state":
        return MATERIAL_STATE_FUNCTION
    if target == "virtual-dispatch":
        return VIRTUAL_DISPATCH_FUNCTION
    raise ValueError(f"unknown CMB material-state target: {target}")


def probe_args(
    entrance: int, daytime: int, settle_frames: int, target: str
) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "trace_run_frames": TRACE_RUN_FRAMES,
        "target": target,
        "target_function": target_function(target),
        "material_state_function": MATERIAL_STATE_FUNCTION,
        "material_submit_function": MATERIAL_SUBMIT_FUNCTION,
        "virtual_dispatch_function": VIRTUAL_DISPATCH_FUNCTION,
        "virtual_target_offset": VIRTUAL_TARGET_OFFSET,
        "virtual_table_offset": VIRTUAL_TABLE_OFFSET,
        "virtual_prepare_slot_offset": VIRTUAL_PREPARE_SLOT_OFFSET,
        "virtual_draw_slot_offset": VIRTUAL_DRAW_SLOT_OFFSET,
        "texture_pack": 0,
    }


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + TRACE_RUN_FRAMES


def parse_pc_hits(
    lines: list[str], watched_path: str = "direct CMB material-state path"
) -> list[str]:
    if not lines:
        raise RuntimeError("oracle pcwatch returned no response")
    match = PC_HITS_RE.match(lines[0])
    if match is None or lines[-1] != "ok end":
        raise RuntimeError(f"oracle pcwatch returned malformed response: {lines}")
    records = lines[1:-1]
    if len(records) != int(match.group("count")):
        raise RuntimeError("oracle pcwatch count does not match returned records")
    if not records:
        raise RuntimeError(f"{watched_path} was not reached")
    return records


def read_word(harness: Any, address: int) -> int:
    response = harness.send(f"r32 0x{address:08x}")
    match = WORD_RE.match(response)
    if match is None:
        raise RuntimeError(f"oracle r32 0x{address:08x} failed: {response}")
    return int(match.group("word"), 16)


def register_value(record: str, register: int) -> int:
    for match in REGISTER_RE.finditer(record):
        if int(match.group("register")) == register:
            return int(match.group("value"), 16)
    raise RuntimeError(f"oracle PC record has no r{register}: {record}")


def capture_live(
    entrance: int, daytime: int, settle_frames: int, target: str
) -> dict[str, Any]:
    watched_function = target_function(target)
    harness = spawn()
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"pcwatch 0x{watched_function:08x}")
        if response != f"ok pcwatch 0x{watched_function:08x}":
            raise RuntimeError(f"oracle guest-PC watch failed: {response}")
        harness.send(f"run {TRACE_RUN_FRAMES}")
        watched_path = (
            "direct CMB material-state path"
            if target == "material-state"
            else "indirect CMB material dispatch"
        )
        records = parse_pc_hits(harness.send_multiline("pchits"), watched_path)
        harness.send("pcwatch off")
        result = {
            "capture_version": CAPTURE_VERSION,
            "target": target,
            "target_function": f"0x{watched_function:08x}",
            "material_state_function": f"0x{MATERIAL_STATE_FUNCTION:08x}",
            "material_submit_function": f"0x{MATERIAL_SUBMIT_FUNCTION:08x}",
            "pc_records": records,
        }
        if target == "virtual-dispatch":
            context = register_value(records[0], 0)
            table = read_word(harness, context + VIRTUAL_TABLE_OFFSET)
            if table == 0:
                raise RuntimeError("virtual CMB dispatch supplied a null method table")
            result.update(
                {
                    "context": f"0x{context:08x}",
                    "target_object": f"0x{read_word(harness, context + VIRTUAL_TARGET_OFFSET):08x}",
                    "method_table": f"0x{table:08x}",
                    "prepare_method": f"0x{read_word(harness, table + VIRTUAL_PREPARE_SLOT_OFFSET):08x}",
                    "draw_method": f"0x{read_word(harness, table + VIRTUAL_DRAW_SLOT_OFFSET):08x}",
                }
            )
        return result
    finally:
        harness.close()


def capture_probe(
    cache: CacheLike,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
    target: str = "material-state",
) -> tuple[dict[str, Any], bool]:
    args = probe_args(entrance, daytime, settle_frames, target)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("cmb-material-state", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("cmb-material-state-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(entrance, daytime, settle_frames, target)
    except RuntimeError as error:
        cache.put_probe(
            "cmb-material-state-failure",
            frame,
            args,
            {"capture_version": CAPTURE_VERSION, "error": str(error)},
        )
        raise
    cache.put_probe("cmb-material-state", frame, args, result)
    return result, False


def cache_context() -> OracleCache:
    apply_repo_environment(REPO, os.environ)
    os.environ["ZELDA3D_HARNESS_TEXPACK"] = "off"
    return OracleCache(GAMEPLAY_STATE)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--entrance", type=lambda value: int(value, 0), default=DEFAULT_ENTRANCE)
    parser.add_argument("--daytime", type=lambda value: int(value, 0), default=DEFAULT_DAYTIME)
    parser.add_argument("--settle-frames", type=int, default=DEFAULT_SETTLE_FRAMES)
    parser.add_argument(
        "--target",
        choices=("material-state", "virtual-dispatch"),
        default="material-state",
        help="material-state watches the direct PICA route; virtual-dispatch resolves its table",
    )
    args = parser.parse_args(arguments)
    if args.settle_frames < 0:
        parser.error("--settle-frames must be non-negative")
    try:
        cache = cache_context()
        result, hit = capture_probe(
            cache, args.entrance, args.daytime, args.settle_frames, args.target
        )
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
