#!/usr/bin/env python3
"""market_scene_probe.py — structured close-test for the scene-select fork.

For each (entrance, dayTime) in the input bracket, warp BOTH Zelda3D and the OoT3D oracle,
read back play->sceneNum from each, and emit one row per pair. This is the structured
signal for [[2026-07-02-market-day-parity-sweep]] finding #2 (Market Day/Night silent fork).

The two engines are considered a match iff sceneNum agrees for every (ent, dayTime). Prints
a summary line and exits nonzero on mismatch (usable as a red/green test).

Assumes: Zelda3D binary already built; the oracle is booted on demand by oracle_cache.warp().

Usage:
    tools/market_scene_probe.py 0xB1 0x0000 0x2000 0x4555 0x6000 0x8000 0xC000 0xE000
"""
import os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_cache

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def soh_scene(ent_hex, day_hex):
    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "restart", ent_hex, day_hex],
        env={**os.environ, "ZELDA3D_HEADLESS": "1"},
        cwd=REPO, capture_output=True, text=True, timeout=90,
    )
    time.sleep(4)
    r = subprocess.run(
        ["python3", "tools/zelda3d_repl.py", "cmd", "posinfo"],
        cwd=REPO, capture_output=True, text=True, timeout=15,
    )
    m = re.search(r"scene=0x([0-9a-fA-F]+)", r.stdout)
    return int(m.group(1), 16) if m else None


def oracle_scene(ent_hex, day_hex, refresh=False):
    """Read the oracle-loaded scene at (entrance, dayTime). Cached — same input returns
    the stored result unless refresh=True (see tools/oracle_cache.py)."""
    result = oracle_cache.warp(ent_hex, day_hex, refresh=refresh)
    return result.get("scene")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    args = sys.argv[1:]
    refresh = "--refresh" in args
    args = [a for a in args if a != "--refresh"]
    ent = args[0]
    times = args[1:]
    mismatches = 0
    print(f"# probe entrance={ent}")
    print(f"# {'dayTime':>8}  {'SoH':>6}  {'Oracle':>6}  match")
    for t in times:
        s = soh_scene(ent, t)
        o = oracle_scene(ent, t, refresh=refresh)
        ok = (s is not None and s == o)
        mismatches += 0 if ok else 1
        print(f"  {t:>8}  0x{s:04X}  0x{o:04X}  {'OK' if ok else 'MISMATCH'}"
              if (s is not None and o is not None)
              else f"  {t:>8}  {s!s:>6}  {o!s:>6}  READ_FAIL")
    print(f"# mismatches: {mismatches}/{len(times)}")
    sys.exit(1 if mismatches else 0)


if __name__ == "__main__":
    main()
