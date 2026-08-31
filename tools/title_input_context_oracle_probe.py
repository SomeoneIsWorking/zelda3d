#!/usr/bin/env python3
"""Capture and cache the title input-latch response to one Start press."""

from __future__ import annotations

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
from harness_paths import GAMEPLAY_STATE, TITLE_STATE
from harness_process import spawn
from repo_environment import apply_repo_environment
from title_oracle_context import configure_vanilla_title_context, initial_title_cs

CAPTURE_VERSION = 2
INPUT_CONTEXT_GLOBAL = 0x0050BB50
INPUT_LATCH_OFFSET = 0x08
START_MASK = 1 << 3
BUTTON_MASKS = {"start": START_MASK, "a": 1 << 8}
HOLD_FRAMES = 4
RELEASE_FRAMES = 8
ADVANCE_CHUNK_FRAMES = 30
MAX_WATCH_RECORDS = 128
OUTDIR = REPO / "scratch" / "title_input_context"

READ_U32_RE = re.compile(r"^ok 0x(?P<value>[0-9a-fA-F]{8})$")
HITS_RE = re.compile(r"^ok hits (?P<count>\d+)$")


class CacheLike(Protocol):
    key: str

    def get_probe(self, name: str, frame: int, args: dict[str, Any]) -> dict[str, Any] | None: ...

    def put_probe(self, name: str, frame: int, args: dict[str, Any], result: dict[str, Any]) -> None: ...

    def put_artifact(
        self, name: str, args: dict[str, Any], source: Path, suffix: str | None = None
    ) -> Path: ...


def probe_args(
    checkpoint_cs: int, advance_frames: int, settle_frames: int, buttons: tuple[str, ...]
) -> dict[str, Any]:
    return {
        "capture_version": CAPTURE_VERSION,
        "checkpoint_cs": checkpoint_cs,
        "advance_frames": advance_frames,
        "input_context_global": f"0x{INPUT_CONTEXT_GLOBAL:08x}",
        "input_latch_offset": f"0x{INPUT_LATCH_OFFSET:x}",
        "buttons": list(buttons),
        "button_masks": [f"0x{BUTTON_MASKS[button]:x}" for button in buttons],
        "hold_frames": HOLD_FRAMES,
        "settle_frames": settle_frames,
        "texture_pack": 0,
    }


def read_u32(harness: Any, address: int) -> int:
    response = harness.send(f"r32 0x{address:08x}")
    match = READ_U32_RE.fullmatch(response.strip())
    if match is None:
        raise RuntimeError(f"r32 0x{address:08x} returned malformed response: {response!r}")
    return int(match.group("value"), 16)


def watch_record_count(lines: list[str]) -> int:
    if len(lines) < 2 or lines[-1] != "ok end":
        raise RuntimeError(f"input-latch watch returned malformed response: {lines}")
    match = HITS_RE.fullmatch(lines[0])
    if match is None:
        raise RuntimeError(f"input-latch watch returned malformed header: {lines[0]!r}")
    count = int(match.group("count"))
    if count >= MAX_WATCH_RECORDS:
        raise RuntimeError("input-latch watch reached its 128-record cap; capture is truncated")
    return count


def advance_title(harness: Any, frames: int) -> None:
    """Advance in bounded commands so a slow title frame cannot hide progress."""
    remaining = frames
    while remaining:
        chunk = min(remaining, ADVANCE_CHUNK_FRAMES)
        response = harness.send(f"run {chunk}")
        if response != f"ok run {chunk}":
            raise RuntimeError(f"title input-context advance failed: {response}")
        remaining -= chunk


def parse_buttons(value: str) -> tuple[str, ...]:
    buttons = tuple(part.strip().lower() for part in value.split(",") if part.strip())
    if not buttons or any(button not in BUTTON_MASKS for button in buttons):
        choices = ", ".join(BUTTON_MASKS)
        raise ValueError(f"buttons must be a non-empty comma-separated subset of: {choices}")
    return buttons


def _capture_live(
    cache: CacheLike,
    args: dict[str, Any],
    advance_frames: int,
    settle_frames: int,
    buttons: tuple[str, ...],
) -> dict[str, Any]:
    OUTDIR.mkdir(parents=True, exist_ok=True)
    raw_path = OUTDIR / "start-latch.log"
    environment = dict(os.environ)
    configure_vanilla_title_context(environment)
    harness = spawn(save_state=str(TITLE_STATE), environment=environment)
    latch_address: int | None = None
    try:
        if advance_frames:
            advance_title(harness, advance_frames)
        input_context = read_u32(harness, INPUT_CONTEXT_GLOBAL)
        if input_context == 0:
            raw_path.write_text(
                "\n".join(
                    [
                        f"# advance_frames={advance_frames}",
                        f"# input_context_global=0x{INPUT_CONTEXT_GLOBAL:08x}",
                        "# input_context=0x00000000",
                        "# outcome=no-input-context",
                    ]
                )
                + "\n"
            )
            artifact = cache.put_artifact("title-input-context-start", args, raw_path, suffix=".log")
            return {
                "input_context": "0x00000000",
                "outcome": "no-input-context",
                "artifact": str(artifact),
            }
        latch_address = input_context + INPUT_LATCH_OFFSET
        response = harness.send(f"watch 0x{latch_address:08x} 4")
        if response != f"ok watch 0x{latch_address:08x} 4":
            raise RuntimeError(f"failed to arm title input-latch watch: {response}")
        steps: list[dict[str, Any]] = []
        raw_lines = [
            f"# advance_frames={advance_frames}",
            f"# input_context=0x{input_context:08x}",
            f"# latch=0x{latch_address:08x}",
        ]
        gameplay = "ok no"
        for button in buttons:
            mask = BUTTON_MASKS[button]
            harness.send(f"hitclear 0x{latch_address:08x}")
            before = read_u32(harness, latch_address)
            press_response = harness.send(f"input 0x{mask:x}")
            hold_response = harness.send(f"run {HOLD_FRAMES}")
            release_response = harness.send("input 0")
            advance_title(harness, settle_frames)
            after = read_u32(harness, latch_address)
            gameplay = harness.send("gameplay")
            hits = harness.send_multiline(f"hits 0x{latch_address:08x}")
            count = watch_record_count(hits)
            step = {
                "button": button,
                "latch_before": f"0x{before:08x}",
                "latch_after": f"0x{after:08x}",
                "watch_records": count,
                "gameplay": gameplay,
            }
            steps.append(step)
            raw_lines.extend(
                [
                    f"# button={button} mask=0x{mask:x}",
                    f"# before=0x{before:08x}",
                    f"# input={press_response}",
                    f"# hold={hold_response}",
                    f"# release={release_response}",
                    f"# settle=ok run {settle_frames} (chunked)",
                    f"# after=0x{after:08x}",
                    f"# gameplay={gameplay}",
                    "# watch hits",
                    *hits,
                ]
            )
            if gameplay == "ok yes":
                break
        result = {
            "input_context": f"0x{input_context:08x}",
            "latch_address": f"0x{latch_address:08x}",
            "steps": steps,
            "gameplay": gameplay,
        }
        if gameplay == "ok yes" and not GAMEPLAY_STATE.exists():
            save_response = harness.send(f"savestate {GAMEPLAY_STATE}")
            if not save_response.startswith("ok"):
                raise RuntimeError(f"failed to save p45 gameplay state: {save_response}")
            result["gameplay_state"] = str(GAMEPLAY_STATE)
        raw_path.write_text(
            "\n".join(raw_lines) + "\n"
        )
        artifact = cache.put_artifact("title-input-context-start", args, raw_path, suffix=".log")
        result["artifact"] = str(artifact)
        return result
    finally:
        if latch_address is not None:
            harness.send(f"unwatch 0x{latch_address:08x} 4")
        harness.close()


def capture_probe(
    cache: CacheLike,
    checkpoint_cs: int,
    advance_frames: int = 0,
    settle_frames: int = RELEASE_FRAMES,
    buttons: tuple[str, ...] = ("start",),
    *,
    capture_live: Callable[[CacheLike, dict[str, Any], int, int, tuple[str, ...]], dict[str, Any]] = _capture_live,
) -> tuple[dict[str, Any], bool]:
    if advance_frames < 0 or settle_frames < 0:
        raise ValueError("advance_frames and settle_frames must be non-negative")
    if not buttons or any(button not in BUTTON_MASKS for button in buttons):
        raise ValueError("buttons must use a supported title confirmation control")
    args = probe_args(checkpoint_cs, advance_frames, settle_frames, buttons)
    cached = cache.get_probe("title-input-context-start", advance_frames, args)
    if cached is not None:
        return cached, True
    result = capture_live(cache, args, advance_frames, settle_frames, buttons)
    cache.put_probe("title-input-context-start", advance_frames, args, result)
    return result, False


def cache_context() -> OracleCache:
    environment = dict(os.environ)
    apply_repo_environment(REPO, environment)
    configure_vanilla_title_context(environment)
    return OracleCache(TITLE_STATE, environment=environment)


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--advance-frames",
        type=int,
        default=0,
        help="advance the settled title checkpoint before observing the input context",
    )
    parser.add_argument(
        "--buttons",
        default="start",
        help="comma-separated confirmation sequence (supported: start,a)",
    )
    parser.add_argument(
        "--settle-frames",
        type=int,
        default=RELEASE_FRAMES,
        help="frames to wait after releasing Start before testing gameplay",
    )
    arguments = parser.parse_args()
    if not TITLE_STATE.is_file():
        print(f"error: missing title checkpoint: {TITLE_STATE}", file=sys.stderr)
        return 1
    try:
        cache = cache_context()
        result, hit = capture_probe(
            cache,
            initial_title_cs(),
            arguments.advance_frames,
            arguments.settle_frames,
            parse_buttons(arguments.buttons),
        )
        print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
