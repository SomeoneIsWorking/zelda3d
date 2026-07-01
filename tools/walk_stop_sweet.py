#!/usr/bin/env python3
"""walk_stop_sweet.py — derive the CSAB-space walk-STOP sweet spots offline.

The walk-stop port (zelda3d_anim.cpp, oot3d-decomp player_anim_states.md §6e) cross-fades the frozen
`nml_walk_free` leg pose (at free-run frame φ) into `nml_walk_end{R,L}_free` @ frame 0. OoT3D's
FUN_002be4c4 picks endR/endL + a morph length from the *leg-phase* (player+0x2254), but Zelda3D's φ is
a free-run CSAB frame that differs from the leg-phase by a CONSTANT OFFSET K (§6d/§6e STATUS). So the
port's sweet-spot math (computed from φ as if K=0) does NOT land where walk_free@φ == walk_end@0 → the
residual pop.

This tool measures the sweet spots DIRECTLY in CSAB-frame units, from Zelda3D's OWN rig geometry (no
oracle): for each end anim it finds φ* = the nml_walk_free frame whose per-bone LOCAL pose best matches
nml_walk_end{R,L}_free @ frame 0. That φ* is the frame at which morphFrames should be 0 (instant). The
walk-stop block then chooses endR/endL by which φ* is nearer the live φ (mod 29) and scales morphFrames
by the cyclic distance to that φ*, keeping OoT3D's length slope.

Cross-check: §6d proved nml_walk_free@frame matches the oracle, so the offline local pose at frame F
must equal a live skindump@F (run with --skindump to compare). Geometry + math reuse cmb.py/csab.py and
parity_pose_diff.py (same childlink_v2 rig, geodesic-angle, parent-relative local rotation).
"""
from __future__ import annotations
import sys, os, math, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from ctr_romfs import CtrRom
from zar import Zar
from parity_pose_diff import PARENT, LABEL, parse_bones

ZAR = os.environ.get("ZELDA3D_LINK_ZAR", "/actor/zelda_link_child_new.zar")
CMB = os.environ.get("ZELDA3D_LINK_CMB", "child/model/childlink_v2.cmb")
WALK = "child/anim/nml_walk_free.csab"
ENDR = "boy/anim/nml_walk_endR_free.csab"
ENDL = "boy/anim/nml_walk_endL_free.csab"


def _ortho(M):
    """Nearest rotation matrix (3x3) to the top-left 3x3 of a 4x4, via Gram-Schmidt-free polar
    using csab/cmb 4x4s. Small helper avoids a numpy dep here (matches parity_pose_diff.ortho)."""
    # Extract 3x3
    R = [[M[i][j] for j in range(3)] for i in range(3)]
    # Gram-Schmidt orthonormalization (rig matrices carry near-unit, possibly-scaled rotation)
    def norm(v):
        n = math.sqrt(sum(c * c for c in v)) or 1.0
        return [c / n for c in v]
    def dot(a, b): return sum(a[i] * b[i] for i in range(3))
    def sub(a, b, k): return [a[i] - k * b[i] for i in range(3)]
    x = norm([R[0][0], R[1][0], R[2][0]])           # col0
    y = [R[0][1], R[1][1], R[2][1]]
    y = norm(sub(y, x, dot(y, x)))                    # col1 _|_ col0
    z = [x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]]  # col2 = x×y
    return [[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]]


def _matT_mul(A3, B3):
    """A3ᵀ · B3 (both 3x3 row-major lists)."""
    out = [[0.0] * 3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            out[i][j] = sum(A3[k][i] * B3[k][j] for k in range(3))
    return out


def _geo_angle(Ra, Rb):
    """Geodesic angle (deg) between two 3x3 rotation matrices: acos((tr(Raᵀ·Rb)-1)/2)."""
    M = _matT_mul(Ra, Rb)
    c = (M[0][0] + M[1][1] + M[2][2] - 1.0) * 0.5
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


def local_rotations(model, csab, frame, bones):
    """{bone: R_local(3x3)} at `frame`. R_local = R_parentᵀ · R_bone from animated WORLD matrices,
    exactly as parity_pose_diff.load_soh_local derives it from a skindump (so the two are comparable)."""
    aw = A.animated_bone_world(model, csab, frame)  # bone_id -> 4x4 world
    Rw = {b: _ortho(aw[b]) for b in aw}
    out = {}
    for b in bones:
        if b not in Rw:
            continue
        p = PARENT[b] if b < len(PARENT) else -1
        out[b] = Rw[b] if (p < 0 or p not in Rw) else _matT_mul(Rw[p], Rw[b])
    return out


def mean_angle(pa, pb, bones):
    common = [b for b in bones if b in pa and b in pb]
    if not common:
        return None, {}
    per = {b: _geo_angle(pa[b], pb[b]) for b in common}
    return sum(per.values()) / len(per), per


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bones", default="1-21", help="bone subset (default 1-21; excludes root + aux)")
    ap.add_argument("--legs", action="store_true", help="match on leg bones only (3-8) — the pop joints")
    ap.add_argument("--substep", type=float, default=0.25, help="φ search step in walk_free frames")
    ap.add_argument("--skindump", help="optional skindump CSV to cross-check offline pose vs live")
    args = ap.parse_args()
    bones = parse_bones("3-8") if args.legs else parse_bones(args.bones)

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(ZAR)))

    def load(name):
        cand = [f for f in z.files if f.name == name]
        if not cand:
            base = name.rsplit("/", 1)[-1]
            cand = [f for f in z.files if f.name.endswith("/" + base)]
        return A.Csab(z.read(cand[0]))

    model = C.Cmb(z.read([f for f in z.files if f.name == CMB][0]))
    walk = load(WALK)
    endR = load(ENDR)
    endL = load(ENDL)
    dur = walk.duration
    print(f"walk_free duration={dur}  endR={endR.duration} endL={endL.duration}  bones={sorted(bones)}")

    poseR0 = local_rotations(model, endR, 0.0, bones)
    poseL0 = local_rotations(model, endL, 0.0, bones)

    # sweep nml_walk_free over its [0, dur) frames; find argmin mean-angle to each end@0
    n = int(round(dur / args.substep))
    bestR = bestL = None  # (mean, phi, per)
    rows = []
    for i in range(n):
        phi = i * args.substep
        pw = local_rotations(model, walk, phi, bones)
        mR, perR = mean_angle(pw, poseR0, bones)
        mL, perL = mean_angle(pw, poseL0, bones)
        rows.append((phi, mR, mL))
        if bestR is None or mR < bestR[0]:
            bestR = (mR, phi, perR)
        if bestL is None or mL < bestL[0]:
            bestL = (mL, phi, perL)

    print("\nφ      meanΔ→endR@0   meanΔ→endL@0  (deg)")
    for phi, mR, mL in rows:
        if abs(phi - round(phi)) < 1e-6:  # print integer frames only (compact)
            mark = ""
            if abs(phi - bestR[1]) < args.substep / 2: mark += " <-φ_R"
            if abs(phi - bestL[1]) < args.substep / 2: mark += " <-φ_L"
            print(f"{phi:5.2f}   {mR:8.2f}      {mL:8.2f}{mark}")

    print(f"\nφ_R (endR sweet spot) = {bestR[1]:.2f}   meanΔ={bestR[0]:.2f}°")
    print(f"φ_L (endL sweet spot) = {bestL[1]:.2f}   meanΔ={bestL[0]:.2f}°")
    print("  worst bones at φ_R:", ", ".join(
        f"{b}({LABEL.get(b,'?')}) {bestR[2][b]:.1f}" for b in sorted(bestR[2], key=lambda b: -bestR[2][b])[:4]))
    print("  worst bones at φ_L:", ", ".join(
        f"{b}({LABEL.get(b,'?')}) {bestL[2][b]:.1f}" for b in sorted(bestL[2], key=lambda b: -bestL[2][b])[:4]))

    if args.skindump:
        # Cross-check: offline local pose @ integer frame F vs live skindump@F (§6d proved match).
        from parity_pose_diff import load_soh_local
        sd = load_soh_local(args.skindump, set(bones))
        print(f"\n[xcheck] skindump caps: {sorted(sd)[:8]}{'...' if len(sd) > 8 else ''}")
        # we don't know the cap→frame mapping; just report the best offline-frame match per cap
        for cap in sorted(sd):
            best = None
            for i in range(dur):
                pw = local_rotations(model, walk, float(i), bones)
                m, _ = mean_angle(pw, sd[cap], bones)
                if m is not None and (best is None or m < best[0]):
                    best = (m, i)
            if best:
                print(f"  cap {cap:4d}: best offline walk_free frame {best[1]:2d}  meanΔ={best[0]:.2f}°")


if __name__ == "__main__":
    main()
