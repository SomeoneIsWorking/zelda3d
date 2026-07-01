#!/usr/bin/env python3
"""pickup_pose_validate.py — geometric A/B for the #117 PICKUP (lift) fix.

For each captured live Zelda3D pose (a `skindump` row-group, tagged with the resolved CSAB name + the
real playhead frame), evaluate the SAME OoT3D clip OFFLINE at the SAME frame (deterministic csab.py
sampler) and report the per-bone geodesic angle between the rendered pose and the clip's geometry.

If the rendered lift pose == the clip (≈0°), the Zelda3D pickup faithfully plays nml_carryB_free across
its frames (the lift raise) and settles into nml_carryB_wait — proving the lift is no longer clobbered
to nml_wait_free by the carry-IDLE override (the #117 pickup bug). No screenshots; pure geometry.

Usage: tools/pickup_pose_validate.py scratch/pickup_skin.csv [--bones 1-21]
"""
import sys, os, csv, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from zar import Zar
from ctr_romfs import CtrRom
from parity_pose_diff import PARENT, parse_bones, ortho as _np_ortho
from walk_stop_sweet import local_rotations, mean_angle, _ortho, _matT_mul, ZAR, CMB
import numpy as np


def load_skin_local_tagged(path, bones):
    """skindump -> list of (cap, anim, frame, {bone: R_local}). Mirrors load_soh_local but keeps the
    per-cap anim/frame tags so each live pose can be compared to its OWN clip@frame."""
    world = {}   # cap -> bone -> R_world
    tag = {}     # cap -> (anim, frame)
    with open(path) as f:
        for row in csv.reader(f):
            if not row or row[0].startswith("#") or row[0] == "cap":
                continue
            if len(row) < 16:
                continue
            cap = int(row[0]); anim = row[1]; frame = float(row[2]); bone = int(row[3])
            m = [float(x) for x in row[4:16]]
            R = _ortho([[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]])
            world.setdefault(cap, {})[bone] = R
            tag[cap] = (anim, frame)
    out = []
    for cap in sorted(world):
        bw = world[cap]
        loc = {}
        for b in bones:
            if b not in bw:
                continue
            p = PARENT[b] if b < len(PARENT) else -1
            loc[b] = bw[b] if (p < 0 or p not in bw) else _matT_mul(bw[p], bw[b])
        out.append((cap, tag[cap][0], tag[cap][1], loc))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("skindump")
    ap.add_argument("--bones", default="1-21")
    args = ap.parse_args()
    bones = parse_bones(args.bones)

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(ZAR)))
    model = C.Cmb(z.read([f for f in z.files if f.name == CMB][0]))

    csab_cache = {}
    def load_csab(animname):
        if animname in csab_cache:
            return csab_cache[animname]
        base = animname if animname.endswith(".csab") else animname + ".csab"
        cand = [f for f in z.files if f.name.endswith("/" + base) or f.name.endswith("/" + base.rsplit("/", 1)[-1])]
        c = A.Csab(z.read(cand[0])) if cand else None
        csab_cache[animname] = c
        return c

    rows = load_skin_local_tagged(args.skindump, set(bones))
    print(f"{'cap':>4} {'anim':<24} {'frame':>7} {'meanDeg':>8} {'maxDeg':>7}")
    worst = {}
    for cap, anim, frame, live in rows:
        c = load_csab(anim)
        if c is None:
            print(f"{cap:>4} {anim:<24} {frame:>7.2f}   (clip not found)")
            continue
        # Loco/loop clips free-run the playhead PAST the clip duration (the live C++ csab wraps it
        # internally); wrap here so the offline sampler evaluates the same wrapped phase.
        dur = getattr(c, "duration", 0) or 0
        ev = frame % dur if dur > 0 else frame
        off = local_rotations(model, c, ev, bones)
        mean, per = mean_angle(live, off, bones)
        if mean is None:
            continue
        mx = max(per.values()); mxb = max(per, key=per.get)
        worst.setdefault(anim, []).append(mean)
        print(f"{cap:>4} {anim:<24} {frame:>7.2f} {mean:>8.2f} {mx:>7.2f} (b{mxb})")
    print("\n=== per-clip mean of per-cap mean geodesic angle (render vs offline clip) ===")
    for anim, ms in worst.items():
        print(f"  {anim:<24} n={len(ms):>3}  mean={sum(ms)/len(ms):.3f}°  max={max(ms):.3f}°")


if __name__ == "__main__":
    main()
