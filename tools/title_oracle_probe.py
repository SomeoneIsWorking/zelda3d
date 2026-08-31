#!/usr/bin/env python3
"""Capture and cache exact-cursor OoT3D title draw evidence.

Uniform and fragment captures are immutable OracleCache artifacts. A cache hit
returns before spawning the embedded oracle, so analysis can be repeated freely
without rerunning OoT3D.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_cache import OracleCache
from harness_process import spawn
from oracle_fragment_summary import summarize
from pica_command_list import last_register_write
from pica_command_provenance_oracle_probe import parse_provenance
from repo_environment import apply_repo_environment
from title_oracle_context import (
    SAVESTATE,
    configure_vanilla_title_context,
    oracle_frame_for_title_cs,
)

OUTDIR = REPO / "scratch" / "title_oracle_probe"
CAPTURE_VERSION = 2
CHECKPOINT_VERSION = 1
CHECKPOINT_INTERVAL = 400
ORACLE_STEP_CHUNK = 25
DRAW_RE = re.compile(r"^draw n=(?P<draw>\d+) .*$")
TEX_ENABLE_RE = re.compile(r"\btexEn=(?P<tex0>[01])/(?P<tex1>[01])/(?P<tex2>[01])\b")


def artifact_args(title_cs: int, *, draw: int | None = None) -> dict[str, int]:
    args = {
        "capture_version": CAPTURE_VERSION,
        "title_cs": title_cs,
        "oracle_frame": oracle_frame_for_title_cs(title_cs),
        "software_renderer": 1,
    }
    if draw is not None:
        args["draw"] = draw
    return args


def checkpoint_args(oracle_frame: int) -> dict[str, int]:
    return {
        "checkpoint_version": CHECKPOINT_VERSION,
        "oracle_frame": oracle_frame,
        "software_renderer": 1,
    }


def latest_checkpoint(cache: OracleCache, target_frame: int) -> tuple[int, Path] | None:
    # Leave at least three post-load frames before the selected frame. Azahar's renderer-owned
    # caches are not serialized, and the first frame after loadstate is known to be corrupt.
    highest = ((target_frame - 3) // CHECKPOINT_INTERVAL) * CHECKPOINT_INTERVAL
    for oracle_frame in range(highest, 0, -CHECKPOINT_INTERVAL):
        cached = cache.get_artifact("title-checkpoint", checkpoint_args(oracle_frame))
        if cached is not None:
            return oracle_frame, cached
    return None


def step_oracle(harness, count: int, chunk: int = ORACLE_STEP_CHUNK) -> None:
    remaining = count
    while remaining > 0:
        step = min(remaining, chunk)
        response = harness.send(f"run {step}")
        if response != f"ok run {step}":
            raise RuntimeError(f"oracle title step failed: {response}")
        remaining -= step


def advance_with_checkpoints(
    cache: OracleCache,
    harness,
    start_frame: int,
    target_frame: int,
) -> None:
    current = start_frame
    while current < target_frame:
        next_checkpoint = ((current // CHECKPOINT_INTERVAL) + 1) * CHECKPOINT_INTERVAL
        stop = min(target_frame, next_checkpoint)
        step_oracle(harness, stop - current)
        current = stop
        if current != next_checkpoint or target_frame - current < 3:
            continue
        OUTDIR.mkdir(parents=True, exist_ok=True)
        live = OUTDIR / f"live_checkpoint_az{current}_{os.getpid()}.state"
        try:
            response = harness.send(f"savestate {live}")
            if response != "ok":
                raise RuntimeError(f"oracle checkpoint save failed: {response}")
            cache.put_artifact(
                "title-checkpoint",
                checkpoint_args(current),
                live,
                suffix=".state",
            )
        finally:
            live.unlink(missing_ok=True)


def capture_frame_log(
    cache: OracleCache,
    harness,
    start_frame: int,
    title_cs: int,
    path: Path,
    *,
    draw: int | None,
) -> None:
    oracle_frame = oracle_frame_for_title_cs(title_cs)
    if oracle_frame < 2:
        raise ValueError("title draw capture requires at least two oracle steps")
    advance_with_checkpoints(cache, harness, start_frame, oracle_frame - 2)
    uniform_response = harness.send(f"vsuni_log {path}")
    if uniform_response != f"ok vsuni_log {path}":
        raise RuntimeError(f"oracle uniform logger failed: {uniform_response}")
    if draw is not None:
        draw_response = harness.send(f"draw_log {path}")
        if draw_response != f"ok draw_log {path}":
            raise RuntimeError(f"oracle fragment logger failed: {draw_response}")
    step_oracle(harness, 2)
    if draw is not None:
        harness.send("draw_log off")
    harness.send("vsuni_log off")


def spawn_for_capture(cache: OracleCache, title_cs: int):
    target_frame = oracle_frame_for_title_cs(title_cs) - 2
    checkpoint = latest_checkpoint(cache, target_frame)
    if checkpoint is None:
        return spawn(save_state=str(SAVESTATE)), 0
    oracle_frame, path = checkpoint
    return spawn(save_state=str(path)), oracle_frame


def dual_texture_draws(path: Path) -> list[tuple[int, str]]:
    candidates: list[tuple[int, str]] = []
    with path.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            draw_match = DRAW_RE.match(line)
            texture_match = TEX_ENABLE_RE.search(line)
            if draw_match is None or texture_match is None:
                continue
            if (
                texture_match.group("tex0") == "1"
                and texture_match.group("tex1") == "1"
            ):
                candidates.append((int(draw_match.group("draw")), line.rstrip()))
    return candidates


def cache_context() -> OracleCache:
    apply_repo_environment(REPO, os.environ)
    configure_vanilla_title_context(os.environ)
    # PIXEL records are emitted by Azahar's software rasterizer. Use the same
    # renderer for the preceding identity capture so draw numbering cannot
    # silently differ between the discovery and selected-fragment phases.
    os.environ["SOH3D_HARNESS_SW"] = "1"
    return OracleCache(SAVESTATE)


def capture_uniforms(cache: OracleCache, title_cs: int) -> tuple[Path, bool]:
    args = artifact_args(title_cs)
    cached = cache.get_artifact("title-vsuni", args)
    if cached is not None:
        return cached, True

    OUTDIR.mkdir(parents=True, exist_ok=True)
    live = OUTDIR / f"live_vsuni_cs{title_cs}_{os.getpid()}.log"
    harness, start_frame = spawn_for_capture(cache, title_cs)
    try:
        capture_frame_log(cache, harness, start_frame, title_cs, live, draw=None)
        captured = cache.put_artifact("title-vsuni", args, live)
    finally:
        harness.close()
        live.unlink(missing_ok=True)
    return captured, False


def capture_fragments(
    cache: OracleCache, title_cs: int, draw: int
) -> tuple[Path, dict[str, object], bool]:
    args = artifact_args(title_cs, draw=draw)
    cached = cache.get_artifact("title-fragments", args)
    if cached is not None:
        with cached.open(encoding="utf-8", errors="replace") as stream:
            return cached, summarize(stream, draw), True

    OUTDIR.mkdir(parents=True, exist_ok=True)
    live = OUTDIR / f"live_fragments_cs{title_cs}_draw{draw}_{os.getpid()}.log"
    os.environ["SOH3D_PIXEL_DRAW"] = str(draw)
    harness, start_frame = spawn_for_capture(cache, title_cs)
    try:
        capture_frame_log(cache, harness, start_frame, title_cs, live, draw=draw)
        with live.open(encoding="utf-8", errors="replace") as stream:
            result = summarize(stream, draw)
        captured = cache.put_artifact("title-fragments", args, live)
        cache.put_probe(
            "title-fragment-summary", oracle_frame_for_title_cs(title_cs), args, result
        )
    finally:
        harness.close()
        live.unlink(missing_ok=True)
    return captured, result, False


def command_list_args(title_cs: int, draw: int) -> dict[str, int]:
    """Return the immutable identity for one title draw's PICA command list."""
    return {
        **artifact_args(title_cs, draw=draw),
        "pica_command_list_capture_version": 1,
    }


def register_args(title_cs: int, draw: int, register: int) -> dict[str, int]:
    """Return the immutable identity for one title-frame PICA register observation."""
    return {
        **command_list_args(title_cs, draw),
        "pica_register_capture_version": 1,
        "register": register,
    }


def capture_command_list(cache: OracleCache, title_cs: int, draw: int) -> tuple[dict[str, Any], bool]:
    """Cache one title draw's exact PICA provenance and raw command list.

    The title cursor driver remains this module's owner.  Packet provenance and
    raw command list are preserved before any register-specific interpretation,
    so later analysis always reuses this one oracle execution.
    """
    args = command_list_args(title_cs, draw)
    oracle_frame = oracle_frame_for_title_cs(title_cs)
    cached = cache.get_probe("title-pica-command-list", oracle_frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("title-pica-command-list-failure", oracle_frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")

    OUTDIR.mkdir(parents=True, exist_ok=True)
    log_path = OUTDIR / "live_pica.log"
    command_list_path = OUTDIR / "live_pica.bin"
    harness = None
    try:
        harness, start_frame = spawn_for_capture(cache, title_cs)
        capture_frame_log(cache, harness, start_frame, title_cs, log_path, draw=None)
        if not log_path.is_file():
            raise RuntimeError("oracle PICA logger produced no title provenance log")
        provenance = parse_provenance(log_path.read_text().splitlines(), draw)
        command_list_bytes = provenance["command_list_word_count"] * 4
        response = harness.send_multiline(
            f"dumpphys 0x{provenance['command_list_address']:08x} {command_list_bytes} {command_list_path}"
        )
        if len(response) != 1 or not response[0].startswith("ok dumpphys "):
            raise RuntimeError(f"oracle title command-list dump failed: {response}")
        if not command_list_path.is_file():
            raise RuntimeError("oracle title command-list dump created no artifact")
        payload = command_list_path.read_bytes()
        if len(payload) != command_list_bytes:
            raise RuntimeError(
                f"oracle title command-list dump has {len(payload)} bytes, expected {command_list_bytes}"
            )
        provenance_artifact = cache.put_artifact(
            "title-pica-provenance", args, log_path, suffix=".log"
        )
        command_list_artifact = cache.put_artifact(
            "title-pica-command-list-data", args, command_list_path, suffix=".bin"
        )
        result = {
            "capture_version": 1,
            "draw": draw,
            "command_list_artifact": str(command_list_artifact),
            "provenance_artifact": str(provenance_artifact),
            **provenance,
        }
        cache.put_probe("title-pica-command-list", oracle_frame, args, result)
        return result, False
    except (OSError, RuntimeError, ValueError) as error:
        cache.put_probe(
            "title-pica-command-list-failure",
            oracle_frame,
            args,
            {"capture_version": 1, "error": str(error)},
        )
        raise
    finally:
        if harness is not None:
            harness.close()
        log_path.unlink(missing_ok=True)
        command_list_path.unlink(missing_ok=True)


def capture_register(
    cache: OracleCache, title_cs: int, draw: int, register: int
) -> tuple[dict[str, Any], bool]:
    """Cache the final write to one PICA register in an already-cached title draw."""
    args = register_args(title_cs, draw, register)
    oracle_frame = oracle_frame_for_title_cs(title_cs)
    cached = cache.get_probe("title-pica-register", oracle_frame, args)
    if cached is not None:
        return cached, True
    failed = cache.get_probe("title-pica-register-failure", oracle_frame, args)
    if failed is not None:
        raise RuntimeError(f"cached oracle failure: {failed['error']}")

    try:
        command_list, _ = capture_command_list(cache, title_cs, draw)
        payload = Path(command_list["command_list_artifact"]).read_bytes()
        word_index, value = last_register_write(
            payload, command_list["command_list_word_index"], register
        )
        result = {
            "capture_version": 1,
            "register": f"0x{register:03x}",
            "value": f"0x{value:08x}",
            "command_list_word_index": word_index,
            **command_list,
        }
        cache.put_probe("title-pica-register", oracle_frame, args, result)
        return result, False
    except (OSError, RuntimeError, ValueError) as error:
        cache.put_probe(
            "title-pica-register-failure",
            oracle_frame,
            args,
            {"capture_version": 1, "error": str(error)},
        )
        raise


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subcommands = parser.add_subparsers(dest="command", required=True)
    uniforms = subcommands.add_parser(
        "uniforms", help="cache one exact title frame's draw/uniform log"
    )
    uniforms.add_argument("title_cs", type=int)
    fragments = subcommands.add_parser(
        "fragments", help="cache one selected draw's fragment stream"
    )
    fragments.add_argument("title_cs", type=int)
    fragments.add_argument("draw", type=int)
    pica = subcommands.add_parser(
        "pica-register", help="cache one title draw's final PICA register write"
    )
    pica.add_argument("title_cs", type=int)
    pica.add_argument("draw", type=int)
    pica.add_argument("register", type=lambda value: int(value, 0))
    command_list = subcommands.add_parser(
        "pica-command-list", help="cache one title draw's raw PICA command list"
    )
    command_list.add_argument("title_cs", type=int)
    command_list.add_argument("draw", type=int)
    args = parser.parse_args(argv)

    try:
        cache = cache_context()
        if args.command == "uniforms":
            path, hit = capture_uniforms(cache, args.title_cs)
            candidates = dual_texture_draws(path)
            print(
                f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}"
            )
            print(f"artifact: {path}")
            print(f"dual-texture draws: {len(candidates)}")
            for _draw, line in candidates:
                print(line)
        elif args.command == "fragments":
            path, result, hit = capture_fragments(cache, args.title_cs, args.draw)
            print(
                f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}"
            )
            print(f"artifact: {path}")
            print(json.dumps(result, indent=2, sort_keys=True))
        elif args.command == "pica-register":
            if not 0 <= args.register <= 0x2FF:
                parser.error("register must be a PICA register index")
            result, hit = capture_register(cache, args.title_cs, args.draw, args.register)
            print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
            print(json.dumps(result, indent=2, sort_keys=True))
        else:
            result, hit = capture_command_list(cache, args.title_cs, args.draw)
            print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
            print(json.dumps(result, indent=2, sort_keys=True))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"title_oracle_probe: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
