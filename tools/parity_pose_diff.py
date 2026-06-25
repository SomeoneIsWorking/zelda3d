#!/usr/bin/env python3
"""parity_pose_diff.py — geometry-level Link pose parity: SoH3D resolved pose vs the OoT3D oracle.

Compares two per-frame bone-world-position captures of the SAME 25-bone childlink_v2 rig:
  SoH3D : REPL `skindump` CSV (cap,anim,frame,bone,m0..m11) — bone world pos = (m3,m7,m11) of aw=skin*bind
  oracle: tools/oracle_link_pose.py CSV (cap,t_ms,bone,x,y,z) — live bone world positions from Azahar

Because the two live in different coordinate frames (Link faces a different way, different unit scale,
different root placement), we PROCRUSTES-align each candidate frame pair — best-fit similarity transform
(scale + rotation + translation, Kabsch) over the bone point cloud — then the residual RMSD is the
pose mismatch, invariant to placement. Per-bone residual after alignment localizes WHICH bones diverge
(e.g. static legs vs cycling legs = the #117 slide).

Matching: for each oracle frame, find the SoH3D frame with the LOWEST aligned RMSD (the best phase
match), since the two playheads are not frame-locked. Reports the per-oracle-frame best RMSD + the
worst per-bone residuals, and an overall verdict (median best-RMSD) for the state.

Usage:
  tools/parity_pose_diff.py --soh scratch/parity/soh_idle.csv --oracle scratch/parity/oracle_idle.csv
  tools/parity_pose_diff.py --soh ... --oracle ... --bones 1-21   # restrict to meaningful bones
"""
import argparse, csv, sys

try:
    import numpy as np
except ImportError:
    sys.exit("parity_pose_diff: needs numpy (pip install numpy)")

# childlink_v2 body-part labels (from oot3d-decomp link_skel_live.py)
LABEL = {0: "root", 1: "waist", 2: "lower-pivot", 3: "thigh+X", 4: "shin+X", 5: "foot+X",
         6: "thigh-X", 7: "shin-X", 8: "foot-X", 9: "upper-pivot", 10: "chest", 11: "head",
         12: "hat", 13: "clav+X", 14: "shldr+X", 15: "elbow+X", 16: "hand+X", 17: "clav-X",
         18: "shldr-X", 19: "elbow-X", 20: "hand-X", 21: "sheath", 22: "root2", 23: "root2.a",
         24: "root2.b"}


def parse_bones(spec):
    out = set()
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return sorted(out)


def load_soh(path, bones):
    """-> {cap: {bone: (x,y,z)}} from skindump (bone world pos = m3,m7,m11)."""
    frames = {}
    with open(path) as f:
        for row in csv.reader(f):
            if not row or row[0].startswith("#") or row[0] == "cap":
                continue
            cap = int(row[0]); bone = int(row[3])
            if bone not in bones:
                continue
            m3, m7, m11 = float(row[7]), float(row[11]), float(row[15])
            frames.setdefault(cap, {})[bone] = (m3, m7, m11)
    return frames


def load_oracle(path, bones):
    """-> {cap: {bone: (x,y,z)}} from oracle_link_pose."""
    frames = {}
    with open(path) as f:
        for row in csv.reader(f):
            if not row or row[0] == "cap":
                continue
            cap = int(row[0]); bone = int(row[2])
            if bone not in bones:
                continue
            frames.setdefault(cap, {})[bone] = (float(row[3]), float(row[4]), float(row[5]))
    return frames


def procrustes(P, Q):
    """Best-fit similarity (scale s, rotation R, translation t) mapping P->Q (Umeyama).
    Returns (rmsd, per_point_residual[n])."""
    Pc = P - P.mean(0); Qc = Q - Q.mean(0)
    H = Pc.T @ Qc
    U, S, Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(Vt.T @ U.T))
    D = np.diag([1, 1, d])
    R = Vt.T @ D @ U.T
    varP = (Pc ** 2).sum()
    s = (S * np.array([1, 1, d])).sum() / varP if varP > 1e-9 else 1.0
    Pa = (s * (R @ Pc.T)).T  # aligned, centered
    resid = np.linalg.norm(Pa - Qc, axis=1)
    rmsd = float(np.sqrt((resid ** 2).mean()))
    return rmsd, resid


def common_matrix(frame_a, frame_b, bones):
    common = [b for b in bones if b in frame_a and b in frame_b]
    A = np.array([frame_a[b] for b in common])
    B = np.array([frame_b[b] for b in common])
    return common, A, B


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--soh", required=True)
    ap.add_argument("--oracle", required=True)
    ap.add_argument("--bones", default="1-21", help="bone subset (default 1-21; excludes root + aux)")
    ap.add_argument("--state", default="?", help="state label for the report")
    args = ap.parse_args()
    bones = parse_bones(args.bones)

    soh = load_soh(args.soh, set(bones))
    ora = load_oracle(args.oracle, set(bones))
    if not soh or not ora:
        sys.exit(f"parity_pose_diff: empty capture (soh={len(soh)} oracle={len(ora)} frames)")

    print(f"== parity [{args.state}]  soh:{len(soh)}f  oracle:{len(ora)}f  bones:{args.bones} ==")
    best_rmsds = []
    worst_bone_acc = {}
    # scale normalization: normalize each cloud by its own RMS radius so RMSD is in % of skeleton size
    for ocap in sorted(ora):
        of = ora[ocap]
        best = None
        for scap in sorted(soh):
            common, A, B = common_matrix(soh[scap], of, bones)
            if len(common) < 4:
                continue
            rmsd, resid = procrustes(A, B)
            # normalize by oracle skeleton radius (scale-free %)
            rad = np.linalg.norm(B - B.mean(0), axis=1).mean()
            pct = 100.0 * rmsd / rad if rad > 1e-6 else rmsd
            if best is None or pct < best[0]:
                best = (pct, scap, common, resid, rad)
        if best is None:
            continue
        pct, scap, common, resid, rad = best
        best_rmsds.append(pct)
        # accumulate worst per-bone residual (normalized)
        for b, r in zip(common, resid):
            worst_bone_acc[b] = max(worst_bone_acc.get(b, 0.0), 100.0 * r / rad)
        print(f"  oracle f{ocap:2d} -> soh f{scap:<3d}  bestRMSD={pct:6.2f}% of skeleton radius")

    med = float(np.median(best_rmsds)) if best_rmsds else float("nan")
    print(f"\n  MEDIAN best-RMSD = {med:.2f}%   "
          f"({'MATCH' if med < 6 else 'DIVERGENT' if med > 12 else 'marginal'})")
    print("  worst per-bone residual (normalized %):")
    for b in sorted(worst_bone_acc, key=lambda b: -worst_bone_acc[b])[:8]:
        print(f"    bone {b:2d} {LABEL.get(b,'?'):10s} {worst_bone_acc[b]:6.2f}%")


if __name__ == "__main__":
    main()
