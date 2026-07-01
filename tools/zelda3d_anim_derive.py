#!/usr/bin/env python3
"""Derive the N64->OoT3D per-limb rotation convention QUANTITATIVELY.

The N64-animation port (Zelda3D_UpdateAnimN64) drives the OoT3D geldwoman skeleton from
En_Ge1's live SkelAnime jointTable. The pose comes out contorted because the per-limb
rotation CONVENTION (euler order / axis sign / axis remap / whether the N64 rotation
composes with or replaces the CMB rest rotation) is unknown. Instead of guessing rotation
orders, this solves it numerically:

  TARGET (known-correct OoT3D local rotation per bone) = the CSAB ge1_s_wait animated
  rotation block M_c = Rz(rcz)Ry(rcy)Rx(rcx) (csab.cpp REPLACES rest rot with the track
  value when present; this is the pose `n64anim 0` renders correctly).

  INPUT = the live N64 jointTable rotations (tools jointdump) for the SAME idle, binang.

For every candidate formula M_n(n64, rest) we measure the Frobenius distance to M_c summed
over all bones; the convention that matches across ALL bones is the answer. ge1_s_wait and
the N64 En_Ge1 idle should be Grezzo's port of the same standing idle, so a correct
convention drives the residual to ~0.

Usage:
  python3 tools/zelda3d_anim_derive.py scratch/bin/ge1_joints.csv [csab_frame]
  (ZELDA3D_OOT3D_ROM must be set; reads /actor/zelda_ge1.zar : Model/geldwoman.cmb + ge1_s_wait)
"""
import sys, os, math, itertools
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from ctr_romfs import CtrRom
from zar import Zar

BINANG = math.pi / 32768.0

def Rx(a): c,s=math.cos(a),math.sin(a); return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def Ry(a): c,s=math.cos(a),math.sin(a); return np.array([[c,0,s],[0,1,0],[-s,0,c]])
def Rz(a): c,s=math.cos(a),math.sin(a); return np.array([[c,-s,0],[s,c,0],[0,0,1]])
RAX = {'x':Rx,'y':Ry,'z':Rz}

def euler(order, ax, ay, az):
    """Product of rotations in the given multiplication order, e.g. 'zyx' = Rz·Ry·Rx."""
    ang = {'x':ax,'y':ay,'z':az}
    M = np.eye(3)
    for axis in order:
        M = M @ RAX[axis](ang[axis])
    return M

def load_jointdump(path):
    rots = {}  # limb index -> (x,y,z) binang
    meta = ""
    with open(path) as f:
        for ln in f:
            ln=ln.strip()
            if ln.startswith('#'): meta = ln; continue
            if ln.startswith('idx') or not ln: continue
            i,x,y,z = [int(v) for v in ln.split(',')]
            rots[i] = (x,y,z)
    return rots, meta

def main():
    jpath = sys.argv[1]
    frame = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
    zar_path = os.environ.get("ZELDA3D_XCHECK_ZAR", "/actor/zelda_ge1.zar")
    cmb_name = os.environ.get("ZELDA3D_XCHECK_CMB", "Model/geldwoman.cmb")

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(zar_path)))
    model = C.Cmb(z.read([f for f in z.files if f.name == cmb_name][0]))
    csab = A.Csab(z.read([f for f in z.files if f.name == "Anim/ge1_s_wait.csab"][0]))

    joints, meta = load_jointdump(jpath)
    print("jointdump:", meta)
    print(f"CSAB ge1_s_wait @ frame {frame}\n")

    bones = sorted(model.bones, key=lambda b: b.id)
    # Per bone gather: rest euler, csab target rotation M_c, n64 euler (binang->rad).
    # OoT3D bone i <- N64 limb (i+1) => joints[i+1].
    data = []
    print("bone | rest(deg xyz)        | csab(deg xyz)        | n64 limb (deg xyz)")
    for b in bones:
        node = csab.node_for_bone(b.id)
        (_s,_r,_t) = A.bone_local_trs(b, node, A._anim_frame(csab, frame))
        Mc = euler('zyx', _r[0], _r[1], _r[2])          # csab.cpp: Rz·Ry·Rx
        Rrest = euler('zyx', b.rot[0], b.rot[1], b.rot[2])
        n = joints.get(b.id + 1, (0,0,0))
        nrad = (n[0]*BINANG, n[1]*BINANG, n[2]*BINANG)
        data.append((b.id, Mc, Rrest, nrad, b.rot, _r))
        d = lambda r: f"{math.degrees(r[0]):7.1f}{math.degrees(r[1]):7.1f}{math.degrees(r[2]):7.1f}"
        print(f"{b.id:4d} | {d(b.rot)} | {d(_r)} | {d(nrad)}")

    print("\n--- brute-force candidate conventions (Frobenius residual, lower=better) ---")
    orders = [''.join(p) for p in itertools.permutations('xyz')]
    signs = list(itertools.product((1,-1),repeat=3))
    # axis remap: which n64 angle (x/y/z) feeds the formula's x/y/z slot
    perms = list(itertools.permutations((0,1,2)))
    structs = ['replace','pre','post']  # M = R ; R·Rrest ; Rrest·R

    results = []
    for struct in structs:
        for order in orders:
            for perm in perms:
                for sg in signs:
                    total = 0.0
                    worst = 0.0
                    for (bid, Mc, Rrest, nrad, _, _) in data:
                        a = [sg[k]*nrad[perm[k]] for k in range(3)]
                        R = euler(order, a[0], a[1], a[2])
                        if struct=='pre':   M = R @ Rrest
                        elif struct=='post':M = Rrest @ R
                        else:               M = R
                        e = np.linalg.norm(M - Mc)
                        total += e; worst = max(worst, e)
                    results.append((total, worst, struct, order, perm, sg))
    results.sort(key=lambda r: r[0])
    print(" total  worst  struct  order  perm        signs")
    for r in results[:12]:
        print(f"{r[0]:7.2f}{r[1]:6.2f}  {r[2]:7s} {r[3]} {str(r[4]):11s} {r[5]}")

    # Detail the winner per bone
    best = results[0]
    print(f"\nBEST: struct={best[2]} order={best[3]} perm={best[4]} signs={best[5]}")
    print("per-bone residual:")
    _,_,struct,order,perm,sg = best
    for (bid, Mc, Rrest, nrad, _, _) in data:
        a = [sg[k]*nrad[perm[k]] for k in range(3)]
        R = euler(order, a[0], a[1], a[2])
        M = R if struct=='replace' else (R@Rrest if struct=='pre' else Rrest@R)
        print(f"  bone {bid:2d}: residual {np.linalg.norm(M-Mc):.4f}")

if __name__ == "__main__":
    main()
