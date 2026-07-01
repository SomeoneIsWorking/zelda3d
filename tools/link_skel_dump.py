#!/usr/bin/env python3
"""Dump the OoT3D child-Link CMB skeleton: per-bone hierarchy, rest rot/trans, and the
computed bind-pose WORLD position of each bone origin. The world position lets us label
each bone geometrically (head = highest, feet = lowest Y, hands = ends of the arm chains)
so the N64-limb <-> OoT3D-bone correspondence can be VERIFIED, not guessed.

Usage: link_skel_dump.py [/actor/zelda_link_child_new.zar]
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(__file__))
from ctr_romfs import CtrRom
from zar import Zar
import cmb as cmblib


def pick_body_cmb(zar):
    cmbs = [f for f in zar.files if f.name.lower().endswith(".cmb")]
    cmbs.sort(key=lambda f: f.size, reverse=True)
    return cmbs[0]


def main():
    zar_path = sys.argv[1] if len(sys.argv) > 1 else "/actor/zelda_link_child_new.zar"
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(zar_path)))
    body = pick_body_cmb(z)
    c = cmblib.Cmb(z.read(body))
    print(f"# zar {zar_path}  CMB {c.name}  bones={len(c.bones)}")
    print(f"# {'bid':>3} {'par':>3}  {'rest rot (deg ZYX-stored x,y,z)':<34} "
          f"{'rest trans (x,y,z)':<28} {'WORLD pos (x,y,z)':<26} children")
    by_id = {b.id: b for b in c.bones}
    children = {b.id: [] for b in c.bones}
    for b in c.bones:
        if b.parent >= 0:
            children[b.parent].append(b.id)
    for b in c.bones:
        M = c.bone_matrix[b.id]
        wp = cmblib._mat_apply_pos(M, (0.0, 0.0, 0.0))
        rd = tuple(math.degrees(r) for r in b.rot)
        print(f"  {b.id:>3} {b.parent:>3}  "
              f"({rd[0]:8.2f},{rd[1]:8.2f},{rd[2]:8.2f})  "
              f"({b.trans[0]:7.1f},{b.trans[1]:7.1f},{b.trans[2]:7.1f})  "
              f"({wp[0]:7.1f},{wp[1]:7.1f},{wp[2]:7.1f})  {children[b.id]}")


if __name__ == "__main__":
    main()
