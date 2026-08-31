#!/usr/bin/env python3
"""Capture one cache-owned PICA fragment-lighting draw from the embedded oracle.

The probe performs no oracle work on a cache hit. On a miss it uses one process:
one frame discovers fragment-lit draw IDs, then one frame captures the selected
draw's raw PICA config, light records, and only its activated lighting LUTs.
"""

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
from harness_gameplay import BTN_START, boot_to_gameplay, set_time_of_day, tap
from harness_paths import GAMEPLAY_STATE
from harness_process import spawn
from repo_environment import apply_repo_environment

CAPTURE_VERSION = 11
# `FUN_003f9b5c` is the candidate CmbRenderer's material-setup vtable slot.
# Watching it distinguishes "the candidate renderer was not used" from "its
# optional fragment-light branch was not used" in the same cached frame.
MATERIAL_SETUP_FUNCTION = 0x003F9B5C
# Start reaches OoT3D's Save overlay from the cached Kokiri gameplay state. It
# is a stable, cacheable negative control for the PICA state logger; it does
# not claim to exercise the separately-authored pause Link model.
DEFAULT_FIXTURE = "kokiri-save-overlay"
DEFAULT_ENTRANCE = 0xEE
DEFAULT_DAYTIME = 0x6000
DEFAULT_SETTLE_FRAMES = 180
TIME_SETTLE_FRAMES = 8
DISCOVERY_RUN_FRAMES = 2
VALIDATION_RUN_FRAMES = 2
SELFTEST_DRAW = 0
OUTDIR = REPO / "scratch" / "cmb_fragment_lighting_oracle"

DRAW_RE = re.compile(r"^draw n=(?P<draw>\d+) .*")
PICA_LIT_RE = re.compile(r"\bpicaLit=1\b")
PC_HITS_RE = re.compile(r"^ok pchits (?P<count>\d+)$")


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


def probe_args(
    entrance: int, daytime: int, settle_frames: int, fixture: str
) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "entrance": entrance,
        "daytime": daytime,
        "settle_frames": settle_frames,
        "time_settle_frames": TIME_SETTLE_FRAMES,
        "discovery_run_frames": DISCOVERY_RUN_FRAMES,
        "validation_run_frames": VALIDATION_RUN_FRAMES,
        "fixture": fixture,
        "pc_watch_function": MATERIAL_SETUP_FUNCTION,
        "texture_pack": 0,
    }


def capture_frame(settle_frames: int) -> int:
    """Return the deterministic post-warp frame identity used by OracleCache."""
    return (
        settle_frames
        + TIME_SETTLE_FRAMES
        + DISCOVERY_RUN_FRAMES
        + VALIDATION_RUN_FRAMES
    )


def choose_enabled_lighting_draw(lines: list[str]) -> tuple[int, str]:
    draw_lines = [line for line in lines if DRAW_RE.match(line) is not None]
    if not draw_lines:
        raise RuntimeError("oracle discovery scanned 0 draws; capture is invalid")
    candidates: list[tuple[int, str]] = []
    for line in draw_lines:
        match = DRAW_RE.match(line)
        if match is not None and PICA_LIT_RE.search(line):
            candidates.append((int(match.group("draw")), line))
    if not candidates:
        raise RuntimeError(
            f"oracle discovery scanned {len(draw_lines)} draws and matched 0 picaLit=1 draws"
        )
    return min(candidates, key=lambda candidate: candidate[0])


def prepare_fixture(harness: Any, fixture: str) -> None:
    """Drive the named, asset-grounded fixture before arming capture."""
    if fixture in {"gameplay", "kokiri-gameplay"}:
        return
    if fixture == "kokiri-save-overlay":
        tap(harness, BTN_START, hold=4, release=60)
        return
    raise ValueError(f"unsupported fragment-lighting fixture: {fixture}")


def _read_uniform_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def require_lighting_log_positive(lines: list[str]) -> None:
    """Reject a PICA-state logger that has not shown its enabled branch."""
    draw_count = sum(DRAW_RE.match(line) is not None for line in lines)
    if draw_count == 0:
        raise RuntimeError("PICA-lighting logger self-test scanned 0 draws")
    if not any(PICA_LIT_RE.search(line) for line in lines):
        raise RuntimeError(
            f"PICA-lighting logger self-test scanned {draw_count} draws and logged no picaLit=1"
        )


def _draw_line(lines: list[str], draw: int) -> str | None:
    for line in lines:
        match = DRAW_RE.match(line)
        if match is not None and int(match.group("draw")) == draw:
            return line
    return None


def cache_lighting_state(
    cache: CacheLike, args: dict[str, Any], lighting_path: Path, draw: int
) -> tuple[dict[str, Any], Path]:
    """Persist raw PICA state before enforcing the expected enabled-LUT shape."""
    lighting = json.loads(lighting_path.read_text())
    artifact = cache.put_artifact(
        "cmb-fragment-lighting-state", args, lighting_path, suffix=".json"
    )
    if lighting.get("draw") != draw or lighting.get("disable") != 0:
        raise RuntimeError(
            f"oracle lighting capture is not the selected enabled draw; cached state: {artifact}"
        )
    if not lighting.get("luts"):
        raise RuntimeError(f"oracle enabled draw capture contains no activated LUT; cached state: {artifact}")
    return lighting, artifact


def _parse_pc_hits(lines: list[str]) -> tuple[int, list[str]]:
    if not lines:
        raise RuntimeError("oracle pcwatch returned no response")
    match = PC_HITS_RE.match(lines[0])
    if match is None or lines[-1] != "ok end":
        raise RuntimeError(f"oracle pcwatch returned malformed response: {lines}")
    count = int(match.group("count"))
    records = lines[1:-1]
    if len(records) != count:
        raise RuntimeError(
            f"oracle pcwatch count mismatch: header={count} records={len(records)}"
        )
    return count, records


def _capture_live(
    cache: CacheLike,
    args: dict[str, Any],
    entrance: int,
    daytime: int,
    settle_frames: int,
    fixture: str,
) -> dict[str, Any]:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    discovery_path = OUTDIR / "discovery.log"
    validation_path = OUTDIR / "validation.log"
    lighting_path = OUTDIR / "lighting.json"
    selftest_path = OUTDIR / "lighting-selftest.log"
    fixture_base = OUTDIR / "fixture"
    fixture_path = OUTDIR / "fixture.az.ppm"

    harness = spawn()
    try:
        if not boot_to_gameplay(harness, entrance, settle_frames):
            raise RuntimeError("oracle failed to reach deterministic gameplay state")
        set_time_of_day(harness, daytime, settle=TIME_SETTLE_FRAMES)
        prepare_fixture(harness, fixture)
        snapshot_response = harness.send_multiline(f"snapshot {fixture_base}")
        if not snapshot_response or snapshot_response[0] != "ok snapshot":
            raise RuntimeError(
                f"oracle fixture snapshot failed: {snapshot_response}"
            )
        if not fixture_path.is_file():
            raise RuntimeError(f"oracle fixture snapshot is missing: {fixture_path}")
        fixture_artifact = cache.put_artifact(
            "cmb-fragment-lighting-fixture", args, fixture_path, suffix=".ppm"
        )

        response = harness.send(f"vsuni_log {selftest_path}")
        if response != f"ok vsuni_log {selftest_path}":
            raise RuntimeError(f"oracle PICA-lighting self-test logger failed: {response}")
        response = harness.send(f"lighting_selftest {SELFTEST_DRAW}")
        if response != f"ok lighting_selftest {SELFTEST_DRAW}":
            raise RuntimeError(f"oracle PICA-lighting self-test arming failed: {response}")
        harness.send(f"run {DISCOVERY_RUN_FRAMES}")
        harness.send("vsuni_log off")
        require_lighting_log_positive(_read_uniform_lines(selftest_path))
        selftest_artifact = cache.put_artifact(
            "cmb-fragment-lighting-logger-selftest", args, selftest_path, suffix=".log"
        )

        response = harness.send(f"pcwatch 0x{MATERIAL_SETUP_FUNCTION:08x}")
        if response != f"ok pcwatch 0x{MATERIAL_SETUP_FUNCTION:08x}":
            raise RuntimeError(f"oracle guest-PC watch failed: {response}")

        response = harness.send(f"vsuni_log {discovery_path}")
        if response != f"ok vsuni_log {discovery_path}":
            raise RuntimeError(f"oracle discovery logger failed: {response}")
        harness.send(f"run {DISCOVERY_RUN_FRAMES}")
        harness.send("vsuni_log off")
        pc_hit_count, pc_hit_records = _parse_pc_hits(
            harness.send_multiline("pchits")
        )
        harness.send("pcwatch off")
        discovery_lines = _read_uniform_lines(discovery_path)
        try:
            draw, discovery_line = choose_enabled_lighting_draw(discovery_lines)
        except RuntimeError as error:
            failed = cache.put_artifact(
                "cmb-fragment-lighting-failed-discovery",
                args,
                discovery_path,
                suffix=".log",
            )
            raise RuntimeError(
                f"FUN_{MATERIAL_SETUP_FUNCTION:08x} material-setup hits={pc_hit_count} "
                f"after {error}; "
                f"records={pc_hit_records}; cached fixture: {fixture_artifact}; "
                f"cached diagnostic: {failed}"
            ) from error

        response = harness.send(f"lighting_capture {draw} {lighting_path}")
        if response != f"ok lighting_capture {draw} {lighting_path}":
            raise RuntimeError(f"oracle lighting capture failed to arm: {response}")
        response = harness.send(f"vsuni_log {validation_path}")
        if response != f"ok vsuni_log {validation_path}":
            raise RuntimeError(f"oracle validation logger failed: {response}")
        harness.send(f"run {VALIDATION_RUN_FRAMES}")
        harness.send("vsuni_log off")

        selected_line = _draw_line(_read_uniform_lines(validation_path), draw)
        validation_draw, validation_line = choose_enabled_lighting_draw(
            [] if selected_line is None else [selected_line]
        )
        if validation_draw != draw:
            raise RuntimeError(
                f"oracle draw identity changed between discovery and capture: "
                f"{draw} -> {validation_draw}"
            )
        if not lighting_path.is_file():
            raise RuntimeError(f"oracle did not produce lighting capture for draw {draw}")

        lighting, lighting_artifact = cache_lighting_state(cache, args, lighting_path, draw)

        discovery_artifact = cache.put_artifact(
            "cmb-fragment-lighting-discovery", args, discovery_path, suffix=".log"
        )
        validation_artifact = cache.put_artifact(
            "cmb-fragment-lighting-validation", args, validation_path, suffix=".log"
        )
        return {
            "capture_version": CAPTURE_VERSION,
            "draw": draw,
            "discovery_line": discovery_line,
            "validation_line": validation_line,
            "discovery_artifact": str(discovery_artifact),
            "validation_artifact": str(validation_artifact),
            "lighting_artifact": str(lighting_artifact),
            "fixture_artifact": str(fixture_artifact),
            "logger_selftest_artifact": str(selftest_artifact),
            "lighting": lighting,
        }
    finally:
        harness.close()
        discovery_path.unlink(missing_ok=True)
        validation_path.unlink(missing_ok=True)
        lighting_path.unlink(missing_ok=True)
        fixture_path.unlink(missing_ok=True)
        selftest_path.unlink(missing_ok=True)


def capture_probe(
    cache: CacheLike,
    entrance: int = DEFAULT_ENTRANCE,
    daytime: int = DEFAULT_DAYTIME,
    settle_frames: int = DEFAULT_SETTLE_FRAMES,
    fixture: str = DEFAULT_FIXTURE,
) -> tuple[dict[str, Any], bool]:
    args = probe_args(entrance, daytime, settle_frames, fixture)
    frame = capture_frame(settle_frames)
    cached = cache.get_probe("cmb-fragment-lighting-state", frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("cmb-fragment-lighting-failure", frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")
    try:
        result = _capture_live(
            cache, args, entrance, daytime, settle_frames, fixture
        )
    except (OSError, RuntimeError, ValueError) as error:
        cache.put_probe(
            "cmb-fragment-lighting-failure",
            frame,
            args,
            {"capture_version": CAPTURE_VERSION, "error": str(error)},
        )
        raise
    cache.put_probe("cmb-fragment-lighting-state", frame, args, result)
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
        "--fixture",
        choices=("kokiri-save-overlay", "kokiri-gameplay", "gameplay"),
        default=DEFAULT_FIXTURE,
    )
    args = parser.parse_args(arguments)
    if args.settle_frames < 0:
        parser.error("--settle-frames must be non-negative")
    try:
        cache = cache_context()
        result, hit = capture_probe(
            cache,
            entrance=args.entrance,
            daytime=args.daytime,
            settle_frames=args.settle_frames,
            fixture=args.fixture,
        )
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
