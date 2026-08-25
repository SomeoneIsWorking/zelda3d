#!/usr/bin/env python3
"""Measure one routed actor's replacement-model contribution across instances."""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

from zelda3d_repl import send, shot

REPO = Path(__file__).resolve().parent.parent


def _send(command: str) -> str:
    try:
        return send(command)
    except SystemExit as exc:
        raise RuntimeError(str(exc)) from exc


def _shot(name: str) -> Path:
    try:
        return Path(shot(name))
    except SystemExit as exc:
        raise RuntimeError(str(exc)) from exc


def _changed_pixels(first: Path, second: Path) -> int:
    try:
        before = np.asarray(Image.open(first).convert("RGB"), dtype=int)
        after = np.asarray(Image.open(second).convert("RGB"), dtype=int)
    except (FileNotFoundError, OSError):
        return 0
    return int((np.abs(before - after).sum(axis=2) > 20).sum())


def main(arguments: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if arguments is None else arguments)
    if not args or len(args) > 5:
        raise ValueError(
            "usage: ahide_check.py <actor_id_hex> <cam_dist> <label> "
            "[elevation_deg] [max_instances]"
        )
    actor_id = args[0]
    distance = args[1] if len(args) > 1 else "200"
    label = args[2] if len(args) > 2 else f"a{actor_id}"
    elevation = args[3] if len(args) > 3 else "0"
    maximum = int(args[4]) if len(args) > 4 else 6
    response = _send("autostate")
    if not response or response == "(no reply)":
        raise RuntimeError(
            "game REPL is not answering — SEARCHED NOTHING "
            "(start it with tools/zelda3d_game.py)"
        )

    best = 0
    best_index = -1
    tried = 0
    for index in range(maximum):
        _send("ahide 0")
        _send("freeze 0")
        selection = _send(f"asel {actor_id} {index}")
        if "no match" in selection:
            break
        tried += 1
        _send(f"acam {distance} 0 {elevation}")
        _send("settle 12")
        time.sleep(1)
        before = _shot(f"ac_{label}_on")
        time.sleep(1)
        _send("ahide 1")
        _send("settle 5")
        time.sleep(1)
        after = _shot(f"ac_{label}_off")
        time.sleep(1)
        changed = _changed_pixels(before, after)
        if changed > best:
            best = changed
            best_index = index
        if best > 200:
            break
    _send("ahide 0")
    if best > 200:
        print(
            f"  {label}: DRAWS ({best} px, instance {best_index} "
            f"of {tried} live instances)"
        )
        return 0
    if tried == 0:
        print(
            f"  {label}: NO LIVE INSTANCE of actor {actor_id} in this scene "
            "-- 0 instances examined, NOTHING TESTED."
        )
        print("        Warp somewhere the actor spawns; do not read this as evidence either way.")
        return 3
    print(
        f"  {label}: no contribution found across {tried} live instance(s), "
        f"best={best} px (threshold 200)"
    )
    print(
        "        INCONCLUSIVE unless autostate shows state=2 AND the prop is a "
        "closed volume framed"
    )
    print(
        "        head-on (flat props need elev/orbit). Blind spots: occlusion, "
        "off-screen, edge-on planes."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
