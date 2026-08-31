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

CAPTURE_VERSION = 1
# `FUN_003fbba8` gates a material state at runtime offset +0x174, configures
# PICA state, then calls the direct material-stage submitter `FUN_003fb5ec`.
MATERIAL_STATE_FUNCTION = 0x003FBBA8
MATERIAL_SUBMIT_FUNCTION = 0x003FB5EC
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
TRACE_RUN_FRAMES = 2

PC_HITS_RE = re.compile(r"^ok pchits (?P<count>\d+)$")


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> Any | None: ...

    def put_probe(
        self, name: str, frame: int, args: dict[str, Any], data: Any
    ) -> None: ...


def probe_args(entrance: int, daytime: int, settle_frames: int) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "trace_run_frames": TRACE_RUN_FRAMES,
        "material_state_function": MATERIAL_STATE_FUNCTION,
        "material_submit_function": MATERIAL_SUBMIT_FUNCTION,
        "texture_pack": 0,
    }


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + TRACE_RUN_FRAMES


def parse_pc_hits(lines: list[str]) -> list[str]:
    if not lines:
        raise RuntimeError("oracle pcwatch returned no response")
    match = PC_HITS_RE.match(lines[0])
    if match is None or lines[-1] != "ok end":
        raise RuntimeError(f"oracle pcwatch returned malformed response: {lines}")
    records = lines[1:-1]
    if len(records) != int(match.group("count")):
        raise RuntimeError("oracle pcwatch count does not match returned records")
    if not records:
        raise RuntimeError("direct CMB material-state path was not reached")
    return records


def capture_live(entrance: int, daytime: int, settle_frames: int) -> dict[str, Any]:
    harness = spawn()
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"pcwatch 0x{MATERIAL_STATE_FUNCTION:08x}")
        if response != f"ok pcwatch 0x{MATERIAL_STATE_FUNCTION:08x}":
            raise RuntimeError(f"oracle guest-PC watch failed: {response}")
        harness.send(f"run {TRACE_RUN_FRAMES}")
        records = parse_pc_hits(harness.send_multiline("pchits"))
        harness.send("pcwatch off")
        return {
            "capture_version": CAPTURE_VERSION,
            "material_state_function": f"0x{MATERIAL_STATE_FUNCTION:08x}",
            "material_submit_function": f"0x{MATERIAL_SUBMIT_FUNCTION:08x}",
            "pc_records": records,
        }
    finally:
        harness.close()


def capture_probe(
    cache: CacheLike,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
) -> tuple[dict[str, Any], bool]:
    args = probe_args(entrance, daytime, settle_frames)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("cmb-material-state", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("cmb-material-state-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(entrance, daytime, settle_frames)
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
    args = parser.parse_args(arguments)
    if args.settle_frames < 0:
        parser.error("--settle-frames must be non-negative")
    try:
        cache = cache_context()
        result, hit = capture_probe(cache, args.entrance, args.daytime, args.settle_frames)
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
