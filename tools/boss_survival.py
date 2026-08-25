#!/usr/bin/env python3
"""Exercise every OoT boss arena beyond its initial scene-load window."""

from __future__ import annotations

import os
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GAME = REPO / "tools/zelda3d_game.py"
REPL = REPO / "tools/zelda3d_repl.py"
LOG = REPO / "scratch/logs/run.log"
BOSSES = (
    ("0x40F", "Gohma_DekuBoss"),
    ("0x40B", "KingDodongo_DodongoBoss"),
    ("0x301", "Barinade_JabuBoss"),
    ("0x00C", "PhantomGanon_ForestBoss"),
    ("0x305", "Volvagia_FireBoss"),
    ("0x417", "Morpha_WaterBoss"),
    ("0x08D", "Twinrova_SpiritBoss"),
    ("0x413", "BongoBongo_ShadowBoss"),
    ("0x41F", "Ganondorf_Boss"),
    ("0x517", "Ganon_Boss"),
)
CRASH = re.compile(r"FATAL signal|Signal: 11|terminate called|Assertion")


def main() -> int:
    dwell = float(os.environ.get("DWELL", "14"))
    environment = {**os.environ, "ZELDA3D_HEADLESS": "1"}
    for entrance, name in BOSSES:
        subprocess.run(
            [sys.executable, str(GAME), "restart", entrance, "0x6000"],
            cwd=REPO,
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=True,
        )
        time.sleep(dwell)
        actors = subprocess.run(
            [sys.executable, str(REPL), "cmd", "actors"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=False,
        ).stdout
        boss_count = actors.count("cat=9")
        crash_lines = [line for line in LOG.read_text(errors="replace").splitlines() if CRASH.search(line)]
        if crash_lines:
            print(f"{entrance} {name} => CRASH")
            for line in crash_lines[-2:]:
                print(f"    {line}")
        else:
            print(f"{entrance} {name} => ALIVE (boss cat=9 actors: {boss_count})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
