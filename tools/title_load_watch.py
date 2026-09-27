#!/usr/bin/env python3
"""Press A once at the title, then give the black screen a REAL load budget and watch it.

`title_flow_watch.py` established the flow shape: the title demo runs at scene 0x006b, A moves the
game to scene 0x0000, and that screen renders black. It then kept pressing buttons, so only ~160
frames ever elapsed at the black screen before the probe gave up. A screen that is *loading* looks
exactly like a screen that is *stuck*: both are black.

This probe presses A exactly once and then only advances time, snapshotting as it goes. If the
screen is loading, it resolves at some frame count and `playstate` leaves `title`. If it is stuck,
the frames stay black indefinitely and the blocker is not a frame budget.

Usage:
    source .env
    tools/title_load_watch.py [--after-a 4000] [--chunk 200]
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_process import spawn  # noqa: E402

OUT = REPO / "scratch" / "title_load_watch"
BTN_A = 1 << 0
BTN_B = 1 << 1
BTN_START = 1 << 3
BUTTONS = {"a": BTN_A, "b": BTN_B, "start": BTN_START}


def to_png(ppm: Path) -> None:
    subprocess.run(
        ["uv", "run", "--frozen", "python", "-c",
         "import sys;from PIL import Image;Image.open(sys.argv[1]).save(sys.argv[2])",
         str(ppm), str(ppm).replace(".ppm", ".png")],
        cwd=REPO, check=True, capture_output=True,
    )


def snap(harness, label: str) -> tuple[str, str, str]:
    OUT.mkdir(parents=True, exist_ok=True)
    base = OUT / label
    harness.send_multiline(f"snapshot {base}")
    az = Path(str(base) + ".az.ppm")
    if az.exists():
        to_png(az)
    scene = (harness.send("scene") or "").strip()
    play = (harness.send("playstate") or "").strip()
    game = (harness.send("gameplay") or "").strip()
    print(f"[{label:16s}] scene={scene!r} playstate={play!r} gameplay={game!r}", flush=True)
    return scene, play, game


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--after-a", type=int, default=4000, help="frames to advance at the black screen")
    parser.add_argument("--chunk", type=int, default=200, help="frames between snapshots")
    parser.add_argument("--title-frames", type=int, default=300, help="frames to run before pressing A")
    parser.add_argument(
        "--pre",
        default="start,start,a",
        help="comma-separated buttons to tap, in order, before the long watch. The flow is order "
        "sensitive and that is not obvious: Start alone changes nothing visible, yet Start then A "
        "moves the title from the demo scene to a different one, while A alone does nothing at all.",
    )
    args = parser.parse_args()
    if args.chunk <= 0:
        raise ValueError("--chunk must be positive")
    if args.after_a < 0:
        raise ValueError("--after-a must be non-negative")
    unknown = [b.strip() for b in args.pre.split(",") if b.strip() and b.strip() not in BUTTONS]
    if unknown:
        raise ValueError(f"unknown buttons {unknown}; known: {sorted(BUTTONS)}")

    os.environ.setdefault("HARNESS_STDERR", str(REPO / "scratch" / "logs" / "title_load_watch.log"))
    harness = spawn()
    boot = harness.send("soh_boot")
    if not boot.startswith("ok"):
        harness.quit()
        raise SystemExit(f"soh_boot failed: {boot}")
    try:
        for done in range(0, args.title_frames, 100):
            harness.send("run 100", per_line_timeout=240.0)
        snap(harness, "00_demo")
        for index, name in enumerate(b.strip() for b in args.pre.split(",")):
            if not name:
                continue
            harness.send(f"input 0x{BUTTONS[name]:x}")
            harness.send("run 4")
            harness.send("input 0")
            snap(harness, f"01_pre_{index:02d}_{name}")
        snap(harness, "02_after_pre")
        advanced = 0
        while advanced < args.after_a:
            step = min(args.chunk, args.after_a - advanced)
            harness.send(f"run {step}", per_line_timeout=300.0)
            advanced += step
            scene, play, game = snap(harness, f"03_at_{advanced:05d}")
            if game.endswith("yes"):
                print(f"[title_load_watch] GAMEPLAY at +{advanced} frames after {args.pre!r}")
                return 0
            if "mode=title" not in play:
                print(f"[title_load_watch] playstate left title at +{advanced}: {play!r}")
                return 0
        print(f"[title_load_watch] still in 'title' after {args.after_a} frames following"
              f" {args.pre!r}: this is not a load budget")
    finally:
        harness.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
