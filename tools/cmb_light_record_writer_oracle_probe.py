#!/usr/bin/env python3
"""Cache the writer PCs for CmbRenderer's three live light-source records."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Callable, Protocol

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from harness_cache import OracleCache
from harness_gameplay import boot_to_gameplay, set_time_of_day
from harness_paths import GAMEPLAY_STATE
from harness_process import spawn
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 1
FINALIZER_FUNCTION = 0x003FA34C
CMB_RENDERER_VTABLE = 0x004EBD98
LIGHT_RECORD_POINTER_OFFSET = 0x10
LIGHT_RECORD_COUNT = 3
LIGHT_RECORD_STRIDE = 0x60
LIGHT_RECORD_BYTES = LIGHT_RECORD_COUNT * LIGHT_RECORD_STRIDE
DEFAULT_ENTRANCE = 0x030D
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
POST_RESOLVE_RUN_FRAMES = 2
MAX_WATCH_RECORDS = 128
OUTDIR = REPO / "scratch" / "cmb_light_record_writer"

PC_HIT_RE = re.compile(
    r"^  pc=0x(?P<pc>[0-9a-fA-F]{8}) lr=0x(?P<lr>[0-9a-fA-F]{8}) ticks=(?P<ticks>\d+) "
    r"r0=0x(?P<r0>[0-9a-fA-F]{8}) r1=0x(?P<r1>[0-9a-fA-F]{8}) "
    r"r2=0x(?P<r2>[0-9a-fA-F]{8}) r3=0x(?P<r3>[0-9a-fA-F]{8}) sp=0x(?P<sp>[0-9a-fA-F]{8})$"
)
HITS_RE = re.compile(r"^ok hits (?P<count>\d+)$")
READ_U32_RE = re.compile(r"^ok 0x(?P<value>[0-9a-fA-F]{8})$")


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> dict[str, Any] | None: ...

    def put_probe(self, name: str, frame: int, args: dict[str, Any], result: dict[str, Any]) -> Path: ...

    def put_artifact(
        self, name: str, args: dict[str, Any], source: Path, suffix: str | None = None
    ) -> Path: ...


def probe_args(entrance: int, daytime: int, settle_frames: int) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "post_resolve_run_frames": POST_RESOLVE_RUN_FRAMES,
        "finalizer_function": f"0x{FINALIZER_FUNCTION:08x}",
        "renderer_vtable": f"0x{CMB_RENDERER_VTABLE:08x}",
        "light_record_pointer_offset": f"0x{LIGHT_RECORD_POINTER_OFFSET:x}",
        "light_record_stride": f"0x{LIGHT_RECORD_STRIDE:x}",
        "light_record_count": LIGHT_RECORD_COUNT,
        "texture_pack": 0,
    }


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + POST_RESOLVE_RUN_FRAMES


def parse_pc_hits(lines: list[str]) -> list[dict[str, int]]:
    if not lines or lines[-1] != "ok end":
        raise RuntimeError(f"pcwatch returned malformed response: {lines}")
    header = re.fullmatch(r"ok pchits (?P<count>\d+)", lines[0])
    if header is None:
        raise RuntimeError(f"pcwatch returned malformed header: {lines[0]!r}")
    records: list[dict[str, int]] = []
    for line in lines[1:-1]:
        match = PC_HIT_RE.fullmatch(line)
        if match is None:
            raise RuntimeError(f"pcwatch returned malformed record: {line!r}")
        records.append({name: int(value, 16) if name != "ticks" else int(value) for name, value in match.groupdict().items()})
    if len(records) != int(header.group("count")):
        raise RuntimeError(f"pcwatch count mismatch: header={header.group('count')} records={len(records)}")
    return records


def read_u32(harness: Any, address: int) -> int:
    response = harness.send(f"r32 0x{address:08x}")
    match = READ_U32_RE.fullmatch(response.strip())
    if match is None:
        raise RuntimeError(f"r32 0x{address:08x} returned malformed response: {response!r}")
    return int(match.group("value"), 16)


def resolve_light_records(harness: Any, pc_hits: list[dict[str, int]]) -> tuple[int, int]:
    if len(pc_hits) != 1:
        raise RuntimeError(f"expected exactly one finalizer PC hit, got {len(pc_hits)}")
    record = pc_hits[0]
    if record["pc"] != FINALIZER_FUNCTION:
        raise RuntimeError(f"pcwatch recorded 0x{record['pc']:08x}, not finalizer 0x{FINALIZER_FUNCTION:08x}")
    renderer = record["r0"]
    if read_u32(harness, renderer) != CMB_RENDERER_VTABLE:
        raise RuntimeError(f"finalizer r0=0x{renderer:08x} does not carry the CmbRenderer vtable")
    records = read_u32(harness, renderer + LIGHT_RECORD_POINTER_OFFSET)
    if records == 0 or records > 0xFFFFFFFF - LIGHT_RECORD_BYTES:
        raise RuntimeError(f"CmbRenderer light-record pointer is invalid: 0x{records:08x}")
    return renderer, records


def parse_watch_hits(lines: list[str]) -> int:
    if not lines or lines[-1] != "ok end":
        raise RuntimeError(f"light-record watch returned malformed response: {lines}")
    match = HITS_RE.fullmatch(lines[0])
    if match is None:
        raise RuntimeError(f"light-record watch returned malformed header: {lines[0]!r}")
    count = int(match.group("count"))
    if count == 0:
        raise RuntimeError("light-record watch captured 0 writes")
    if count >= MAX_WATCH_RECORDS:
        raise RuntimeError("light-record watch reached its 128-record cap; capture is truncated")
    return count


def _capture_live(
    cache: CacheLike, args: dict[str, Any], entrance: int, daytime: int, settle_frames: int
) -> dict[str, Any]:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    raw_path = OUTDIR / "light-record-writers.log"
    harness = spawn()
    watched_records: int | None = None
    records_address: int | None = None
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"pcwatch 0x{FINALIZER_FUNCTION:08x}")
        if response != f"ok pcwatch 0x{FINALIZER_FUNCTION:08x}":
            raise RuntimeError(f"failed to arm finalizer PC watch: {response}")
        harness.send(f"run {POST_RESOLVE_RUN_FRAMES}")
        pc_lines = harness.send_multiline("pchits")
        harness.send("pcwatch off")
        renderer, records_address = resolve_light_records(harness, parse_pc_hits(pc_lines))
        response = harness.send(f"watch 0x{records_address:08x} {LIGHT_RECORD_BYTES}")
        if response != f"ok watch 0x{records_address:08x} {LIGHT_RECORD_BYTES}":
            raise RuntimeError(f"failed to arm light-record watch: {response}")
        harness.send(f"hitclear 0x{records_address:08x}")
        harness.send(f"run {POST_RESOLVE_RUN_FRAMES}")
        watch_lines = harness.send_multiline(f"hits 0x{records_address:08x}")
        watched_records = parse_watch_hits(watch_lines)
        raw_path.write_text("\n".join(["# finalizer entry", *pc_lines, "# light-record writes", *watch_lines]) + "\n")
        artifact = cache.put_artifact("cmb-light-record-writers", args, raw_path, suffix=".log")
        return {
            "capture_version": CAPTURE_VERSION,
            "renderer": f"0x{renderer:08x}",
            "light_records": f"0x{records_address:08x}",
            "light_record_bytes": LIGHT_RECORD_BYTES,
            "watch_records": watched_records,
            "artifact": str(artifact),
        }
    finally:
        if records_address is not None:
            harness.send(f"unwatch 0x{records_address:08x} {LIGHT_RECORD_BYTES}")
        harness.close()


def capture_probe(
    cache: CacheLike,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
    *,
    gameplay_state: Path = GAMEPLAY_STATE,
    capture_live: Callable[[CacheLike, dict[str, Any], int, int, int], dict[str, Any]] = _capture_live,
) -> tuple[dict[str, Any], bool]:
    args = probe_args(entrance, daytime, settle_frames)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("cmb-light-record-writers", frame, args)
    if cached is not None:
        return cached, True
    if not gameplay_state.is_file():
        raise RuntimeError(f"current render-contract gameplay state is missing: {gameplay_state}")
    result = capture_live(cache, args, entrance, daytime, settle_frames)
    cache.put_probe("cmb-light-record-writers", frame, args, result)
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
