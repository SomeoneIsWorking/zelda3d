#!/usr/bin/env python3
"""Derive the per-bone CONSTANT rest-frame correction for the N64->OoT3D Link retarget.

The "replace rotation" retarget (Zelda3D_UpdateAnimN64Mapped: OoT3D bone local rot := N64
jointTable rot) works only for bones whose OoT3D rest LOCAL frame coincides with the N64
limb's (legs/head). Grezzo re-rigged Link's spine/chest/arms with a DIVERGENT rest frame, so
"replace" bends the torso and twists the arms. For those bones we need a constant correction C:

    A_oot(t) = f(C, R_n64(t))            f in { replace:R, left:C·R, right:R·C, two-sided:P·R·Q^T }

derived QUANTITATIVELY (never eyeballed) from a KNOWN matched pair:
  - GROUND TRUTH A_oot(t) = the OoT3D _new CSAB (authored for the 25-bone childlink_v2 rig),
    sampled at the live phase (curFrame/animLength * csab.duration), same as the live phase-lock.
  - INPUT R_n64(t) = Link's LIVE blended jointTable for the SAME named anim
    (tools linkjointdump -> scratch/bin/link_joints.csv).
Both are Grezzo's port of the same motion, so a correct C drives the residual to ~0 AND is
CONSTANT across frames (low spread). The LEGS are the built-in check: they already work under
"replace", so their replace-residual must be small — if it isn't, the frame alignment is wrong.

Usage:
  . ./.env && python3 tools/zelda3d_link_retarget_derive.py scratch/bin/link_joints.csv
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
ANIMMAP = "Shipwright/soh/src/zelda3d/zelda3d_player_animmap.inc"

# OoT3D childlink_v2 bone -> N64 player limb (must mirror kLinkChildBoneMap in zelda3d.c).
# Here we map ALL anatomical bones (incl. the currently rest-pinned spine/chest/arms) so the
# tool can MEASURE what correction each divergent bone needs. -1 = no live joint.
# childlink_v2 rest hierarchy: b1=pelvis(up-translation), b2=leg reorient, b9=spine reorient,
# b10=chest segment, b11=neck/head; arms (b13/b17 clav -> upper -> fore -> hand) hang off b10.
# N64 has NO clavicle (3 arm limbs/side: upper,fore,hand) -> OoT3D clav stays at rest. OoT3D L
# side (+z) <- N64 +z limbs (13,14,15); R side (-z) <- N64 -z limbs (16,17,18). Legs/neck already
# work under "replace" (C=I); spine/arms are the divergent bones needing a correction C.
BONEMAP = {
    0: -1, 1: -1, 2: -1,           # root, pelvis(translation), leg reorient
    3: 6, 4: 7, 5: 8,              # L leg <- N64 limb 6,7,8
    6: 3, 7: 4, 8: 5,              # R leg <- N64 limb 3,4,5
    # ORACLE-VERIFIED spine (../../../../oot3d-decomp/docs/link_bone_map.md): OoT3D inserts an
    # EXTRA chest bone (b10) between the upper-pivot (b9=N64 UPPER) and the head/arms. Eyes+mouth
    # bind to b11 (=N64 HEAD), the cap tip to b12 (=N64 HAT). The chest b10 has NO N64 limb -> rest.
    # (The pre-2026-06-23 map shifted HEAD/HAT one bone too low — drove HEAD pitch onto the chest;
    #  that was the #8 head-down / #83 dialog-arms root cause.)
    9: 9, 10: -1, 11: 10, 12: 11,  # upper-pivot<-9(UPPER), chest(rest), head<-10(HEAD), hat<-11(HAT)
    13: -1, 14: 13, 15: 14, 16: 15,  # L clav(rest), upper<-13, fore<-14, hand<-15
    17: -1, 18: 16, 19: 17, 20: 18,  # R clav(rest), upper<-16, fore<-17, hand<-18
    21: -1, 22: -1, 23: -1, 24: -1,
}

def Rx(a): c,s=np.cos(a),np.sin(a); return np.array([[1,0,0],[0,c,-s],[0,s,c]])
def Ry(a): c,s=np.cos(a),np.sin(a); return np.array([[c,0,s],[0,1,0],[-s,0,c]])
def Rz(a): c,s=np.cos(a),np.sin(a); return np.array([[c,-s,0],[s,c,0],[0,0,1]])
def zyx(rx,ry,rz): return Rz(rz)@Ry(ry)@Rx(rx)   # csab.cpp / live: Rz*Ry*Rx

def ortho(M):
    """Nearest rotation matrix to M (SVD projection onto SO(3))."""
    U,_,Vt = np.linalg.svd(M)
    D = np.diag([1,1,np.sign(np.linalg.det(U@Vt))])
    return U@D@Vt

def mat_to_euler_zyx(R):
    """Inverse of zyx(): recover (rx,ry,rz) such that R = Rz(rz)Ry(ry)Rx(rx)."""
    sy = -R[2,0]
    sy = max(-1.0, min(1.0, sy))
    ry = np.arcsin(sy)
    if abs(R[2,0]) < 0.99999:
        rx = np.arctan2(R[2,1], R[2,2])
        rz = np.arctan2(R[1,0], R[0,0])
    else:  # gimbal lock
        rx = np.arctan2(-R[1,2], R[1,1]); rz = 0.0
    return rx,ry,rz

def load_animmap(path):
    m = {}
    if not os.path.exists(path): return m
    for ln in open(path):
        g = re.search(r'"([^"]+)"\s*,\s*"([^"]+)"', ln)
        if g: m[g.group(1)] = g.group(2)
    return m

def load_caps(path):
    """rows -> dict cap -> {'curFrame','animLength','anim', limb-> (x,y,z)}"""
    caps = {}
    for ln in open(path):
        ln = ln.strip()
        if not ln or ln.startswith('#') or ln.startswith('cap,'): continue
        p = ln.split(',')
        cap = int(p[0]); cf=float(p[1]); al=float(p[2]); anim=p[3]
        limb=int(p[4]); x,y,z=int(p[5]),int(p[6]),int(p[7])
        d = caps.setdefault(cap, {'curFrame':cf,'animLength':al,'anim':anim,'limbs':{}})
        d['limbs'][limb] = (x,y,z)
    return caps

def fit_two_sided(As, Rs, iters=50):
    """Fit constant P,Q minimising sum ||A - P R Q^T||. Alternating Procrustes."""
    Q = np.eye(3)
    for _ in range(iters):
        P = ortho(sum(Aa @ Q @ Rr.T for Aa,Rr in zip(As,Rs)))
        Q = ortho(sum((P@Rr).T @ Aa for Aa,Rr in zip(As,Rs)).T)
    res = np.mean([np.linalg.norm(Aa - P@Rr@Q.T) for Aa,Rr in zip(As,Rs)])
    return P, Q, res

def main():
    csv = sys.argv[1] if len(sys.argv)>1 else "scratch/bin/link_joints.csv"
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(ZAR)))
    model = C.Cmb(z.read([f for f in z.files if f.name == CMB][0]))
    byname = {f.name: f for f in z.files}
    base_to_file = {}
    for f in z.files:
        if f.name.endswith('.csab'):
            base_to_file.setdefault(os.path.basename(f.name)[:-5], f)
    animmap = load_animmap(ANIMMAP)
    csab_cache = {}
    def get_csab(n64base):
        cb = animmap.get(n64base)
        if cb is None: return None
        if cb in csab_cache: return csab_cache[cb]
        f = base_to_file.get(cb)
        c = A.Csab(z.read(f)) if f else None
        csab_cache[cb] = c
        return c

    caps = load_caps(csv)
    print(f"loaded {len(caps)} captured frames from {csv}")
    anims = {}
    for d in caps.values(): anims[d['anim']] = anims.get(d['anim'],0)+1
    for a,n in anims.items(): print(f"  {n:4d} frames  {a}  -> csab {animmap.get(a,'?')}")
    print()

    bones = {b.id: b for b in model.bones}
    # Gather (A_oot, R_n64) samples per bone, tagged by anim (for cross-validation).
    samples = {bid: [] for bid,lm in BONEMAP.items() if lm>=0}
    for cap,d in sorted(caps.items()):
        c = get_csab(d['anim'])
        if c is None: continue
        cf = (d['curFrame']/d['animLength']*c.duration) if d['animLength']>4 else d['curFrame']
        fr = A._anim_frame(c, cf)
        for bid,limb in BONEMAP.items():
            if limb<0 or bid not in bones: continue
            row = d['limbs'].get(limb+1)   # bonemap value m -> jointTable[1+m] -> dump limb (m+1)
            if row is None: continue
            Rn = zyx(row[0]*BINANG, row[1]*BINANG, row[2]*BINANG)
            node = c.node_for_bone(bid)
            (_s,_r,_t) = A.bone_local_trs(bones[bid], node, fr)
            Ao = zyx(_r[0],_r[1],_r[2])
            samples[bid].append((Ao, Rn, d['anim']))

    print("bone limb  n  | replace  left(res/spread)  right(res/spread)  twosided | left C (deg zyx)")
    results = {}
    for bid in sorted(samples):
        S = samples[bid]
        if not S: continue
        As=[s[0] for s in S]; Rs=[s[1] for s in S]
        rep = np.mean([np.linalg.norm(Aa-Rr) for Aa,Rr in zip(As,Rs)])
        # left: A = X R  -> X_t = A R^T
        Xl_t=[Aa@Rr.T for Aa,Rr in zip(As,Rs)]; Xl=ortho(sum(Xl_t))
        lres=np.mean([np.linalg.norm(Aa-Xl@Rr) for Aa,Rr in zip(As,Rs)])
        lspread=np.mean([np.linalg.norm(x-Xl) for x in Xl_t])
        # right: A = R X -> X_t = R^T A
        Xr_t=[Rr.T@Aa for Aa,Rr in zip(As,Rs)]; Xr=ortho(sum(Xr_t))
        rres=np.mean([np.linalg.norm(Aa-Rr@Xr) for Aa,Rr in zip(As,Rs)])
        rspread=np.mean([np.linalg.norm(x-Xr) for x in Xr_t])
        P,Q,tres = fit_two_sided(As,Rs)
        le=np.degrees(mat_to_euler_zyx(Xl))
        print(f"{bid:4d} {BONEMAP[bid]:4d} {len(S):3d} | {rep:6.3f}  {lres:5.3f}/{lspread:5.3f}     "
              f"{rres:5.3f}/{rspread:5.3f}     {tres:5.3f}  | ({le[0]:6.1f},{le[1]:6.1f},{le[2]:6.1f})")
        results[bid]=dict(rep=rep,left=Xl,lres=lres,lspread=lspread,right=Xr,rres=rres,
                          rspread=rspread,P=P,Q=Q,tres=tres,n=len(S))

    # --- EMIT the generated correction table ---------------------------------------------------
    # Keep legs (b3-8) and neck/head (b11) on PURE REPLACE (C=I) — they already match and the user
    # requires no leg/head regression. Apply the fitted per-bone constant correction only to the
    # DIVERGENT upper-body bones (spine b9/b10, arms b14-16/b18-20), choosing left (C·R) or right
    # (R·C) by whichever has the lower residual (both 3-DOF; two-sided is skipped to avoid overfit
    # to the low-motion idle data). modes: 0=rest(no live joint) 1=replace 2=left 3=right.
    REPLACE_SET = {3,4,5,6,7,8,11,12}  # legs + head(b11) + hat(b12): rest frames coincide w/ N64
    CORRECT_SET = {9,14,15,16,18,19,20}  # divergent spine-pivot(b9) + arms; chest b10 is now rest
    TWOSIDED_MARGIN = 0.10  # only use the 6-DOF two-sided fit if it beats the best 3-DOF by this
    out_inc = os.environ.get("ZELDA3D_LINK_BONECORR_INC",
                             "Shipwright/soh/src/zelda3d/zelda3d_link_bonecorr.inc")
    lines = []
    modename = {0:'rest',1:'replace',2:'left C·R',3:'right R·C',4:'two-sided C·R·C2'}
    print("\n--- emitted per-bone mode ---")
    for bid in range(25):
        limb = BONEMAP.get(bid, -1)
        mode = 0; Cm = np.eye(3); Cm2 = np.eye(3)
        if limb >= 0:
            if bid in CORRECT_SET and bid in results:
                r = results[bid]
                # best 3-DOF (left/right), then upgrade to two-sided only if clearly better.
                if r['lres'] <= r['rres']: mode, Cm = 2, r['left']
                else:                      mode, Cm = 3, r['right']
                if r['tres'] < min(r['lres'], r['rres']) - TWOSIDED_MARGIN:
                    mode, Cm, Cm2 = 4, r['P'], r['Q'].T  # A = P·R·Q^T  ->  C·R·C2, C2=Q^T
            else:
                mode = 1  # replace (legs, neck, or any mapped-but-not-divergent bone)
        flat = ",".join(f"{v:.6f}f" for v in Cm.flatten())
        flat2 = ",".join(f"{v:.6f}f" for v in Cm2.flatten())
        lines.append(f"    {{ {limb:3d}, {mode}, {{ {flat} }}, {{ {flat2} }} }}, // b{bid}")
        if limb >= 0:
            print(f"  b{bid:2d}<-limb{limb:2d}: mode={mode} ({modename[mode]})")
    with open(out_inc, "w") as f:
        f.write("// GENERATED by tools/zelda3d_link_retarget_derive.py — do not edit by hand.\n")
        f.write("// Per OoT3D childlink_v2 bone: { n64Limb, mode, C[9], C2[9] }. mode 0=rest, 1=replace\n")
        f.write("// (local rot := N64 jointTable rot), 2=left (C·R_n64), 3=right (R_n64·C), 4=two-sided\n")
        f.write("// (C·R_n64·C2). C/C2 are row-major 3x3 constant rest-frame corrections; identity unless\n")
        f.write("// used. Legs/neck stay on pure replace (verified, no regression); divergent spine/arms\n")
        f.write("// get a correction. Derived QUANTITATIVELY from the live N64 jointTable (idle+run) vs the\n")
        f.write("// OoT3D CSAB (best proxy for the authored pose); a residual floor is irreducible\n")
        f.write("// authoring noise (the N64 anim and OoT3D CSAB are independently authored).\n")
        f.write("static const Zelda3dBoneCorr kLinkChildBoneCorr[25] = {\n")
        f.write("\n".join(lines))
        f.write("\n};\n")
    print(f"\nwrote {out_inc}")

    # Cross-validation: fit left-C on anim #1 rows, test residual on anim #2 rows (and vice versa).
    print("\n--- cross-validation (fit left-C on anim A, test on anim B) ---")
    anim_list=list(anims)
    if len(anim_list)>=2:
        a1,a2=anim_list[0],anim_list[1]
        print(f"  A={a1}\n  B={a2}")
        print("bone | fitA->testB  fitB->testA")
        for bid in sorted(samples):
            S=samples[bid]
            SA=[(s[0],s[1]) for s in S if s[2]==a1]; SB=[(s[0],s[1]) for s in S if s[2]==a2]
            if not SA or not SB: continue
            XA=ortho(sum(Aa@Rr.T for Aa,Rr in SA)); XB=ortho(sum(Aa@Rr.T for Aa,Rr in SB))
            eAB=np.mean([np.linalg.norm(Aa-XA@Rr) for Aa,Rr in SB])
            eBA=np.mean([np.linalg.norm(Aa-XB@Rr) for Aa,Rr in SA])
            print(f"{bid:4d} |   {eAB:6.3f}      {eBA:6.3f}")

if __name__ == "__main__":
    main()
