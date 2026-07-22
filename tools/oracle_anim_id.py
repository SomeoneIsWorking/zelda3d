#!/usr/bin/env python3
"""oracle_anim_id.py — identify, per oracle frame, which Zelda3D CSAB pose best matches it.

Loads an oracle jointTable capture (per-bone LOCAL-rotation CSV) and, for each
captured frame, finds the (anim, frame) among a set of offline-sampled CSABs (nml_walk_free,
nml_walk_endR_free, nml_walk_endL_free) whose per-bone leg pose is closest (mean geodesic angle).

Purpose: empirically determine what OoT3D child Link actually plays through a walk-STOP — does it ride
nml_walk_free then jump to an end anim, and at what phase? Grounds the §6e walk-stop port in observed
behavior rather than assuming the loaded (adult boy/) end CSAB is the one OoT3D uses on the child rig.
"""
from __future__ import annotations
import sys, os, csv, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from ctr_romfs import CtrRom
from zar import Zar
from parity_pose_diff import PARENT, parse_bones, load_oracle_local
from walk_stop_sweet import local_rotations, mean_angle, ZAR, CMB, WALK, ENDR, ENDL


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--oracle", required=True)
    ap.add_argument("--bones", default="3-8", help="bone subset for matching (default legs 3-8)")
    ap.add_argument("--substep", type=float, default=0.5)
    args = ap.parse_args()
    bones = parse_bones(args.bones)

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(ZAR)))

    def load(name):
        cand = [f for f in z.files if f.name == name] or \
               [f for f in z.files if f.name.endswith("/" + name.rsplit("/", 1)[-1])]
        return A.Csab(z.read(cand[0]))

    model = C.Cmb(z.read([f for f in z.files if f.name == CMB][0]))
    anims = {"walk_free": load(WALK), "endR": load(ENDR), "endL": load(ENDL)}

    # pre-sample each anim over its frames
    samples = []  # (anim, frame, {bone:R})
    for nm, cs in anims.items():
        n = int(round(cs.duration / args.substep))
        for i in range(n):
            fr = i * args.substep
            samples.append((nm, fr, local_rotations(model, cs, fr, bones)))

    ora = load_oracle_local(args.oracle, set(bones))
    print(f"oracle frames: {len(ora)}   matching on bones {bones}")
    print("cap  best-anim       @frame   meanΔ(deg)")
    prev = None
    for cap in sorted(ora):
        of = ora[cap]
        best = None
        for nm, fr, sp in samples:
            m, _ = mean_angle(sp, of, bones)
            if m is not None and (best is None or m < best[0]):
                best = (m, nm, fr)
        m, nm, fr = best
        mark = "  <== anim change" if prev and prev != nm else ""
        prev = nm
        print(f"{cap:3d}  {nm:12s}  {fr:5.1f}   {m:6.2f}{mark}")


if __name__ == "__main__":
    main()
