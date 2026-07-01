#!/usr/bin/env python3
"""Derive a per-shoulder mode-4 (C·R·C2) correction that satisfies BOTH Link's idle pose
AND the held-cucco CARRY pose for the N64->OoT3D Link retarget (#6).

Why this exists (the idle-only fit is insufficient):
  The shoulders (b14 left, b18 right) are mode-2 (C·R) in zelda3d_link_bonecorr.inc, hand-woven
  for #8 so IDLE looks right. But the N64 jointTable's two shoulders are a sagittal MIRROR at
  idle (L upper-arm Z ~ +180 deg, R ~ -180) and NOT a mirror in carry (BOTH ~ +20 deg). A
  single 3-DOF C tuned for the idle mirror therefore cannot also route the carry swing
  correctly for both arms — the left arm stays down in carry (observed, #6). A 6-DOF
  two-sided correction (C·R·C2) CAN satisfy two independent pose constraints, so we fit one
  that pins idle to its current (verified-good) output AND maps carry to OoT3D's own
  nml_carryB_wait CSAB pose (the ground truth: arms raised overhead in a V).

Constraints fitted per bone (Procrustes least-squares, tools/zelda3d_link_retarget_derive style):
  - IDLE:  C·R_idle·C2  ==  Corr_old(R_idle)     (preserve the #8 idle EXACTLY)
  - CARRY: C·R_carry·C2 ==  T_carry               (OoT3D nml_carryB_wait CSAB local rot)
  R_idle / R_carry are Link's LIVE blended jointTable (scratch/bin/idle.csv, carry.csv).

Usage:
  . ./.env && python3 tools/zelda3d_carry_corr_derive.py
Writes the tuned rows to scratch/bin/carry_corr_rows.inc for hand-merge into the table, and
prints the fit residual + the resulting carry/idle euler so the fit can be sanity-checked.
"""
import sys, os, re
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from ctr_romfs import CtrRom
from zar import Zar

BINANG = np.pi / 32768.0
ZAR = os.environ.get("ZELDA3D_LINK_ZAR", "/actor/zelda_link_child_new.zar")
CMB = os.environ.get("ZELDA3D_LINK_CMB", "child/model/childlink_v2.cmb")
CARRY_CSAB = "nml_carryB_wait"
IDLE_CSAB = "waitF_itemB_20f"
IDLE_FROM_CSAB = os.environ.get("IDLE_FROM_CSAB", "0") == "1"
BONECORR_INC = "Shipwright/soh/src/zelda3d/zelda3d_link_bonecorr.inc"

# OoT3D bone -> N64 player limb (mirror of BONEMAP in zelda3d_link_retarget_derive.py). dump limb = m+1.
ARM_BONES = {14: 13, 15: 14, 16: 15, 18: 16, 19: 17, 20: 18}  # b: n64 limb

def Rx(a): c,s=np.cos(a),np.sin(a); return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def Ry(a): c,s=np.cos(a),np.sin(a); return np.array([[c,0,s],[0,1,0],[-s,0,c]])
def Rz(a): c,s=np.cos(a),np.sin(a); return np.array([[c,-s,0],[s,c,0],[0,0,1]])
def zyx(rx,ry,rz): return Rz(rz)@Ry(ry)@Rx(rx)

def ortho(M):
    U,_,Vt = np.linalg.svd(M)
    D = np.diag([1,1,np.sign(np.linalg.det(U@Vt))])
    return U@D@Vt

def mat_to_euler_zyx(R):
    sy = max(-1.0, min(1.0, -R[2,0])); ry = np.arcsin(sy)
    if abs(R[2,0]) < 0.99999:
        rx = np.arctan2(R[2,1], R[2,2]); rz = np.arctan2(R[1,0], R[0,0])
    else:
        rx = np.arctan2(-R[1,2], R[1,1]); rz = 0.0
    return rx,ry,rz

def fit_two_sided(As, Rs, iters=200):
    """Fit constant P,Q minimising sum ||A - P R Q^T||. Alternating Procrustes."""
    Q = np.eye(3)
    for _ in range(iters):
        P = ortho(sum(Aa @ Q @ Rr.T for Aa,Rr in zip(As,Rs)))
        Q = ortho(sum((P@Rr).T @ Aa for Aa,Rr in zip(As,Rs)).T)
    res = np.mean([np.linalg.norm(Aa - P@Rr@Q.T) for Aa,Rr in zip(As,Rs)])
    return P, Q, res

def load_caps_mean(path):
    """Mean jointTable per limb across all captured frames (the pose is a steady hold)."""
    acc = {}
    for ln in open(path):
        ln = ln.strip()
        if not ln or ln.startswith('#') or ln.startswith('cap,'): continue
        p = ln.split(',')
        limb=int(p[4]); x,y,z=int(p[5]),int(p[6]),int(p[7])
        acc.setdefault(limb, []).append((x,y,z))
    return {limb: np.mean(v, axis=0) for limb,v in acc.items()}

def parse_corr_table(path):
    """Read the current { limb, mode, {C9}, {C2 9} } rows -> dict bone-> (limb,mode,C,C2)."""
    txt = open(path).read()
    rows = {}
    bid = 0
    for m in re.finditer(r'\{\s*(-?\d+),\s*(\d+),\s*\{([^}]*)\},\s*\{([^}]*)\}\s*\}', txt):
        limb=int(m.group(1)); mode=int(m.group(2))
        c=[float(x.strip().rstrip('f')) for x in m.group(3).split(',')]
        c2=[float(x.strip().rstrip('f')) for x in m.group(4).split(',')]
        rows[bid]=(limb,mode,np.array(c).reshape(3,3),np.array(c2).reshape(3,3))
        bid+=1
    return rows

def apply_corr(mode, Cm, Cm2, R):
    if mode==1: return R
    if mode==2: return Cm@R
    if mode==3: return R@Cm
    if mode==4: return Cm@R@Cm2
    return R

def main():
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(ZAR)))
    model = C.Cmb(z.read([f for f in z.files if f.name == CMB][0]))
    bones = {b.id: b for b in model.bones}
    base_to_file = {os.path.basename(f.name)[:-5]: f for f in z.files if f.name.endswith('.csab')}
    carry = A.Csab(z.read(base_to_file[CARRY_CSAB]))
    idleC = A.Csab(z.read(base_to_file[IDLE_CSAB]))

    idle = load_caps_mean("scratch/bin/idle.csv")
    cary = load_caps_mean("scratch/bin/carry.csv")
    cur = parse_corr_table(BONECORR_INC)

    # carryB_wait is a steady hold; sample its first frame.
    fr = A._anim_frame(carry, 0.0)
    print(f"carryB_wait duration={carry.duration}")
    print(f"{'bone':>4} {'limb':>4} | {'res':>6} | idle-euler(zyx deg)      carry-euler(target)      carry-euler(fit)")
    out = []
    for bid, n64limb in sorted(ARM_BONES.items()):
        limb_dump = n64limb + 1
        R_idle = zyx(*(idle[limb_dump]*BINANG))
        R_carry = zyx(*(cary[limb_dump]*BINANG))
        # idle target = current correction applied to idle R (preserve #8 exactly)
        _,mode_old,C_old,C2_old = cur[bid]
        if IDLE_FROM_CSAB:
            ni = idleC.node_for_bone(bid); (_si,_ri,_ti)=A.bone_local_trs(bones[bid], ni, A._anim_frame(idleC,0.0))
            T_idle = zyx(_ri[0],_ri[1],_ri[2])
        else:
            T_idle = apply_corr(mode_old, C_old, C2_old, R_idle)
        # carry target = OoT3D's own carryB_wait CSAB local rotation for this bone
        node = carry.node_for_bone(bid)
        (_s,_r,_t) = A.bone_local_trs(bones[bid], node, fr)
        T_carry = zyx(_r[0],_r[1],_r[2])
        # fit C,C2 over the two pose constraints
        P,Q,res = fit_two_sided([T_idle,T_carry],[R_idle,R_carry])
        C2 = Q.T
        fit_carry = P@R_carry@C2
        ie=np.degrees(mat_to_euler_zyx(T_idle)); tce=np.degrees(mat_to_euler_zyx(T_carry))
        fce=np.degrees(mat_to_euler_zyx(fit_carry))
        print(f"{bid:4d} {n64limb:4d} | {res:6.3f} | ({ie[0]:6.1f},{ie[1]:6.1f},{ie[2]:6.1f})  "
              f"({tce[0]:6.1f},{tce[1]:6.1f},{tce[2]:6.1f})  ({fce[0]:6.1f},{fce[1]:6.1f},{fce[2]:6.1f})")
        flat=",".join(f"{v:.6f}f" for v in P.flatten())
        flat2=",".join(f"{v:.6f}f" for v in C2.flatten())
        out.append(f"    {{ {n64limb:3d}, 4, {{ {flat} }}, {{ {flat2} }} }}, // b{bid} CARRY-FIT (idle+carryB_wait)")
    os.makedirs("scratch/bin", exist_ok=True)
    with open("scratch/bin/carry_corr_rows.inc","w") as f:
        f.write("\n".join(out)+"\n")
    print("\nwrote scratch/bin/carry_corr_rows.inc")

if __name__ == "__main__":
    main()
