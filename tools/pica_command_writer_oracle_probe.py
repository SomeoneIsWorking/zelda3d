#!/usr/bin/env python3
"""Cache-owned writer-PC capture for a PICA command-list register packet."""

from __future__ import annotations

import argparse
import json
import os
import struct
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
from pica_command_provenance_oracle_probe import (
    DISCOVERY_RUN_FRAMES,
    TIME_SETTLE_FRAMES,
    parse_provenance,
    provenance_records,
)
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 2
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
FCRAM_PHYSICAL_BASE = 0x20000000
LINEAR_HEAP_VIRTUAL_BASE = 0x14000000
OUTDIR = REPO / "scratch" / "pica_command_writer"


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> dict[str, Any] | None: ...

    def put_probe(self, name: str, frame: int, args: dict[str, Any], result: dict[str, Any]) -> Path: ...

    def put_artifact(
        self, name: str, args: dict[str, Any], source: Path, suffix: str | None = None
    ) -> Path: ...


def probe_args(
    draw: int, register: int, label: str, entrance: int, daytime: int, settle_frames: int
) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "draw": draw,
        "register": register,
        "label": label,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "discovery_run_frames": DISCOVERY_RUN_FRAMES,
        "validation_run_frames": 1,
        "texture_pack": 0,
    }


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + DISCOVERY_RUN_FRAMES + 1


def parse_command_writes(payload: bytes, end_word: int) -> list[tuple[int, int, int]]:
    if len(payload) % 4:
        raise ValueError("PICA command list is not word-aligned")
    words = struct.unpack(f"<{len(payload) // 4}I", payload)
    if end_word > len(words):
        raise ValueError(f"draw cursor {end_word} exceeds {len(words)} command-list words")
    writes: list[tuple[int, int, int]] = []
    index = 0
    while index < end_word:
        if index % 2:
            index += 1
        if index + 1 >= end_word:
            break
        value, header = words[index], words[index + 1]
        register = header & 0xFFFF
        extra_count = (header >> 20) & 0xFF
        grouped = bool(header & 0x80000000)
        writes.append((index, register, value))
        for offset in range(extra_count):
            extra_index = index + 2 + offset
            if extra_index >= end_word:
                raise ValueError("PICA command extends beyond draw cursor")
            writes.append((extra_index, register + offset + 1 if grouped else register, words[extra_index]))
        index += 2 + extra_count
    return writes


def last_register_write(payload: bytes, end_word: int, register: int) -> tuple[int, int]:
    matches = [write for write in parse_command_writes(payload, end_word) if write[1] == register]
    if not matches:
        raise RuntimeError(f"PICA register 0x{register:03x} has no write before the selected draw")
    word_index, _, value = matches[-1]
    return word_index, value


def linear_virtual_address(physical_address: int, word_index: int) -> int:
    if physical_address < FCRAM_PHYSICAL_BASE:
        raise ValueError(f"PICA command list 0x{physical_address:08x} is outside FCRAM")
    return LINEAR_HEAP_VIRTUAL_BASE + physical_address - FCRAM_PHYSICAL_BASE + word_index * 4


def parse_hits(lines: list[str]) -> list[str]:
    if not lines or not lines[0].startswith("ok hits ") or lines[-1] != "ok end":
        raise RuntimeError(f"oracle memory watch returned malformed response: {lines}")
    records = lines[1:-1]
    if not records:
        raise RuntimeError("PICA command-buffer word was not rewritten in the validation frame")
    return records


def capture_live(
    cache: CacheLike,
    args: dict[str, Any],
    draw: int,
    register: int,
    entrance: int,
    daytime: int,
    settle_frames: int,
) -> dict[str, Any]:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    discovery_path = OUTDIR / "discovery.log"
    alternate_path = OUTDIR / "alternate.log"
    validation_path = OUTDIR / "validation.log"
    command_list_path = OUTDIR / "command-list.bin"
    harness = spawn()
    watch_address = 0
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"vsuni_log {discovery_path}")
        if response != f"ok vsuni_log {discovery_path}":
            raise RuntimeError(f"oracle PICA logger failed: {response}")
        harness.send(f"run {DISCOVERY_RUN_FRAMES}")
        harness.send("vsuni_log off")
        if not discovery_path.is_file():
            raise RuntimeError("oracle PICA logger produced no discovery log")
        provenance = parse_provenance(discovery_path.read_text().splitlines(), draw)
        response = harness.send(f"vsuni_log {alternate_path}")
        if response != f"ok vsuni_log {alternate_path}":
            raise RuntimeError(f"oracle alternate-buffer logger failed: {response}")
        harness.send("run 1")
        harness.send("vsuni_log off")
        if not alternate_path.is_file():
            raise RuntimeError("oracle alternate-buffer logger produced no log")
        alternate = parse_provenance(alternate_path.read_text().splitlines(), draw)
        if alternate["command_list_address"] == provenance["command_list_address"]:
            raise RuntimeError(
                "PICA command-list address did not rotate to an alternate buffer: "
                f"0x{alternate['command_list_address']:08x}"
            )
        list_bytes = alternate["command_list_word_count"] * 4
        dump_response = harness.send_multiline(
            f"dumpphys 0x{alternate['command_list_address']:08x} {list_bytes} {command_list_path}"
        )
        if len(dump_response) != 1 or not dump_response[0].startswith("ok dumpphys "):
            raise RuntimeError(f"oracle command-list dump failed: {dump_response}")
        payload = command_list_path.read_bytes()
        if len(payload) != list_bytes:
            raise RuntimeError(f"oracle command-list dump has {len(payload)} bytes, expected {list_bytes}")
        word_index, command_value = last_register_write(
            payload, alternate["command_list_word_index"], register
        )
        discovery_artifact = cache.put_artifact(
            "pica-command-writer-discovery", args, discovery_path, suffix=".log"
        )
        alternate_artifact = cache.put_artifact(
            "pica-command-writer-alternate", args, alternate_path, suffix=".log"
        )
        command_list_artifact = cache.put_artifact(
            "pica-command-writer-list", args, command_list_path, suffix=".bin"
        )
        watch_address = linear_virtual_address(alternate["command_list_address"], word_index)
        response = harness.send(f"watch 0x{watch_address:08x} 4")
        if response != f"ok watch 0x{watch_address:08x} 4":
            raise RuntimeError(f"oracle command-buffer watch failed: {response}")
        response = harness.send(f"vsuni_log {validation_path}")
        if response != f"ok vsuni_log {validation_path}":
            raise RuntimeError(f"oracle validation logger failed: {response}")
        harness.send("run 2")
        harness.send("vsuni_log off")
        hits = parse_hits(harness.send_multiline(f"hits 0x{watch_address:08x}"))
        response = harness.send(f"unwatch 0x{watch_address:08x} 4")
        if response != f"ok unwatch 0x{watch_address:08x} 4":
            raise RuntimeError(f"oracle command-buffer unwatch failed: {response}")
        watch_address = 0
        if not validation_path.is_file():
            raise RuntimeError("oracle validation logger produced no log")
        validation = provenance_records(validation_path.read_text().splitlines(), draw)
        if not any(record["command_list_address"] == alternate["command_list_address"] for record in validation):
            raise RuntimeError(
                "watched PICA command-list buffer was not reused during validation: "
                f"0x{alternate['command_list_address']:08x}"
            )
        validation_artifact = cache.put_artifact(
            "pica-command-writer-validation", args, validation_path, suffix=".log"
        )
        return {
            "capture_version": CAPTURE_VERSION,
            "draw": draw,
            "register": f"0x{register:03x}",
            "command_value": f"0x{command_value:08x}",
            "command_list_address": f"0x{alternate['command_list_address']:08x}",
            "command_list_word_index": word_index,
            "watch_address": f"0x{linear_virtual_address(alternate['command_list_address'], word_index):08x}",
            "writer_records": hits,
            "discovery_artifact": str(discovery_artifact),
            "alternate_artifact": str(alternate_artifact),
            "command_list_artifact": str(command_list_artifact),
            "validation_artifact": str(validation_artifact),
        }
    finally:
        if watch_address:
            harness.send(f"unwatch 0x{watch_address:08x} 4")
        harness.close()
        discovery_path.unlink(missing_ok=True)
        alternate_path.unlink(missing_ok=True)
        validation_path.unlink(missing_ok=True)
        command_list_path.unlink(missing_ok=True)


def capture_probe(
    cache: CacheLike,
    draw: int,
    register: int,
    label: str,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
) -> tuple[dict[str, Any], bool]:
    args = probe_args(draw, register, label, entrance, daytime, settle_frames)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("pica-command-writer", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("pica-command-writer-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(cache, args, draw, register, entrance, daytime, settle_frames)
    except (OSError, RuntimeError, ValueError) as error:
        cache.put_probe(
            "pica-command-writer-failure",
            frame,
            args,
            {"capture_version": CAPTURE_VERSION, "error": str(error)},
        )
        raise
    cache.put_probe("pica-command-writer", frame, args, result)
    return result, False


def cache_context() -> OracleCache:
    apply_repo_environment(REPO, os.environ)
    os.environ["ZELDA3D_HARNESS_TEXPACK"] = "off"
    return OracleCache(GAMEPLAY_STATE)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--draw", required=True, type=int)
    parser.add_argument("--register", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--label", required=True)
    parser.add_argument("--entrance", type=lambda value: int(value, 0), default=DEFAULT_ENTRANCE)
    parser.add_argument("--daytime", type=lambda value: int(value, 0), default=DEFAULT_DAYTIME)
    parser.add_argument("--settle-frames", type=int, default=DEFAULT_SETTLE_FRAMES)
    args = parser.parse_args(arguments)
    if args.draw < 0:
        parser.error("--draw must be non-negative")
    if not 0 <= args.register <= 0x2FF:
        parser.error("--register must be a PICA register index")
    if args.settle_frames < 0:
        parser.error("--settle-frames must be non-negative")
    try:
        cache = cache_context()
        result, hit = capture_probe(
            cache,
            args.draw,
            args.register,
            args.label,
            args.entrance,
            args.daytime,
            args.settle_frames,
        )
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
