#!/usr/bin/env python3
"""Cache-owned raw-texture identity probe for a visible enabled CMB source.

On a cache miss this runs one deterministic oracle fixture, records the PICA
draw log, dumps the bytes for only source-descriptor-compatible texture slots,
and associates a draw only through an exact raw payload match.  Successes and
failures are both cached, so repeating the command never starts Azahar.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Protocol

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from cmb_texture_draw_identity import (
    descriptor_candidates,
    logged_texture_descriptors,
    match_guest_payloads,
    source_textures,
)
from harness_cache import OracleCache
from harness_gameplay import boot_to_gameplay, set_time_of_day
from harness_paths import GAMEPLAY_STATE
from harness_process import spawn
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 3
DEFAULT_ARCHIVE = "/actor/zelda_wood02.zar"
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
DISCOVERY_RUN_FRAMES = 2
OUTDIR = REPO / "scratch" / "cmb_texture_draw_identity_oracle"


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> Any | None: ...

    def put_probe(
        self, name: str, frame: int, args: dict[str, Any], data: Any
    ) -> None: ...

    def put_artifact(
        self,
        name: str,
        args: dict[str, Any],
        source: Path,
        suffix: str | None = None,
    ) -> Path: ...


class OracleObservationFailure(RuntimeError):
    """A complete oracle observation whose negative result is cacheable."""


def probe_args(
    archive: str, source_mode: str, entrance: int, daytime: int, settle_frames: int
) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "archive": archive,
        "source_mode": source_mode,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "discovery_run_frames": DISCOVERY_RUN_FRAMES,
        "texture_pack": 0,
    }


def capture_frame(settle_frames: int) -> int:
    return settle_frames + TIME_SETTLE_FRAMES + DISCOVERY_RUN_FRAMES


def _read_draw_lines(path: Path) -> list[str]:
    if not path.is_file():
        raise RuntimeError(f"oracle PICA log is missing: {path}")
    return [line.strip() for line in path.read_text().splitlines() if line.startswith("draw ")]


def _read_guest_memory(harness: Any, address: int, size: int, destination: Path) -> bytes:
    response = harness.send_multiline(f"dumpphys 0x{address:08x} {size} {destination}")
    if len(response) != 1 or not response[0].startswith("ok dumpphys "):
        raise RuntimeError(f"oracle physical-memory read at 0x{address:08x} failed: {response}")
    if not destination.is_file():
        raise RuntimeError(f"oracle physical-memory read at 0x{address:08x} created no dump")
    payload = destination.read_bytes()
    if len(payload) != size:
        raise RuntimeError(
            f"oracle physical-memory read at 0x{address:08x} returned {len(payload)} bytes, expected {size}"
        )
    return payload


def _capture_live(
    cache: CacheLike,
    args: dict[str, Any],
    archive: str,
    source_mode: str,
    entrance: int,
    daytime: int,
    settle_frames: int,
) -> dict[str, Any]:
    sources = source_textures(
        archive, require_enabled_fragment_primary=source_mode == "enabled-fragment-primary"
    )
    source_description = (
        "enabled fragment-primary" if source_mode == "enabled-fragment-primary" else "archive"
    )
    source_sizes = {
        (source.width, source.height, source.pica_format): len(source.payload)
        for source in sources
    }
    OUTDIR.mkdir(parents=True, exist_ok=True)
    discovery_path = OUTDIR / "discovery.log"
    memory_path = OUTDIR / "guest-textures.json"
    harness = spawn()
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        response = harness.send(f"vsuni_log {discovery_path}")
        if response != f"ok vsuni_log {discovery_path}":
            raise RuntimeError(f"oracle PICA logger failed: {response}")
        harness.send(f"run {DISCOVERY_RUN_FRAMES}")
        harness.send("vsuni_log off")
        lines = _read_draw_lines(discovery_path)
        logged = logged_texture_descriptors(lines)
        candidates = descriptor_candidates(logged, sources)
        if not candidates:
            artifact = cache.put_artifact(
                "cmb-texture-draw-identity-discovery", args, discovery_path, suffix=".log"
            )
            raise OracleObservationFailure(
                f"oracle draw log scanned {len(logged)} texture descriptors and matched 0 "
                f"source descriptors from {len(sources)} {source_description} textures; "
                f"cached diagnostic: {artifact}"
            )
        guest_payloads: dict[int, bytes] = {}
        raw_records: list[dict[str, Any]] = []
        for candidate in candidates:
            size = source_sizes[(candidate.width, candidate.height, candidate.pica_format)]
            payload = guest_payloads.get(candidate.address)
            if payload is None:
                dump_path = OUTDIR / f"guest-{candidate.address:08x}.bin"
                payload = _read_guest_memory(harness, candidate.address, size, dump_path)
                guest_payloads[candidate.address] = payload
            raw_records.append(
                {
                    "draw": candidate.draw,
                    "address": f"0x{candidate.address:08x}",
                    "width": candidate.width,
                    "height": candidate.height,
                    "pica_format": candidate.pica_format,
                    "payload_hex": payload.hex(),
                }
            )
        memory_path.write_text(json.dumps(raw_records, indent=2, sort_keys=True))
        discovery_artifact = cache.put_artifact(
            "cmb-texture-draw-identity-discovery", args, discovery_path, suffix=".log"
        )
        memory_artifact = cache.put_artifact(
            "cmb-texture-draw-identity-guest-bytes", args, memory_path, suffix=".json"
        )
        matches = match_guest_payloads(candidates, sources, guest_payloads)
        if not matches:
            raise OracleObservationFailure(
                f"oracle draw log scanned {len(logged)} texture descriptors, matched "
                f"{len(candidates)} source-compatible descriptors, read "
                f"{len(guest_payloads)} unique guest textures, and matched 0 exact CMB payloads; "
                f"cached diagnostics: {discovery_artifact}, {memory_artifact}"
            )
        return {
            "capture_version": CAPTURE_VERSION,
            "archive": archive,
            "source_texture_count": len(sources),
            "logged_texture_count": len(logged),
            "candidate_count": len(candidates),
            "guest_texture_count": len(guest_payloads),
            "discovery_artifact": str(discovery_artifact),
            "guest_bytes_artifact": str(memory_artifact),
            "matches": [
                {
                    "draw": candidate.draw,
                    "address": f"0x{candidate.address:08x}",
                    "source": source.label,
                    "texture": source.texture_name,
                    "sha256": source.digest,
                }
                for candidate, source in matches
            ],
        }
    finally:
        harness.close()
        discovery_path.unlink(missing_ok=True)
        memory_path.unlink(missing_ok=True)
        for dump_path in OUTDIR.glob("guest-*.bin"):
            dump_path.unlink(missing_ok=True)


def capture_probe(
    cache: CacheLike,
    archive: str = DEFAULT_ARCHIVE,
    source_mode: str = "enabled-fragment-primary",
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
) -> tuple[dict[str, Any], bool]:
    if source_mode not in {"enabled-fragment-primary", "any"}:
        raise ValueError(f"unsupported source mode: {source_mode}")
    args = probe_args(archive, source_mode, entrance, daytime, settle_frames)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("cmb-texture-draw-identity", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("cmb-texture-draw-identity-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = _capture_live(
            cache, args, archive, source_mode, entrance, daytime, settle_frames
        )
    except OracleObservationFailure as error:
        cache.put_probe(
            "cmb-texture-draw-identity-failure",
            frame,
            args,
            {"capture_version": CAPTURE_VERSION, "error": str(error)},
        )
        raise
    cache.put_probe("cmb-texture-draw-identity", frame, args, result)
    return result, False


def cache_context() -> OracleCache:
    apply_repo_environment(REPO, os.environ)
    os.environ["ZELDA3D_HARNESS_TEXPACK"] = "off"
    return OracleCache(GAMEPLAY_STATE)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", default=DEFAULT_ARCHIVE)
    parser.add_argument(
        "--source-mode", choices=("enabled-fragment-primary", "any"),
        default="enabled-fragment-primary",
    )
    parser.add_argument("--entrance", type=lambda value: int(value, 0), default=DEFAULT_ENTRANCE)
    parser.add_argument("--daytime", type=lambda value: int(value, 0), default=DEFAULT_DAYTIME)
    parser.add_argument("--settle-frames", type=int, default=DEFAULT_SETTLE_FRAMES)
    options = parser.parse_args(arguments)
    try:
        result, hit = capture_probe(
            cache_context(),
            options.archive,
            options.source_mode,
            options.entrance,
            options.daytime,
            options.settle_frames,
        )
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    print(json.dumps({"cache_hit": hit, **result}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
