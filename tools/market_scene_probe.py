#!/usr/bin/env python3
"""market_scene_probe.py — structured close-test for the scene-select fork.

For each (entrance, dayTime) in the input bracket, warp BOTH Zelda3D and the OoT3D oracle,
read back play->sceneNum from each, and emit one row per pair. This is the structured
signal for [[2026-07-02-market-day-parity-sweep]] finding #2 (Market Day/Night silent fork).

The two engines are considered a match iff sceneNum agrees for every (ent, dayTime). Prints
a summary line and exits nonzero on mismatch (usable as a red/green test).

Assumes: Zelda3D binary already built; Azahar oracle already booted (tools/oracle_boot.sh).

Usage:
    tools/market_scene_probe.py 0xB1 0x0000 0x2000 0x4555 0x6000 0x8000 0xC000 0xE000
"""
import os, re, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP = os.path.expanduser("~/repo/oot3d-decomp")


def soh_scene(ent_hex, day_hex):
    subprocess.run(
        f"ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh restart {ent_hex} {day_hex}",
        shell=True, cwd=REPO, capture_output=True, text=True, timeout=90,
    )
    time.sleep(4)
    r = subprocess.run(
        ["python3", "tools/zelda3d_repl.py", "cmd", "posinfo"],
        cwd=REPO, capture_output=True, text=True, timeout=15,
    )
    m = re.search(r"scene=0x([0-9a-fA-F]+)", r.stdout)
    return int(m.group(1), 16) if m else None


def oracle_scene(ent_hex, day_hex):
    r = subprocess.run(
        ["python3", "tools/link_ctl.py", "warp", ent_hex, day_hex],
        cwd=DECOMP, capture_output=True, text=True, timeout=30,
    )
    m = re.search(r"scene=(\d+)", r.stdout)
    return int(m.group(1)) if m else None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ent = sys.argv[1]
    times = sys.argv[2:]
    mismatches = 0
    print(f"# probe entrance={ent}")
    print(f"# {'dayTime':>8}  {'SoH':>6}  {'Oracle':>6}  match")
    for t in times:
        s = soh_scene(ent, t)
        o = oracle_scene(ent, t)
        ok = (s is not None and s == o)
        mismatches += 0 if ok else 1
        print(f"  {t:>8}  0x{s:04X}  0x{o:04X}  {'OK' if ok else 'MISMATCH'}"
              if (s is not None and o is not None)
              else f"  {t:>8}  {s!s:>6}  {o!s:>6}  READ_FAIL")
    print(f"# mismatches: {mismatches}/{len(times)}")
    sys.exit(1 if mismatches else 0)


if __name__ == "__main__":
    main()
