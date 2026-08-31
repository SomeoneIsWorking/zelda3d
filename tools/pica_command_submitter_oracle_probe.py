#!/usr/bin/env python3
"""Cache-owned GSP submitter provenance for one deterministic PICA draw."""

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
from pica_command_provenance_oracle_probe import (
    DISCOVERY_RUN_FRAMES,
    TIME_SETTLE_FRAMES,
    parse_provenance,
)
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 7
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
OUTDIR = REPO / "scratch" / "pica_command_submitter"
POINTER_RANGE = (0x14480000, 0x145A0000)

SUBMIT_RE = re.compile(
    r"^CMDSUBMIT source=(?P<source>GSP|MMIO) pc=0x(?P<pc>[0-9a-fA-F]{8}) lr=0x(?P<lr>[0-9a-fA-F]{8}) "
    r"listVa=0x(?P<virtual_address>[0-9a-fA-F]{8}) listPa=0x(?P<physical_address>[0-9a-fA-F]{8}) "
    r"size=(?P<size>\d+) mmio=0x(?P<mmio_address>[0-9a-fA-F]{8}) r0=0x(?P<r0>[0-9a-fA-F]{8}) r1=0x(?P<r1>[0-9a-fA-F]{8}) "
    r"r2=0x(?P<r2>[0-9a-fA-F]{8}) r3=0x(?P<r3>[0-9a-fA-F]{8}) sp=0x(?P<sp>[0-9a-fA-F]{8}) "
    r"s0=0x(?P<s0>[0-9a-fA-F]{8}) s1=0x(?P<s1>[0-9a-fA-F]{8}) s2=0x(?P<s2>[0-9a-fA-F]{8}) "
    r"s3=0x(?P<s3>[0-9a-fA-F]{8}) s4=0x(?P<s4>[0-9a-fA-F]{8}) s5=0x(?P<s5>[0-9a-fA-F]{8}) "
    r"s6=0x(?P<s6>[0-9a-fA-F]{8}) s7=0x(?P<s7>[0-9a-fA-F]{8}) s8=0x(?P<s8>[0-9a-fA-F]{8}) "
    r"s9=0x(?P<s9>[0-9a-fA-F]{8}) s10=0x(?P<s10>[0-9a-fA-F]{8}) s11=0x(?P<s11>[0-9a-fA-F]{8}) "
    r"s12=0x(?P<s12>[0-9a-fA-F]{8}) s13=0x(?P<s13>[0-9a-fA-F]{8}) s14=0x(?P<s14>[0-9a-fA-F]{8}) "
    r"s15=0x(?P<s15>[0-9a-fA-F]{8}) s16=0x(?P<s16>[0-9a-fA-F]{8})$"
)
POINTER_RE = re.compile(
    r"^PTR pc=0x(?P<pc>[0-9a-fA-F]{8}) lr=0x(?P<lr>[0-9a-fA-F]{8}) "
    r"va=0x(?P<virtual_address>[0-9a-fA-F]{8}) r0=0x(?P<r0>[0-9a-fA-F]{8}) "
    r"r1=0x(?P<r1>[0-9a-fA-F]{8}) r2=0x(?P<r2>[0-9a-fA-F]{8}) "
    r"r3=0x(?P<r3>[0-9a-fA-F]{8}) sp=0x(?P<sp>[0-9a-fA-F]{8})$"
)
BULK_RE = re.compile(
    r"^MB pc=0x(?P<pc>[0-9a-fA-F]{8}) lr=0x(?P<lr>[0-9a-fA-F]{8}) "
    r"va=0x(?P<virtual_address>[0-9a-fA-F]{8}) sz=(?P<size>\d+) "
    r"r0=0x(?P<r0>[0-9a-fA-F]{8}) r1=0x(?P<r1>[0-9a-fA-F]{8}) "
    r"r2=0x(?P<r2>[0-9a-fA-F]{8}) r3=0x(?P<r3>[0-9a-fA-F]{8}) "
    r"sp=0x(?P<sp>[0-9a-fA-F]{8})$"
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
        "pointer_range": [f"0x{POINTER_RANGE[0]:08x}", f"0x{POINTER_RANGE[1]:08x}"],
    }


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + DISCOVERY_RUN_FRAMES


def parse_submit_records(lines: list[str]) -> list[dict[str, int | str]]:
    records = []
    for raw_line in lines:
        for line in raw_line.replace("\\n", "\n").splitlines():
            match = SUBMIT_RE.match(line)
            if match is not None:
                records.append(
                    {
                        name: value
                        if name == "source"
                        else int(value, 16)
                        if name != "size"
                        else int(value)
                        for name, value in match.groupdict().items()
                    }
                )
    return records


def parse_pointer_records(lines: list[str], virtual_address: int) -> list[dict[str, int]]:
    records = []
    for line in lines:
        match = POINTER_RE.match(line)
        if match is not None and int(match.group("virtual_address"), 16) == virtual_address:
            records.append({name: int(value, 16) for name, value in match.groupdict().items()})
    return records


def parse_bulk_records(lines: list[str], start: int, end: int) -> list[dict[str, int]]:
    records = []
    for line in lines:
        match = BULK_RE.match(line)
        if match is None:
            continue
        record = {
            name: int(value) if name == "size" else int(value, 16)
            for name, value in match.groupdict().items()
        }
        if record["virtual_address"] < end and record["virtual_address"] + record["size"] > start:
            records.append(record)
    return records


def parse_bulk_selftest_records(
    before_lines: list[str], after_lines: list[str], virtual_address: int
) -> list[dict[str, int]]:
    if after_lines[: len(before_lines)] != before_lines:
        raise RuntimeError("bulk-write log changed before the self-test boundary")
    records = parse_bulk_records(
        after_lines[len(before_lines) :], virtual_address, virtual_address + 16
    )
    if not records:
        raise RuntimeError("WriteBlock bulk logger self-test produced no matching record")
    return records


def match_submit_record(
    records: list[dict[str, int | str]], physical_address: int, size: int
) -> dict[str, int | str]:
    matches = [
        record
        for record in records
        if record["physical_address"] == physical_address and record["size"] == size
    ]
    if not matches:
        raise RuntimeError(
            f"GSP submit log has no {size}-byte record for command list 0x{physical_address:08x}"
        )
    distinct = {tuple(sorted(record.items())) for record in matches}
    if len(distinct) != 1:
        raise RuntimeError(
            f"GSP submit log has {len(distinct)} distinct records for command list 0x{physical_address:08x}"
        )
    return matches[-1]


def result_from_logs(
    discovery_lines: list[str],
    submit_lines: list[str],
    draw: int,
    discovery_artifact: str,
    submit_artifact: str,
) -> dict[str, Any]:
    provenance = parse_provenance(discovery_lines, draw)
    submitter = match_submit_record(
        parse_submit_records(submit_lines),
        provenance["command_list_address"],
        provenance["command_list_word_count"] * 4,
    )
    return {
        "capture_version": CAPTURE_VERSION,
        "draw": draw,
        "command_list_address": f"0x{provenance['command_list_address']:08x}",
        "command_list_word_index": provenance["command_list_word_index"],
        "command_list_word_count": provenance["command_list_word_count"],
        "submitter": {
            name: value if name == "source" else f"0x{value:08x}"
            for name, value in submitter.items()
            if name not in {"size", "source"}
        }
        | {"source": submitter["source"], "size": submitter["size"]},
        "discovery_artifact": discovery_artifact,
        "submit_artifact": submit_artifact,
    }


def capture_live(
    cache: CacheLike, args: dict[str, Any], draw: int, entrance: int, daytime: int, settle_frames: int
) -> dict[str, Any]:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    discovery_path = OUTDIR / "discovery.log"
    submit_path = OUTDIR / "gsp-submit.log"
    pointer_path = OUTDIR / "pointer.log"
    bulk_path = OUTDIR / "bulk-write.log"
    environment = {
        **os.environ,
        "SOH3D_HARNESS_LOG_GSP_SUBMIT": str(submit_path),
        "SOH3D_PTRLOG_RANGE": f"0x{POINTER_RANGE[0]:08x}:0x{POINTER_RANGE[1]:08x}",
        "SOH3D_PTRLOG_PATH": str(pointer_path),
        "SOH3D_MEMLOG_RANGES": f"0x{POINTER_RANGE[0]:08x}:0x{POINTER_RANGE[1]:08x}",
        "SOH3D_MEMLOG_PATH": str(bulk_path),
    }
    harness = spawn(environment=environment)
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
        if not submit_path.is_file():
            raise RuntimeError("oracle GSP submit logger produced no log")
        if not pointer_path.is_file():
            raise RuntimeError("oracle pointer logger produced no log")
        if not bulk_path.is_file():
            raise RuntimeError("oracle bulk-write logger produced no log")
        discovery_artifact = cache.put_artifact(
            "pica-command-submitter-discovery", args, discovery_path, suffix=".log"
        )
        submit_artifact = cache.put_artifact(
            "pica-command-submitter-gsp", args, submit_path, suffix=".log"
        )
        pointer_artifact = cache.put_artifact(
            "pica-command-submitter-pointer", args, pointer_path, suffix=".log"
        )
        result = result_from_logs(
            discovery_path.read_text().splitlines(),
            submit_path.read_text().splitlines(),
            draw,
            str(discovery_artifact),
            str(submit_artifact),
        )
        result["pointer_artifact"] = str(pointer_artifact)
        bulk_lines = bulk_path.read_text().splitlines()
        bulk_artifact = cache.put_artifact(
            "pica-command-submitter-bulk", args, bulk_path, suffix=".log"
        )
        result["bulk_artifact"] = str(bulk_artifact)
        result["pointer_records"] = parse_pointer_records(
            pointer_path.read_text().splitlines(), int(result["submitter"]["virtual_address"], 16)
        )
        list_start = int(result["submitter"]["virtual_address"], 16)
        result["bulk_records"] = parse_bulk_records(
            bulk_lines,
            list_start,
            list_start + result["command_list_word_count"] * 4,
        )
        response = harness.send(f"memlogselftest 0x{list_start:08x}")
        if response != f"ok memlogselftest va=0x{list_start:08x} size=16":
            raise RuntimeError(f"WriteBlock bulk logger self-test failed: {response}")
        selftest_lines = bulk_path.read_text().splitlines()
        result["bulk_selftest_records"] = parse_bulk_selftest_records(
            bulk_lines, selftest_lines, list_start
        )
        bulk_selftest_artifact = cache.put_artifact(
            "pica-command-submitter-bulk-selftest", args, bulk_path, suffix=".log"
        )
        result["bulk_selftest_artifact"] = str(bulk_selftest_artifact)
        return result
    finally:
        harness.close()
        discovery_path.unlink(missing_ok=True)
        submit_path.unlink(missing_ok=True)
        pointer_path.unlink(missing_ok=True)
        bulk_path.unlink(missing_ok=True)


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
    cached = cache.get_probe("pica-command-submitter", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("pica-command-submitter-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = capture_live(cache, args, draw, entrance, daytime, settle_frames)
    except (OSError, RuntimeError, ValueError) as error:
        cache.put_probe(
            "pica-command-submitter-failure",
            frame,
            args,
            {"capture_version": CAPTURE_VERSION, "error": str(error)},
        )
        raise
    cache.put_probe("pica-command-submitter", frame, args, result)
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
