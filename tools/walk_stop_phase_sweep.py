#!/usr/bin/env python3
"""walk_stop_phase_sweep.py — sweep the walk-STOP across stop PHASES and report the worst per-logic-frame
pose jump. The §6e walk-stop pop is φ-DEPENDENT (it spikes only when the stop free-run phase φ lands
where the port's morphFrames is too short for the actual walk→walk_end pose gap), so a single stop is
not representative. This drives the live game: for each phase offset k it re-warps to Kokiri open ground,
walks, freezes, advances the walk φ by k frames (walkhold still held), releases, then samples the stop
per logic frame via posescan and reports the max per-bone jump (deg).

Usage: tools/walk_stop_phase_sweep.py [--offsets 0,2,4,...] [--steps 18] [--sleep 0.13]
Run with the game up (tools/zelda3d_game.py) and `link 1` set. Prints a per-offset table + the worst case.
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zelda3d_repl as R


def parse_dump(txt):
    """posescan dump -> list of (i, deg, bone, frame, csab)."""
    out = []
    for ln in txt.splitlines():
        p = ln.split(",")
        if len(p) == 5 and p[0].isdigit():
            out.append((int(p[0]), float(p[1]), int(p[2]), float(p[3]), p[4]))
    return out


def establish_walk(settle=2.5):
    R.send("freeze 0")
    R.send("warp 0xEE")
    time.sleep(settle)
    R.send("gcam 1")
    R.send("walkhold 600 0 32")
    time.sleep(0.4)
    st = R.send("linkanimstate")
    return "nml_walk_free" in st


def capture_stop(k, steps, sleep):
    R.send("freeze 1")
    for _ in range(k):
        R.send("step 1")            # advance walk φ (walkhold still held)
    R.send("posescan on")
    R.send("walkhold 0")            # release -> deceleration / walk_end
    for _ in range(steps):
        R.send("step 1")
        time.sleep(sleep)
    R.send("posescan off")
    return parse_dump(R.send("posescan dump", timeout=5.0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--offsets", default=",".join(str(i) for i in range(0, 29, 2)))
    ap.add_argument("--steps", type=int, default=18)
    ap.add_argument("--sleep", type=float, default=0.13)
    args = ap.parse_args()
    offsets = [int(x) for x in args.offsets.split(",")]

    print(f"phase-sweep: offsets={offsets} steps={args.steps}")
    print("  k   maxJump  bone  @endframe  endmaxJump  (end region only)")
    worst = None
    for k in offsets:
        for attempt in range(3):
            if establish_walk():
                break
            print(f"  k={k}: not walking (NPC wedge?) — retry {attempt+1}")
        rows = capture_stop(k, args.steps, args.sleep)
        if not rows:
            print(f"  k={k:2d}: (no data)")
            continue
        # overall max, and max within the walk_end region (the pop of interest)
        gmax = max(rows, key=lambda r: r[1])
        endrows = [r for r in rows if "walk_end" in r[4]]
        emax = max(endrows, key=lambda r: r[1]) if endrows else (None,) * 5
        print(f"  {k:2d}   {gmax[1]:6.1f}   b{gmax[2]:<3d} f{gmax[3]:<5.1f}   "
              f"{emax[1] if emax[1] is not None else float('nan'):6.1f}  ({emax[4] if emax[4] else '-'})")
        cand = emax if emax[1] is not None else gmax
        if worst is None or cand[1] > worst[1]:
            worst = (cand[1], k, cand[2], cand[4])
    if worst:
        print(f"\nWORST end-region jump = {worst[0]:.1f} deg at stop-offset k={worst[1]} "
              f"bone {worst[2]} ({worst[3]})")


if __name__ == "__main__":
    main()
