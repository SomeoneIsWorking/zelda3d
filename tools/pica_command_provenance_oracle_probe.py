#!/usr/bin/env python3
"""Cache-owned command-list provenance for one deterministic PICA draw."""

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

CAPTURE_VERSION = 2
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
DISCOVERY_RUN_FRAMES = 2
OUTDIR = REPO / "scratch" / "pica_command_provenance"

DRAW_RE = re.compile(
    r"^draw n=(?P<draw>\d+)\b.*\bcmdList=(?P<address>[0-9a-fA-F]{8})/"
    r"(?P<word_index>\d+)/(?P<word_count>\d+)\b"
)


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> dict[str, Any] | None: ...

    def put_probe(self, name: str, frame: int, args: dict[str, Any], result: dict[str, Any]) -> Path: ...

    def put_artifact(
        self, name: str, args: dict[str, Any], source: Path, suffix: str | None = None
    ) -> Path: ...


def probe_args(draw: int, label: str, entrance: int, daytime: int, settle_frames: int) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "draw": draw,
        "label": label,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "discovery_run_frames": DISCOVERY_RUN_FRAMES,
        "texture_pack": 0,
    }


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + DISCOVERY_RUN_FRAMES


def provenance_records(lines: list[str], draw: int) -> list[dict[str, int]]:
    records = []
    for line in lines:
        match = DRAW_RE.match(line)
        if match is not None and int(match.group("draw")) == draw:
            records.append(
                {
                    "draw": draw,
                    "command_list_address": int(match.group("address"), 16),
                    "command_list_word_index": int(match.group("word_index")),
                    "command_list_word_count": int(match.group("word_count")),
                }
            )
    return records


def parse_provenance(lines: list[str], draw: int) -> dict[str, int]:
    records = provenance_records(lines, draw)
    if not records:
        raise RuntimeError(f"oracle log has no command-list provenance for draw {draw}")
    if len(records) != 1:
        raise RuntimeError(f"oracle log has {len(records)} command-list records for draw {draw}")
    return records[0]


def capture_live(
    cache: CacheLike, args: dict[str, Any], draw: int, entrance: int, daytime: int, settle_frames: int
) -> dict[str, Any]:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    log_path = OUTDIR / "discovery.log"
    command_list_path = OUTDIR / "command-list.bin"
    harness = spawn()
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"vsuni_log {log_path}")
        if response != f"ok vsuni_log {log_path}":
            raise RuntimeError(f"oracle PICA logger failed: {response}")
        harness.send(f"run {DISCOVERY_RUN_FRAMES}")
        harness.send("vsuni_log off")
        if not log_path.is_file():
            raise RuntimeError("oracle PICA logger produced no discovery log")
        lines = log_path.read_text().splitlines()
        provenance = parse_provenance(lines, draw)
        artifact = cache.put_artifact("pica-command-provenance", args, log_path, suffix=".log")
        command_list_bytes = provenance["command_list_word_count"] * 4
        dump_response = harness.send_multiline(
            f"dumpphys 0x{provenance['command_list_address']:08x} {command_list_bytes} {command_list_path}"
        )
        if len(dump_response) != 1 or not dump_response[0].startswith("ok dumpphys "):
            raise RuntimeError(f"oracle command-list dump failed: {dump_response}")
        if not command_list_path.is_file():
            raise RuntimeError("oracle command-list dump created no artifact")
        if command_list_path.stat().st_size != command_list_bytes:
            raise RuntimeError(
                f"oracle command-list dump has {command_list_path.stat().st_size} bytes, "
                f"expected {command_list_bytes}"
            )
        command_list_artifact = cache.put_artifact(
            "pica-command-list", args, command_list_path, suffix=".bin"
        )
        return {
            "capture_version": CAPTURE_VERSION,
            "artifact": str(artifact),
            "command_list_artifact": str(command_list_artifact),
            "command_list_bytes": command_list_bytes,
            **provenance,
        }
    finally:
        harness.close()
        log_path.unlink(missing_ok=True)
        command_list_path.unlink(missing_ok=True)


def capture_probe(
    cache: CacheLike,
    draw: int,
    label: str,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
) -> tuple[dict[str, Any], bool]:
    args = probe_args(draw, label, entrance, daytime, settle_frames)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("pica-command-provenance", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("pica-command-provenance-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(cache, args, draw, entrance, daytime, settle_frames)
    except (OSError, RuntimeError) as error:
        cache.put_probe(
            "pica-command-provenance-failure",
            frame,
            args,
            {"capture_version": CAPTURE_VERSION, "error": str(error)},
        )
        raise
    cache.put_probe("pica-command-provenance", frame, args, result)
    return result, False


def cache_context() -> OracleCache:
    apply_repo_environment(REPO, os.environ)
    os.environ["ZELDA3D_HARNESS_TEXPACK"] = "off"
    return OracleCache(GAMEPLAY_STATE)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--draw", required=True, type=int)
    parser.add_argument("--label", required=True)
    parser.add_argument("--entrance", type=lambda value: int(value, 0), default=DEFAULT_ENTRANCE)
    parser.add_argument("--daytime", type=lambda value: int(value, 0), default=DEFAULT_DAYTIME)
    parser.add_argument("--settle-frames", type=int, default=DEFAULT_SETTLE_FRAMES)
    args = parser.parse_args(arguments)
    if args.draw < 0:
        parser.error("--draw must be non-negative")
    if args.settle_frames < 0:
        parser.error("--settle-frames must be non-negative")
    try:
        cache = cache_context()
        result, hit = capture_probe(
            cache, args.draw, args.label, args.entrance, args.daytime, args.settle_frames
        )
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
