#!/usr/bin/env python3
"""Cold-boot until persistent HUD corruption is caught for live diagnosis."""

from __future__ import annotations

import os
import sys
from pathlib import Path

from hud_corruption_probe import frame_hud, game, hud_mse, repl, screenshot, wait_for_scene

REPO = Path(__file__).resolve().parent.parent


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if len(args) > 2:
        raise ValueError("usage: zelda3d_hud_catch.py [max_launches] [clean_reference.png]")
    maximum = int(args[0]) if args else 25
    reference = REPO / (args[1] if len(args) > 1 else "scratch/screenshots/hudscan/launch_01.png")
    entrance = os.environ.get("SCAN_ENTR", "238")
    daytime = os.environ.get("SCAN_TIME", "0x6000")
    output = REPO / "scratch/screenshots/hudscan"
    output.mkdir(parents=True, exist_ok=True)
    for launch in range(1, maximum + 1):
        label = f"{launch:02d}"
        game("stop")
        if not game("start", entrance, daytime) or not wait_for_scene():
            print(f"catch {label}: not ready")
            continue
        frame_hud(settle_frames=8)
        candidate = screenshot(f"hudscan/catch_{label}")
        mse = hud_mse(reference, candidate)
        print(f"catch {label}: MSE={mse:.1f}")
        if mse > 150:
            print(
                f"CAUGHT CORRUPT at launch {label} (MSE={mse:.1f}) — "
                "game LEFT RUNNING for diagnosis"
            )
            print(f"shot: {candidate.relative_to(REPO)}")
            return 0
    game("stop")
    print(f"no corruption caught in {maximum} launches")
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
