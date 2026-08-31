#!/usr/bin/env python3
"""Cache-owned writer-PC capture for a PICA command-list register packet."""

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
from pica_command_list import last_register_write
from pica_command_provenance_oracle_probe import (
    DISCOVERY_RUN_FRAMES,
    TIME_SETTLE_FRAMES,
    parse_provenance,
)
from pica_command_provenance_oracle_probe import (
    capture_probe as capture_provenance_probe,
)
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 17
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
FCRAM_PHYSICAL_BASE = 0x20000000
LINEAR_HEAP_VIRTUAL_BASE = 0x14000000
OUTDIR = REPO / "scratch" / "pica_command_writer"
DEFAULT_LINEAR_RANGE = (0x14480000, 0x145A0000)
CPU_MODES = ("dynarmic", "interpreter")
SOURCE_TRACE_VERSION = 2
WATCH_TRACE_VERSION = 1
STATE_WATCH_TRACE_VERSION = 2
MEMLOG_RE = re.compile(
    r"^MW pc=0x[0-9a-fA-F]{8} lr=0x[0-9a-fA-F]{8} "
    r"va=0x(?P<address>[0-9a-fA-F]{8}) sz=(?P<size>\d+) "
)
MEMLOG_FIELD_RE = re.compile(r"(?:^|\s)(?P<name>[a-z0-9]+)=0x(?P<value>[0-9a-fA-F]+)")
OWNER_FIELD_OFFSETS = {
    "dispatch_table": "sr4p0",
    "renderer_context": "sr4p4",
    "packet_descriptors": "sr4p5c",
    "visibility_table": "sr4p6c",
}
VIRTUAL_SLOT_FIELDS = {"setup_a": "sr4t14", "setup_b": "sr4t20", "setup_c": "sr4t24"}
PACKET_DESCRIPTOR_FIELDS = {
    "source_pointer": "r4p8",
    "byte_count": "r4p10",
    "block_index": "r4p14",
}
COPY_LOOP_PC = 0x00371758
COPY_SOURCE_REGISTERS = ("r7", "r8", "r9", "r10")
CONFIG_TEMPLATE_STORE_PC = 0x0040CFE4
MATERIAL_DESCRIPTOR_BIND_PC = 0x004C6374
CONFIG_BUILDER_R10_OFFSET = 0x100
CONFIG_BUILDER_INPUT_WORDS = {
    "0x000": "r10bp0",
    "0x164": "r10bp164",
    "0x168": "r10bp168",
    "0x16c": "r10bp16c",
    "0x170": "r10bp170",
    "0x174": "r10bp174",
    "0x178": "r10bp178",
    "0x17c": "r10bp17c",
    "0x180": "r10bp180",
    "0x184": "r10bp184",
    "0x188": "r10bp188",
    "0x18c": "r10bp18c",
    "0x190": "r10bp190",
}
MATERIAL_DESCRIPTOR_WORDS = {
    "0x10": "r1p10",
    "0x14": "r1p14",
    "0x18": "r1p18",
    "0x1c": "r1p1c",
    "0x20": "r1p20",
    "0x24": "r1p24",
    "0x28": "r1p28",
}


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> dict[str, Any] | None: ...

    def put_probe(self, name: str, frame: int, args: dict[str, Any], result: dict[str, Any]) -> Path: ...

    def put_artifact(
        self, name: str, args: dict[str, Any], source: Path, suffix: str | None = None
    ) -> Path: ...


def probe_args(
    draw: int,
    register: int,
    label: str,
    entrance: int,
    daytime: int,
    settle_frames: int,
    linear_range: tuple[int, int],
    cpu_mode: str,
    source_range: tuple[int, int] | None,
    watch_address: int | None,
    state_watch_address: int | None,
) -> dict[str, Any]:
    args = {
        "capture_version": CAPTURE_VERSION,
        "draw": draw,
        "register": register,
        "label": label,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "discovery_run_frames": DISCOVERY_RUN_FRAMES,
        "linear_range": [f"0x{linear_range[0]:08x}", f"0x{linear_range[1]:08x}"],
        "cpu_mode": cpu_mode,
        "texture_pack": 0,
    }
    if source_range is not None:
        args["source_range"] = [f"0x{source_range[0]:08x}", f"0x{source_range[1]:08x}"]
        args["source_trace_version"] = SOURCE_TRACE_VERSION
    if watch_address is not None:
        args["watch_address"] = f"0x{watch_address:08x}"
        args["watch_trace_version"] = WATCH_TRACE_VERSION
    if state_watch_address is not None:
        args["state_watch_address"] = f"0x{state_watch_address:08x}"
        args["state_watch_trace_version"] = STATE_WATCH_TRACE_VERSION
    return args


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + DISCOVERY_RUN_FRAMES


def parse_range(value: str, option: str) -> tuple[int, int]:
    try:
        start_text, end_text = value.split(":", 1)
        start, end = int(start_text, 0), int(end_text, 0)
    except ValueError as error:
        raise ValueError(f"{option} must be START:END") from error
    if start >= end:
        raise ValueError(f"{option} must be non-empty")
    return start, end


def harness_environment(
    cpu_mode: str,
    linear_range: tuple[int, int],
    memory_log_path: Path,
    source_range: tuple[int, int] | None = None,
    watch_address: int | None = None,
    state_watch_address: int | None = None,
) -> dict[str, str]:
    if cpu_mode not in CPU_MODES:
        raise ValueError(f"unsupported CPU mode: {cpu_mode}")
    ranges = [linear_range]
    if source_range is not None:
        ranges.append(source_range)
    if watch_address is not None:
        ranges.append((watch_address, watch_address + 4))
    if state_watch_address is not None:
        ranges.append((state_watch_address, state_watch_address + 4))
    environment = {
        **os.environ,
        "SOH3D_HARNESS_DISABLE_FASTMEM": "1",
        "SOH3D_MEMLOG_RANGES": ",".join(f"0x{start:08x}:0x{end:08x}" for start, end in ranges),
        "SOH3D_MEMLOG_PATH": str(memory_log_path),
    }
    if cpu_mode == "interpreter":
        environment["SOH3D_CPU_INTERPRETER"] = "1"
    return environment


def linear_virtual_address(physical_address: int, word_index: int) -> int:
    if physical_address < FCRAM_PHYSICAL_BASE:
        raise ValueError(f"PICA command list 0x{physical_address:08x} is outside FCRAM")
    return LINEAR_HEAP_VIRTUAL_BASE + physical_address - FCRAM_PHYSICAL_BASE + word_index * 4


def parse_memlog(path: Path, target_address: int) -> list[str]:
    if not path.is_file():
        raise RuntimeError("oracle command-buffer memory log is missing")
    records = []
    for line in path.read_text().splitlines():
        match = MEMLOG_RE.match(line)
        if match is not None:
            address = int(match.group("address"), 16)
            size = int(match.group("size"))
            if address <= target_address < address + size:
                records.append(line)
    if not records:
        raise RuntimeError(f"memory log has no exact writer for 0x{target_address:08x}")
    return records


def persist_selected_memlog(
    path: Path, *record_groups: list[str], watch_address: int | None = None, watch_count: int | None = None
) -> None:
    records: list[str] = []
    for group in record_groups:
        records.extend(record for record in group if record not in records)
    if not records:
        raise RuntimeError("no memory-log records selected for cache")
    lines = ["# Exact writer records selected from the oracle memory log."]
    if watch_address is not None and watch_count is not None:
        lines.append(f"# Watch 0x{watch_address:08x}: {watch_count} matching record(s).")
    lines.extend(records)
    path.write_text("\n".join(lines) + "\n")


def memlog_fields(record: str) -> dict[str, int]:
    return {match.group("name"): int(match.group("value"), 16) for match in MEMLOG_FIELD_RE.finditer(record)}


def selected_writer_record(records: list[str], command_value: int) -> dict[str, int]:
    matching = [memlog_fields(record) for record in command_value_records(records, command_value)]
    if not matching:
        raise RuntimeError(f"memory log has no writer record for command value 0x{command_value:08x}")
    descriptors = {fields.get("r4") for fields in matching}
    if None in descriptors:
        raise RuntimeError("memory log does not include the packet descriptor register")
    if len(descriptors) != 1:
        raise RuntimeError(f"command value 0x{command_value:08x} has multiple packet descriptors: {descriptors}")
    return matching[0]


def command_value_records(records: list[str], command_value: int) -> list[str]:
    return [record for record in records if memlog_fields(record).get("data") == command_value]


def copy_source_value_address(writer: dict[str, int], command_value: int) -> int:
    required = {"pc", "r1", *COPY_SOURCE_REGISTERS}
    missing = sorted(required.difference(writer))
    if missing:
        raise RuntimeError(f"memory log lacks copy-source registers: {', '.join(missing)}")
    if writer["pc"] != COPY_LOOP_PC:
        raise RuntimeError(f"selected writer is not copy loop 0x{COPY_LOOP_PC:08x}")
    matching = [index for index, register in enumerate(COPY_SOURCE_REGISTERS) if writer[register] == command_value]
    if len(matching) != 1:
        raise RuntimeError(
            f"copy loop has {len(matching)} source registers for command value 0x{command_value:08x}; refusing guess"
        )
    # At the second `stmia` of `FUN_00371758`, two preceding `ldmia r1!` instructions
    # have advanced r1 by 32 bytes; r7-r10 hold the final 16-byte source group.
    return writer["r1"] - 16 + matching[0] * 4


def snapshot_config_builder_input(record: dict[str, int]) -> dict[str, Any]:
    required = {"pc", "r10", "r10b", *CONFIG_BUILDER_INPUT_WORDS.values()}
    missing = sorted(required.difference(record))
    if missing:
        raise RuntimeError(f"memory log lacks config-builder fields: {', '.join(missing)}")
    if record["pc"] != CONFIG_TEMPLATE_STORE_PC:
        raise RuntimeError(f"selected writer is not config-template store 0x{CONFIG_TEMPLATE_STORE_PC:08x}")
    expected_base = record["r10"] - CONFIG_BUILDER_R10_OFFSET
    if record["r10b"] != expected_base:
        raise RuntimeError(
            f"config-builder input base 0x{record['r10b']:08x} does not match r10 - 0x{CONFIG_BUILDER_R10_OFFSET:x}"
        )
    return {
        "address": f"0x{record['r10b']:08x}",
        "words": {offset: f"0x{record[field]:08x}" for offset, field in CONFIG_BUILDER_INPUT_WORDS.items()},
    }


def snapshot_material_descriptor(record: dict[str, int]) -> dict[str, Any]:
    required = {"pc", "r1", *MATERIAL_DESCRIPTOR_WORDS.values()}
    missing = sorted(required.difference(record))
    if missing:
        raise RuntimeError(f"memory log lacks material-descriptor fields: {', '.join(missing)}")
    if record["pc"] != MATERIAL_DESCRIPTOR_BIND_PC:
        raise RuntimeError(f"selected writer is not material-descriptor bind 0x{MATERIAL_DESCRIPTOR_BIND_PC:08x}")
    return {
        "address": f"0x{record['r1']:08x}",
        "words": {offset: f"0x{record[field]:08x}" for offset, field in MATERIAL_DESCRIPTOR_WORDS.items()},
    }


def snapshot_owner_state(writer: dict[str, int]) -> dict[str, Any]:
    required = {
        "r0",
        "r4",
        "sr4",
        *OWNER_FIELD_OFFSETS.values(),
        *VIRTUAL_SLOT_FIELDS.values(),
        *PACKET_DESCRIPTOR_FIELDS.values(),
    }
    missing = sorted(required - writer.keys())
    if missing:
        raise RuntimeError(f"memory log lacks exact-store dispatcher fields: {', '.join(missing)}")
    owner = writer["sr4"]
    source_word = writer["r0"]
    return {
        "object_address": f"0x{owner:08x}",
        "packet_descriptor_address": f"0x{writer['r4']:08x}",
        "source_packet_word": f"0x{source_word:08x}",
        "source_packet_base": f"0x{source_word - 8:08x}",
        # `0x00466e60` stores the previous 16-byte group then post-increments r0;
        # the copied PICA value is the final word of that group.
        "source_value_address": f"0x{source_word - 4:08x}",
        "fields": {name: f"0x{writer[field]:08x}" for name, field in OWNER_FIELD_OFFSETS.items()},
        "packet_descriptor": {name: f"0x{writer[field]:08x}" for name, field in PACKET_DESCRIPTOR_FIELDS.items()},
        "virtual_slots": {name: f"0x{writer[field]:08x}" for name, field in VIRTUAL_SLOT_FIELDS.items()},
    }


def capture_live(
    cache: CacheLike,
    args: dict[str, Any],
    draw: int,
    register: int,
    entrance: int,
    daytime: int,
    settle_frames: int,
    linear_range: tuple[int, int],
    cpu_mode: str,
    source_range: tuple[int, int] | None,
    watch_address: int | None,
    state_watch_address: int | None,
) -> dict[str, Any]:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    discovery, _ = capture_provenance_probe(cache, draw, str(args["label"]), entrance, daytime, settle_frames)
    command_list_artifact = Path(discovery["command_list_artifact"])
    payload = command_list_artifact.read_bytes()
    word_index, command_value = last_register_write(payload, discovery["command_list_word_index"], register)
    writer_address = linear_virtual_address(discovery["command_list_address"], word_index)
    if not linear_range[0] <= writer_address < linear_range[1]:
        raise RuntimeError(
            f"command-list writer 0x{writer_address:08x} is outside the permitted memory-log range"
        )
    trace_range = (writer_address, writer_address + 4)
    discovery_path = OUTDIR / "trace-discovery.log"
    command_list_path = OUTDIR / "trace-command-list.bin"
    memlog_path = OUTDIR / "memory-writes.log"
    selected_memlog_path = OUTDIR / "selected-memory-writes.log"
    harness = spawn(
        environment=harness_environment(
            cpu_mode, trace_range, memlog_path, source_range, watch_address, state_watch_address
        )
    )
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
        trace_provenance = parse_provenance(discovery_path.read_text().splitlines(), draw)
        matching_command_shape = all(
            trace_provenance[key] == discovery[key]
            for key in ("draw", "command_list_word_index", "command_list_word_count")
        )
        if not matching_command_shape:
            raise RuntimeError("traced oracle frame changed the command-list shape; refusing writer trace")
        same_command_list_address = trace_provenance["command_list_address"] == discovery["command_list_address"]
        if not same_command_list_address and watch_address is None:
            raise RuntimeError(
                "traced oracle frame relocated the command list; an exact source-word watch is required"
            )
        list_bytes = trace_provenance["command_list_word_count"] * 4
        dump_response = harness.send_multiline(
            f"dumpphys 0x{trace_provenance['command_list_address']:08x} {list_bytes} {command_list_path}"
        )
        if len(dump_response) != 1 or not dump_response[0].startswith("ok dumpphys "):
            raise RuntimeError(f"oracle command-list dump failed: {dump_response}")
        trace_payload = command_list_path.read_bytes()
        if len(trace_payload) != list_bytes:
            raise RuntimeError(f"oracle command-list dump has {len(trace_payload)} bytes, expected {list_bytes}")
        trace_word_index, trace_command_value = last_register_write(
            trace_payload, trace_provenance["command_list_word_index"], register
        )
        if (trace_word_index, trace_command_value) != (word_index, command_value):
            raise RuntimeError("traced oracle frame changed the selected PICA register packet; refusing writer trace")
        writer_records: list[str] = []
        owner_state: dict[str, Any] | None = None
        packet_value_address: int | None = None
        if same_command_list_address:
            writer_records = parse_memlog(memlog_path, writer_address)
            selected_writer = selected_writer_record(writer_records, command_value)
            owner_state = snapshot_owner_state(selected_writer)
            packet_value_address = int(owner_state["source_value_address"], 16)
        template_value_address: int | None = None
        source_records: list[str] = []
        if source_range is not None:
            if packet_value_address is None:
                raise RuntimeError("command list relocated; a source range cannot identify the current packet word")
            source_records = parse_memlog(memlog_path, packet_value_address)
            template_value_address = copy_source_value_address(
                selected_writer_record(source_records, command_value), command_value
            )
        watch_records: list[str] = []
        watch_command_records: list[str] = []
        copy_watch_records: list[str] = []
        config_builder_input: dict[str, Any] | None = None
        material_descriptor: dict[str, Any] | None = None
        state_watch_records: list[str] = []
        if watch_address is not None:
            try:
                watch_records = parse_memlog(memlog_path, watch_address)
            except RuntimeError as error:
                if str(error) != f"memory log has no exact writer for 0x{watch_address:08x}":
                    raise
            watch_command_records = command_value_records(watch_records, command_value)
            copy_watch_records = [
                record
                for record in watch_command_records
                if (fields := memlog_fields(record)).get("pc") == COPY_LOOP_PC
                and fields.get("data") == command_value
            ]
            if copy_watch_records:
                watched_template_value_address = copy_source_value_address(
                    selected_writer_record(copy_watch_records, command_value), command_value
                )
                if template_value_address is not None and watched_template_value_address != template_value_address:
                    raise RuntimeError("source-range and exact-watch template addresses disagree")
                template_value_address = watched_template_value_address
            config_store_inputs = [
                snapshot_config_builder_input(memlog_fields(record))
                for record in watch_command_records
                if memlog_fields(record).get("pc") == CONFIG_TEMPLATE_STORE_PC
            ]
            distinct_config_store_inputs: list[dict[str, Any]] = []
            for input_state in config_store_inputs:
                if input_state not in distinct_config_store_inputs:
                    distinct_config_store_inputs.append(input_state)
            if len(distinct_config_store_inputs) > 1:
                raise RuntimeError("exact template watch produced multiple config-builder input states")
            if distinct_config_store_inputs:
                config_builder_input = distinct_config_store_inputs[0]
        if state_watch_address is not None:
            state_watch_records = parse_memlog(memlog_path, state_watch_address)
            descriptor_inputs = [
                snapshot_material_descriptor(memlog_fields(record))
                for record in state_watch_records
                if memlog_fields(record).get("pc") == MATERIAL_DESCRIPTOR_BIND_PC
            ]
            distinct_descriptors: list[dict[str, Any]] = []
            for descriptor in descriptor_inputs:
                if descriptor not in distinct_descriptors:
                    distinct_descriptors.append(descriptor)
            if len(distinct_descriptors) > 1:
                raise RuntimeError("state watch produced multiple material descriptors")
            if distinct_descriptors:
                material_descriptor = distinct_descriptors[0]
        persist_selected_memlog(
            selected_memlog_path,
            writer_records,
            source_records,
            watch_command_records,
            state_watch_records,
            watch_address=watch_address,
            watch_count=len(watch_records) if watch_address is not None else None,
        )
        memory_log_artifact = cache.put_artifact(
            "pica-command-writer-memory", args, selected_memlog_path, suffix=".log"
        )
        if source_range is not None and not any(
            memlog_fields(record).get("data") == command_value for record in source_records
        ):
            raise RuntimeError(
                f"memory log has no source write for command value 0x{command_value:08x}; "
                f"artifact: {memory_log_artifact}"
            )
        return {
            "capture_version": CAPTURE_VERSION,
            "draw": draw,
            "register": f"0x{register:03x}",
            "command_value": f"0x{command_value:08x}",
            "command_list_address": f"0x{discovery['command_list_address']:08x}",
            "command_list_word_index": word_index,
            "writer_address": f"0x{writer_address:08x}",
            "writer_records": writer_records,
            "writer_owner": owner_state,
            "packet_value_address": f"0x{packet_value_address:08x}" if packet_value_address is not None else None,
            "watched_packet_value_address": f"0x{watch_address:08x}" if watch_address is not None else None,
            "template_value_address": (
                f"0x{template_value_address:08x}" if template_value_address is not None else None
            ),
            "source_writer_records": source_records,
            "watch_address": f"0x{watch_address:08x}" if watch_address is not None else None,
            "watch_write_count": len(watch_records) if watch_address is not None else None,
            "watch_command_match_count": len(watch_command_records) if watch_address is not None else None,
            "watch_command_records": watch_command_records,
            "copy_source_match_count": len(copy_watch_records) if watch_address is not None else None,
            "config_builder_input": config_builder_input,
            "material_descriptor": material_descriptor,
            "state_watch_address": f"0x{state_watch_address:08x}" if state_watch_address is not None else None,
            "state_watch_write_count": len(state_watch_records) if state_watch_address is not None else None,
            "state_watch_records": state_watch_records,
            "discovery_artifact": str(discovery["artifact"]),
            "command_list_artifact": str(command_list_artifact),
            "memory_log_artifact": str(memory_log_artifact),
        }
    finally:
        harness.close()
        discovery_path.unlink(missing_ok=True)
        command_list_path.unlink(missing_ok=True)
        memlog_path.unlink(missing_ok=True)
        selected_memlog_path.unlink(missing_ok=True)


def capture_probe(
    cache: CacheLike,
    draw: int,
    register: int,
    label: str,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
    linear_range: tuple[int, int] = DEFAULT_LINEAR_RANGE,
    cpu_mode: str = "dynarmic",
    source_range: tuple[int, int] | None = None,
    watch_address: int | None = None,
    state_watch_address: int | None = None,
) -> tuple[dict[str, Any], bool]:
    args = probe_args(
        draw,
        register,
        label,
        entrance,
        daytime,
        settle_frames,
        linear_range,
        cpu_mode,
        source_range,
        watch_address,
        state_watch_address,
    )
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("pica-command-writer", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("pica-command-writer-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(
            cache,
            args,
            draw,
            register,
            entrance,
            daytime,
            settle_frames,
            linear_range,
            cpu_mode,
            source_range,
            watch_address,
            state_watch_address,
        )
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
    parser.add_argument("--linear-range", default="0x14480000:0x145a0000")
    parser.add_argument("--source-range", help="optional packet-source VA range to trace")
    parser.add_argument("--watch-address", type=lambda value: int(value, 0), help="optional exact VA write watch")
    parser.add_argument(
        "--state-watch-address",
        type=lambda value: int(value, 0),
        help="optional exact live renderer-state VA write watch",
    )
    parser.add_argument("--cpu-mode", choices=CPU_MODES, default="dynarmic")
    args = parser.parse_args(arguments)
    if args.draw < 0:
        parser.error("--draw must be non-negative")
    if not 0 <= args.register <= 0x2FF:
        parser.error("--register must be a PICA register index")
    if args.settle_frames < 0:
        parser.error("--settle-frames must be non-negative")
    try:
        linear_range = parse_range(args.linear_range, "--linear-range")
        source_range = parse_range(args.source_range, "--source-range") if args.source_range else None
    except ValueError as error:
        parser.error(str(error))
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
            linear_range,
            args.cpu_mode,
            source_range,
            args.watch_address,
            args.state_watch_address,
        )
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
