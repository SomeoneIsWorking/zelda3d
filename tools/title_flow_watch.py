#!/usr/bin/env python3
"""Drive the OoT3D title flow one step at a time, snapshotting each step.

The cold title route is recorded as failing ("never reached gameplay from the title"), and the
reason is recorded as a missing system title plus empty save slots. Both are guesses about a flow
nobody has watched. This probe watches it: every step reports the harness's own `scene`,
`playstate` and `gameplay` verdicts AND writes a frame pair, so the stall point is a picture rather
than an inference.

The tap schedule is deliberately varied rather than repeated, because the existing driver hammers
one button 12 times and a flow that wants a *held* confirm, or a different button, looks identical
to a flow that wants nothing at all. Each step is labelled with what it pressed.

Usage:
    source .env
    tools/title_flow_watch.py
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from harness_process import spawn  # noqa: E402

OUT = REPO / "scratch" / "title_flow_watch"
BUTTONS = {"start": 1 << 3, "a": 1 << 0, "b": 1 << 1, "select": 1 << 2, "up": 1 << 4, "down": 1 << 5}


def snap(harness, label: str) -> None:
    """Write a frame pair for `label` and report the harness's own verdicts."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = OUT / label
    harness.send_multiline(f"snapshot {base}")
    scene = (harness.send("scene") or "").strip()
    play = (harness.send("playstate") or "").strip()
    game = (harness.send("gameplay") or "").strip()
    have = [s for s in (".soh.ppm", ".az.ppm") if Path(str(base) + s).exists()]
    print(f"[{label:22s}] scene={scene!r} playstate={play!r} gameplay={game!r} frames={have}")
    for suffix in (".soh.ppm", ".az.ppm"):
        src = Path(str(base) + suffix)
        if src.exists():
            subprocess.run(
                ["uv", "run", "--frozen", "python", "-c",
                 "import sys;from PIL import Image;Image.open(sys.argv[1]).save(sys.argv[2])",
                 str(src), str(base) + suffix.replace(".ppm", ".png")],
                cwd=REPO, check=True, capture_output=True,
            )


def tap(harness, name: str, hold: int, release: int) -> None:
    harness.send(f"input 0x{BUTTONS[name]:x}")
    harness.send(f"run {hold}")
    harness.send("input 0")
    harness.send(f"run {release}")


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--start-frame",
        type=int,
        default=300,
        help="run this many frames before the first tap. The title card/logo only appears late "
        "(around frame 1093), and a menu that ignores input until then looks identical to a menu "
        "that ignores input always.",
    )
    args = parser.parse_args()
    os.environ.setdefault("HARNESS_STDERR", str(REPO / "scratch" / "logs" / "title_flow_watch.log"))
    harness = spawn()
    boot = harness.send("soh_boot")
    if not boot.startswith("ok"):
        harness.quit()
        raise SystemExit(f"soh_boot failed: {boot}")
    try:
        snap(harness, "00_boot")
        for done in range(0, args.start_frame, 100):
            harness.send("run 100", per_line_timeout=240.0)
        if args.start_frame % 100:
            harness.send(f"run {args.start_frame % 100}", per_line_timeout=240.0)
        snap(harness, f"01_after_{args.start_frame}")

        # Each entry is (label, button, hold, release). Hold/release are varied because a flow that
        # wants a long confirm and a flow that wants a short one look the same from the outside.
        schedule = [
            ("02_start_short", "start", 4, 8),
            ("03_start_long", "start", 30, 60),
            ("04_a_short", "a", 4, 8),
            ("05_a_long", "a", 30, 60),
            ("06_a_after_settle", "a", 4, 30),
            ("07_down", "down", 4, 8),
            ("08_a_on_down", "a", 4, 8),
            ("09_b", "b", 4, 8),
            ("10_start_again", "start", 4, 8),
        ]
        for label, button, hold, release in schedule:
            tap(harness, button, hold, release)
            snap(harness, label)
            if (harness.send("gameplay") or "").strip().endswith("yes"):
                print(f"[title_flow_watch] reached gameplay at {label}")
                break
        else:
            print("[title_flow_watch] never reached gameplay; the frames above show where it stopped")
    finally:
        harness.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
