#!/usr/bin/env python3
"""oracle_carry_id.py — for an oracle carry-walk capture, identify per frame which CSAB+frame best
matches the LOWER body (legs, bones 3-8) and the UPPER body (arms/spine, bones 9-21) separately.

Reveals OoT3D's carry-walk composition: does the lower body play nml_walk_free / nml_run_free /
nml_carryB_free, and does the upper play nml_carryB_wait / nml_carryB_free? (the #117 question).
Reuses the deterministic CSAB sampler (csab.py via walk_stop_sweet.local_rotations) + the oracle CSV
loader (parity_pose_diff.load_oracle_local) — same machinery as the walk-stop work.
"""
import sys, os, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from zar import Zar
from ctr_romfs import CtrRom
from parity_pose_diff import PARENT, parse_bones, load_oracle_local
from walk_stop_sweet import local_rotations, mean_angle, ZAR, CMB

# child-rig CSAB candidates (paths under the link child zar)
CANDIDATES = {
    "walk_free":   "child/anim/nml_walk_free.csab",
    "run_free":    "child/anim/nml_run_free.csab",
    "carryB_free": "child/anim/nml_carryB_free.csab",
    "carryB":      "child/anim/nml_carryB.csab",
    "carryB_wait": "child/anim/nml_carryB_wait.csab",
}
FRAMES = {  # frame counts from csab_catalog (search the full clip)
    "walk_free": 29, "run_free": 20, "carryB_free": 17, "carryB": 36, "carryB_wait": 55,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oracle", required=True)
    ap.add_argument("--substep", type=float, default=0.5)
    ap.add_argument("--lower", default="3-8")
    ap.add_argument("--upper", default="9-21")
    args = ap.parse_args()

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(ZAR)))

    def load(name):
        cand = [f for f in z.files if f.name == name] or \
               [f for f in z.files if f.name.endswith("/" + name.rsplit("/", 1)[-1])]
        if not cand:
            return None
        return A.Csab(z.read(cand[0]))

    model = C.Cmb(z.read([f for f in z.files if f.name == CMB][0]))
    anims = {}
    for k, p in CANDIDATES.items():
        c = load(p)
        if c is None:
            print(f"  (missing CSAB {p})")
        anims[k] = c

    lower = parse_bones(args.lower)
    upper = parse_bones(args.upper)
    oracle = load_oracle_local(args.oracle, set(lower) | set(upper))   # {cap: {bone: R3x3}}

    def best(opose, bones, names):
        bb = (None, None, 1e9)
        for nm in names:
            c = anims.get(nm)
            if c is None:
                continue
            n = FRAMES[nm]
            f = 0.0
            while f < n:
                cp = local_rotations(model, c, f, bones)
                m, _ = mean_angle(opose, cp, bones)
                if m is not None and m < bb[2]:
                    bb = (nm, f, m)
                f += args.substep
        return bb

    print(f"frames={len(oracle)}  lower-bones={args.lower}  upper-bones={args.upper}")
    print(f"{'cap':>3} | {'LOWER best':>28} | {'UPPER best':>28}")
    lo_names = ["walk_free", "run_free", "carryB_free"]
    up_names = ["carryB_wait", "carryB_free", "carryB", "walk_free"]
    for cap in sorted(oracle):
        op = oracle[cap]
        lo = best(op, lower, lo_names)
        up = best(op, upper, up_names)
        print(f"{cap:>3} | {lo[0]:>12}@{lo[1]:5.1f} ({lo[2]:5.1f}°) | "
              f"{up[0]:>12}@{up[1]:5.1f} ({up[2]:5.1f}°)")


if __name__ == "__main__":
    main()
