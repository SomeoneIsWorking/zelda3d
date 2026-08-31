#!/usr/bin/env python3
"""Cache-owned guest-PC observation for one deterministic gameplay fixture."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Protocol

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from harness_cache import OracleCache
from harness_gameplay import boot_to_gameplay, set_time_of_day
from harness_paths import GAMEPLAY_STATE
from harness_process import spawn
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 1
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
TRACE_RUN_FRAMES = 2
PC_HITS_RE = re.compile(r"ok pchits (\d+)$")


class CacheLike(Protocol):
    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> dict[str, Any] | None: ...

    def put_probe(self, name: str, frame: int, args: dict[str, Any], result: dict[str, Any]) -> Path: ...


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + TRACE_RUN_FRAMES


def probe_args(function: int, label: str, entrance: int, daytime: int, settle_frames: int) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "function": function,
        "label": label,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "trace_run_frames": TRACE_RUN_FRAMES,
        "texture_pack": 0,
    }


def parse_pc_hits(lines: list[str]) -> list[str]:
    if not lines:
        raise RuntimeError("oracle pcwatch returned no response")
    match = PC_HITS_RE.fullmatch(lines[0])
    if match is None or lines[-1] != "ok end":
        raise RuntimeError(f"oracle pcwatch returned malformed response: {lines}")
    records = lines[1:-1]
    if len(records) != int(match.group(1)):
        raise RuntimeError("oracle pcwatch count does not match returned records")
    if not records:
        raise RuntimeError("watched function was not reached in this fixture")
    return records


def capture_live(function: int, entrance: int, daytime: int, settle_frames: int) -> dict[str, Any]:
    harness = spawn()
    armed = False
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"pcwatch 0x{function:08x}")
        if response != f"ok pcwatch 0x{function:08x}":
            raise RuntimeError(f"oracle guest-PC watch failed: {response}")
        armed = True
        harness.send(f"run {TRACE_RUN_FRAMES}")
        records = parse_pc_hits(harness.send_multiline("pchits"))
        return {"capture_version": CAPTURE_VERSION, "function": f"0x{function:08x}", "pc_records": records}
    finally:
        if armed:
            harness.send("pcwatch off")
        harness.close()


def capture_probe(
    cache: CacheLike,
    function: int,
    label: str,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
) -> tuple[dict[str, Any], bool]:
    args = probe_args(function, label, entrance, daytime, settle_frames)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("pcwatch", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("pcwatch-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(function, entrance, daytime, settle_frames)
    except RuntimeError as error:
        cache.put_probe(
            "pcwatch-failure", frame, args, {"capture_version": CAPTURE_VERSION, "error": str(error)}
        )
        raise
    cache.put_probe("pcwatch", frame, args, result)
    return result, False


def cache_context() -> OracleCache:
    apply_repo_environment(REPO, os.environ)
    os.environ["ZELDA3D_HARNESS_TEXPACK"] = "off"
    return OracleCache(GAMEPLAY_STATE)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--function", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--label", required=True)
    parser.add_argument("--entrance", type=lambda value: int(value, 0), default=DEFAULT_ENTRANCE)
    parser.add_argument("--daytime", type=lambda value: int(value, 0), default=DEFAULT_DAYTIME)
    parser.add_argument("--settle-frames", type=int, default=DEFAULT_SETTLE_FRAMES)
    args = parser.parse_args(arguments)
    if not 0x00100000 <= args.function <= 0x08000000:
        parser.error("--function must be an OoT3D code address")
    if args.settle_frames < 0:
        parser.error("--settle-frames must be non-negative")
    try:
        cache = cache_context()
        result, hit = capture_probe(
            cache, args.function, args.label, args.entrance, args.daytime, args.settle_frames
        )
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
