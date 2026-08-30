#!/usr/bin/env python3
"""Capture exact SoH3D title-script frames against cached oracle images.

This command never advances the embedded OoT3D oracle. It resolves the oracle
frame for a requested title-cutscene cursor from the RE'd clock relationship,
requires that frame to already exist in OracleCache, and advances only the host.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_cache import OracleCache
from harness_process import spawn
from repo_environment import apply_repo_environment
from title_ab import compose_sxs, content_score, ppm_to_png
from title_oracle_context import (
    SAVESTATE,
    configure_vanilla_title_context,
    oracle_frame_for_title_cs,
)

OUTDIR = REPO / "scratch" / "title_host_capture"

# The host title presentation becomes live at step 232. A small deterministic
# margin establishes the title actor before the direct cursor write.
SOH_TITLE_BOOT_STEPS = 240
TITLE_CS_RE = re.compile(r"^ok soh_titlecs frame=(-?\d+) end=(-?\d+)$")


def read_host_title_cs(harness) -> int:
    read_response = harness.send("soh_titlecs")
    match = TITLE_CS_RE.match(read_response)
    if match is None:
        raise RuntimeError(f"invalid host title cursor response: {read_response}")
    return int(match.group(1))


def step_host(harness, count: int, chunk: int = 100) -> None:
    remaining = count
    while remaining > 0:
        step = min(remaining, chunk)
        response = harness.send(f"soh_step {step}")
        if response != f"ok soh_step {step}":
            raise RuntimeError(f"host title frame step failed: {response}")
        remaining -= step


def advance_host_title(harness, current_cs: int, target_cs: int) -> int:
    """Advance naturally to target_cs without writing title-script state."""
    if target_cs < current_cs:
        raise RuntimeError(
            f"host title target moved backward: current={current_cs}, target={target_cs}"
        )

    # The recovered cursor rate is one title unit per two host ticks. Stop two
    # ticks short of that estimate, then read after each final tick so a phase
    # change cannot silently overshoot the requested frame.
    bulk_steps = max(0, 2 * (target_cs - current_cs) - 2)
    step_host(harness, bulk_steps)
    observed = read_host_title_cs(harness)
    for _ in range(4):
        if observed == target_cs:
            return observed
        if observed > target_cs:
            break
        step_host(harness, 1)
        observed = read_host_title_cs(harness)
    raise RuntimeError(
        f"host title cursor mismatch: requested={target_cs}, observed={observed}"
    )


def boot_host_title(harness, unified_renderer: int) -> int:
    response = harness.send("soh_boot")
    if response != "ok soh_boot":
        raise RuntimeError(f"host boot failed: {response}")
    response = harness.send(f"soh_unified {unified_renderer}")
    if response != f"ok soh_unified {unified_renderer}":
        raise RuntimeError(f"host unified-renderer selection failed: {response}")
    response = harness.send(f"soh_step {SOH_TITLE_BOOT_STEPS}")
    if response != f"ok soh_step {SOH_TITLE_BOOT_STEPS}":
        raise RuntimeError(f"host title boot step failed: {response}")
    response = harness.send("soh_camera")
    if not response.startswith("ok soh_camera live=1"):
        raise RuntimeError(f"host title did not become active: {response}")
    return read_host_title_cs(harness)


def capture_host_frame(harness, title_cs: int, base: Path) -> Path:
    observed = read_host_title_cs(harness)
    if observed != title_cs:
        raise RuntimeError(
            f"refusing mismatched capture: requested={title_cs}, observed={observed}"
        )
    response = harness.send(f"soh_snapshot {base}")
    if not response.startswith("ok soh_snapshot"):
        raise RuntimeError(f"host framebuffer capture failed: {response}")
    return ppm_to_png(str(base) + ".soh.ppm")


def arm_host_draw_list(harness, title_cs: int) -> None:
    response = harness.send("soh_drawlist")
    if response != "ok soh_drawlist armed":
        raise RuntimeError(f"host draw-list arm failed: {response}")
    # advance_host_title stops on the first tick carrying title_cs. The cursor
    # is half-rate, so the next tick remains on that exact script cursor while
    # causing the renderer to publish the requested per-group identity list.
    step_host(harness, 1)
    observed = read_host_title_cs(harness)
    if observed != title_cs:
        raise RuntimeError(
            f"host draw-list tick changed title cursor: requested={title_cs}, observed={observed}"
        )


def wordmark_metrics(oracle_path: Path, host_path: Path) -> dict[str, float | int]:
    """Measure the title wordmark box with one explicit gold-pixel predicate."""
    oracle = np.asarray(Image.open(oracle_path).convert("RGB"), dtype=np.float64)
    host = np.asarray(Image.open(host_path).convert("RGB"), dtype=np.float64)
    if oracle.shape != host.shape:
        raise RuntimeError(f"capture size mismatch: oracle={oracle.shape}, host={host.shape}")

    oracle = oracle[40:190, 110:300]
    host = host[40:190, 110:300]

    def gold_mask(image: np.ndarray) -> np.ndarray:
        red, green, blue = image[..., 0], image[..., 1], image[..., 2]
        return (red > 45.0) & (red > green * 1.05) & (green > blue * 1.15)

    oracle_gold = gold_mask(oracle)
    host_gold = gold_mask(host)
    union = oracle_gold | host_gold
    if not np.any(union):
        raise RuntimeError("wordmark metric found no gold pixels")
    return {
        "oracle_gold_px": int(np.count_nonzero(oracle_gold)),
        "host_gold_px": int(np.count_nonzero(host_gold)),
        "oracle_gold_mean_r": float(oracle[..., 0][oracle_gold].mean()),
        "host_gold_mean_r": float(host[..., 0][host_gold].mean()),
        "union_rgb_mae": float(np.abs(oracle[union] - host[union]).mean()),
    }


def require_cached_oracle_frames(
    cache: OracleCache, title_frames: list[int]
) -> dict[int, tuple[int, Path]]:
    resolved: dict[int, tuple[int, Path]] = {}
    missing: list[tuple[int, int]] = []
    for title_cs in title_frames:
        oracle_frame = oracle_frame_for_title_cs(title_cs)
        cached = cache.get_frame(oracle_frame)
        if cached is None:
            missing.append((title_cs, oracle_frame))
        else:
            resolved[title_cs] = (oracle_frame, cached)
    if missing:
        details = ", ".join(f"cs={cs}->az={az}" for cs, az in missing)
        raise RuntimeError(
            f"oracle cache miss under key={cache.key}: {details}; "
            "warm those frames explicitly with tools/oracle_cache.py warm"
        )
    return resolved


def run(title_frames: list[int], name: str, unified_renderer: int, draw_list: bool) -> None:
    apply_repo_environment(REPO, os.environ)
    # The historical title anchors being consumed here are vanilla ROM frames.
    # Set this before OracleCache construction so both cache identity and host
    # runtime describe the same texture inputs.
    configure_vanilla_title_context(os.environ)
    cache = OracleCache(SAVESTATE)
    ordered_frames = sorted(set(title_frames))
    cached = require_cached_oracle_frames(cache, ordered_frames)
    OUTDIR.mkdir(parents=True, exist_ok=True)
    (REPO / "scratch" / "logs").mkdir(parents=True, exist_ok=True)
    os.environ.setdefault(
        "HARNESS_STDERR", str(REPO / "scratch" / "logs" / "title_host_capture.log")
    )
    # OracleCache stores native 400x240 title frames. Pin the host capture to
    # the same raster instead of comparing a scaled 800x480 default.
    os.environ["ZELDA3D_HARNESS_RES_FACTOR"] = "1"
    os.environ["ZELDA3D_HARNESS_SOH_W"] = "400"
    os.environ["ZELDA3D_HARNESS_SOH_H"] = "240"

    print(f"[title_host_capture] oracle cache hit key={cache.key}")
    harness = spawn()
    try:
        current_cs = boot_host_title(harness, unified_renderer)
        for title_cs in ordered_frames:
            oracle_frame, oracle_path = cached[title_cs]
            current_cs = advance_host_title(harness, current_cs, title_cs)
            if draw_list:
                arm_host_draw_list(harness, title_cs)
            base = OUTDIR / f"{name}_cs{title_cs}"
            host_path = capture_host_frame(harness, title_cs, base)
            oracle_output = Path(str(base) + ".az.png")
            Image.open(oracle_path).convert("RGB").save(oracle_output)
            score = content_score(oracle_output, host_path)
            metrics = wordmark_metrics(oracle_output, host_path)
            comparison = compose_sxs(
                oracle_output,
                host_path,
                Path(str(base) + "_sxs.png"),
                label_top=f"cached OoT3D oracle az={oracle_frame} / title cs={title_cs}",
                label_bottom=f"SoH3D naturally advanced title cs={title_cs}",
            )
            print(
                f"[title_host_capture] cs={title_cs} az={oracle_frame} "
                f"content={score:.4f} gold_px={metrics['oracle_gold_px']}/"
                f"{metrics['host_gold_px']} gold_mean_r="
                f"{metrics['oracle_gold_mean_r']:.1f}/{metrics['host_gold_mean_r']:.1f} "
                f"union_rgb_mae={metrics['union_rgb_mae']:.2f} sxs={comparison}"
            )
    finally:
        harness.close()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("title_cs", type=int, nargs="+", help="exact title cutscene cursor(s)")
    parser.add_argument("--name", default="title", help="output basename prefix")
    parser.add_argument(
        "--unified-renderer",
        type=int,
        choices=range(4),
        default=1,
        help="host unified-renderer bitmask (default: 1, CMB unified)",
    )
    parser.add_argument(
        "--draw-list",
        action="store_true",
        help="publish the exact-cursor host group/material identity list",
    )
    args = parser.parse_args(argv)
    try:
        run(args.title_cs, args.name, args.unified_renderer, args.draw_list)
    except (RuntimeError, ValueError) as error:
        print(f"title_host_capture: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
