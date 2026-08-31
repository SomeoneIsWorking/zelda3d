#!/usr/bin/env python3
"""Cache the active CMB model draw dispatch observed in the embedded oracle."""

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
# `FUN_004c7ab0` marks a submitted CMB model and invokes its vtable `+0x08`
# draw slot.  At entry r1+0x28 is the model object (decomp: 004c7ab0.c).
MODEL_DISPATCH_FUNCTION = 0x004C7AB0
MODEL_POINTER_OFFSET = 0x28
MODEL_DRAW_SLOT_OFFSET = 0x08
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
TRACE_RUN_FRAMES = 2

PC_HITS_RE = re.compile(r"^ok pchits (?P<count>\d+)$")
REGISTER_RE = re.compile(r"\br1=0x(?P<r1>[0-9a-fA-F]{8})\b")
WORD_RE = re.compile(r"^ok 0x(?P<word>[0-9a-fA-F]{8})$")


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
        "dispatch_function": MODEL_DISPATCH_FUNCTION,
        "model_pointer_offset": MODEL_POINTER_OFFSET,
        "model_draw_slot_offset": MODEL_DRAW_SLOT_OFFSET,
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
        raise RuntimeError("active CMB model dispatcher was not reached")
    return records


def read_word(harness: Any, address: int) -> int:
    response = harness.send(f"r32 0x{address:08x}")
    match = WORD_RE.match(response)
    if match is None:
        raise RuntimeError(f"oracle r32 0x{address:08x} failed: {response}")
    return int(match.group("word"), 16)


def capture_live(entrance: int, daytime: int, settle_frames: int) -> dict[str, Any]:
    harness = spawn()
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"pcwatch 0x{MODEL_DISPATCH_FUNCTION:08x}")
        if response != f"ok pcwatch 0x{MODEL_DISPATCH_FUNCTION:08x}":
            raise RuntimeError(f"oracle guest-PC watch failed: {response}")
        harness.send(f"run {TRACE_RUN_FRAMES}")
        records = parse_pc_hits(harness.send_multiline("pchits"))
        harness.send("pcwatch off")
        record = records[0]
        match = REGISTER_RE.search(record)
        if match is None:
            raise RuntimeError(f"oracle dispatcher record has no r1: {record}")
        call_context = int(match.group("r1"), 16)
        model = read_word(harness, call_context + MODEL_POINTER_OFFSET)
        if model == 0:
            raise RuntimeError("active CMB dispatcher supplied a null model object")
        vtable = read_word(harness, model)
        draw_method = read_word(harness, vtable + MODEL_DRAW_SLOT_OFFSET)
        if draw_method == 0:
            raise RuntimeError("active CMB model vtable has a null +0x08 draw slot")
        return {
            "capture_version": CAPTURE_VERSION,
            "dispatch_function": f"0x{MODEL_DISPATCH_FUNCTION:08x}",
            "pc_records": records,
            "call_context": f"0x{call_context:08x}",
            "model": f"0x{model:08x}",
            "vtable": f"0x{vtable:08x}",
            "draw_method": f"0x{draw_method:08x}",
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
    cached = cache.get_probe("cmb-model-dispatch", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("cmb-model-dispatch-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(entrance, daytime, settle_frames)
    except RuntimeError as error:
        cache.put_probe(
            "cmb-model-dispatch-failure",
            frame,
            args,
            {"capture_version": CAPTURE_VERSION, "error": str(error)},
        )
        raise
    cache.put_probe("cmb-model-dispatch", frame, args, result)
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
