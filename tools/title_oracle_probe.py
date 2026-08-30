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

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_cache import OracleCache
from harness_process import spawn
from oracle_fragment_summary import summarize
from repo_environment import apply_repo_environment
from title_oracle_context import (
    SAVESTATE,
    configure_vanilla_title_context,
    oracle_frame_for_title_cs,
)

OUTDIR = REPO / "scratch" / "title_oracle_probe"
CAPTURE_VERSION = 1
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


def step_oracle(harness, count: int, chunk: int = 100) -> None:
    remaining = count
    while remaining > 0:
        step = min(remaining, chunk)
        response = harness.send(f"run {step}")
        if response != f"ok run {step}":
            raise RuntimeError(f"oracle title step failed: {response}")
        remaining -= step


def capture_frame_log(harness, title_cs: int, path: Path, *, draw: int | None) -> None:
    oracle_frame = oracle_frame_for_title_cs(title_cs)
    if oracle_frame < 2:
        raise ValueError("title draw capture requires at least two oracle steps")
    step_oracle(harness, oracle_frame - 2)
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


def dual_texture_draws(path: Path) -> list[tuple[int, str]]:
    candidates: list[tuple[int, str]] = []
    with path.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            draw_match = DRAW_RE.match(line)
            texture_match = TEX_ENABLE_RE.search(line)
            if draw_match is None or texture_match is None:
                continue
            if texture_match.group("tex0") == "1" and texture_match.group("tex1") == "1":
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
    harness = spawn(save_state=str(SAVESTATE))
    try:
        capture_frame_log(harness, title_cs, live, draw=None)
        captured = cache.put_artifact("title-vsuni", args, live)
    finally:
        harness.close()
        live.unlink(missing_ok=True)
    return captured, False


def capture_fragments(cache: OracleCache, title_cs: int, draw: int) -> tuple[Path, dict[str, object], bool]:
    args = artifact_args(title_cs, draw=draw)
    cached = cache.get_artifact("title-fragments", args)
    if cached is not None:
        with cached.open(encoding="utf-8", errors="replace") as stream:
            return cached, summarize(stream, draw), True

    OUTDIR.mkdir(parents=True, exist_ok=True)
    live = OUTDIR / f"live_fragments_cs{title_cs}_draw{draw}_{os.getpid()}.log"
    os.environ["SOH3D_PIXEL_DRAW"] = str(draw)
    harness = spawn(save_state=str(SAVESTATE))
    try:
        capture_frame_log(harness, title_cs, live, draw=draw)
        with live.open(encoding="utf-8", errors="replace") as stream:
            result = summarize(stream, draw)
        captured = cache.put_artifact("title-fragments", args, live)
        cache.put_probe("title-fragment-summary", oracle_frame_for_title_cs(title_cs), args, result)
    finally:
        harness.close()
        live.unlink(missing_ok=True)
    return captured, result, False


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    subcommands = parser.add_subparsers(dest="command", required=True)
    uniforms = subcommands.add_parser("uniforms", help="cache one exact title frame's draw/uniform log")
    uniforms.add_argument("title_cs", type=int)
    fragments = subcommands.add_parser("fragments", help="cache one selected draw's fragment stream")
    fragments.add_argument("title_cs", type=int)
    fragments.add_argument("draw", type=int)
    args = parser.parse_args(argv)

    try:
        cache = cache_context()
        if args.command == "uniforms":
            path, hit = capture_uniforms(cache, args.title_cs)
            candidates = dual_texture_draws(path)
            print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
            print(f"artifact: {path}")
            print(f"dual-texture draws: {len(candidates)}")
            for _draw, line in candidates:
                print(line)
        else:
            path, result, hit = capture_fragments(cache, args.title_cs, args.draw)
            print(f"oracle: {'cache hit' if hit else 'captured and cached'} key={cache.key}")
            print(f"artifact: {path}")
            print(json.dumps(result, indent=2, sort_keys=True))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"title_oracle_probe: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
