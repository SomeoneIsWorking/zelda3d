#!/usr/bin/env python3
"""Cold-launch HUD corruption corpus producer and classifier."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from hud_corruption_probe import frame_hud, game, repl, screenshot, wait_for_scene

REPO = Path(__file__).resolve().parent.parent


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if len(args) > 1:
        raise ValueError("usage: zelda3d_hud_corruption_scan.py [launches]")
    launches = int(args[0]) if args else 12
    entrance = os.environ.get("SCAN_ENTR", "238")
    daytime = os.environ.get("SCAN_TIME", "0x6000")
    output = REPO / "scratch/screenshots/hudscan"
    output.mkdir(parents=True, exist_ok=True)
    for old in output.glob("launch_*.png"):
        old.unlink()
    captured: list[Path] = []
    for launch in range(1, launches + 1):
        label = f"{launch:02d}"
        print(f"=== launch {label}/{launches} ===")
        game("stop")
        if not game("start", entrance, daytime) or not wait_for_scene():
            print(f"launch {label}: never became ready, skipping")
            continue
        frame_hud(settle_frames=30)
        captured.append(screenshot(f"hudscan/launch_{label}"))
        run_log = REPO / "scratch/logs/run.log"
        debug_lines = [
            line for line in run_log.read_text(errors="replace").splitlines()
            if "HUDTEXDBG" in line
        ]
        (output / f"dbg_{label}.log").write_text(
            "\n".join(debug_lines) + ("\n" if debug_lines else ""),
            encoding="utf-8",
        )
        repl("posinfo", quiet=False)
    game("stop")
    print("\n=== detector ===")
    return subprocess.run(
        [
            sys.executable,
            str(REPO / "tools/zelda3d_hud_detect.py"),
            *(str(path) for path in captured),
        ],
        cwd=REPO,
        check=False,
    ).returncode


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
