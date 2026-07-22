#!/usr/bin/env python3
"""parity_pose_diff.py — geometry-level Link pose parity: Zelda3D resolved pose vs the OoT3D oracle.

Compares per-bone LOCAL rotations (the pure, parent-relative anim output) of the SAME 25-bone
childlink_v2 rig between:
  Zelda3D : REPL `skindump` CSV (cap,anim,frame,bone,m0..m11) — aw = animated bone-WORLD matrix.
          We orthonormalize each bone's 3x3 and derive its LOCAL rotation R_local = R_parentᵀ·R_bone
          using the rig parent map, so it is comparable to the oracle's local rotation.
  oracle: per-bone CSV (cap,t_ms,bone,r0..r8) — the live jointTable LOCAL rotation.

Both rigs share the same childlink_v2 bind frame, so local rotations are directly comparable (no
alignment needed). For each oracle frame we find the Zelda3D frame with the lowest mean per-bone geodesic
angle (best phase match — the two playheads are not frame-locked), then report the per-bone divergence
and an overall verdict (median best mean-angle, degrees). High per-bone angle = that joint diverges
(e.g. static legs vs cycling legs = the #117 slide).

Usage:
  tools/parity_pose_diff.py --soh scratch/parity/soh_run.csv --oracle scratch/parity/oracle_run.csv
  tools/parity_pose_diff.py --soh ... --oracle ... --bones 1-21 --state run
"""
import argparse, csv, math, sys

try:
    import numpy as np
except ImportError:
    sys.exit("parity_pose_diff: needs numpy (pip install numpy)")

# childlink_v2 parent map + labels (from the CMB skeleton; see oot3d-decomp link_skel_live.py)
PARENT = [-1, 0, 1, 2, 3, 4, 2, 6, 7, 1, 9, 10, 11, 10, 13, 14, 15, 10, 17, 18, 19, 10, 0, 22, 22]
LABEL = {0: "root", 1: "waist", 2: "lower-pivot", 3: "thigh+X", 4: "shin+X", 5: "foot+X",
         6: "thigh-X", 7: "shin-X", 8: "foot-X", 9: "upper-pivot", 10: "chest", 11: "head",
         12: "hat", 13: "clav+X", 14: "shldr+X", 15: "elbow+X", 16: "hand+X", 17: "clav-X",
         18: "shldr-X", 19: "elbow-X", 20: "hand-X", 21: "sheath", 22: "root2", 23: "root2.a",
         24: "root2.b"}


def parse_bones(spec):
    out = set()
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-"); out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return sorted(out)


def ortho(R):
    """Nearest rotation matrix to a possibly-scaled 3x3 (polar via SVD)."""
    U, _, Vt = np.linalg.svd(R)
    M = U @ Vt
    if np.linalg.det(M) < 0:
        U[:, -1] *= -1; M = U @ Vt
    return M


def geo_angle(Ra, Rb):
    """Geodesic angle (deg) between two rotation matrices."""
    c = (np.trace(Ra.T @ Rb) - 1.0) * 0.5
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


def load_soh_local(path, bones):
    """skindump aw -> {cap: {bone: R_local(3x3)}}. R_local = R_parentᵀ · R_bone (orthonormalized)."""
    world = {}  # cap -> bone -> R_world(3x3)
    with open(path) as f:
        for row in csv.reader(f):
            if not row or row[0].startswith("#") or row[0] == "cap":
                continue
            if len(row) < 16:  # truncated row (partial write on a frame boundary) — skip
                continue
            cap = int(row[0]); bone = int(row[3])
            m = [float(x) for x in row[4:16]]  # m0..m11 (row-major top 3 rows)
            R = ortho(np.array([[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]))
            world.setdefault(cap, {})[bone] = R
    out = {}
    for cap, bw in world.items():
        for b in bones:
            if b not in bw:
                continue
            p = PARENT[b] if b < len(PARENT) else -1
            Rl = bw[b] if (p < 0 or p not in bw) else bw[p].T @ bw[b]
            out.setdefault(cap, {})[b] = Rl
    return out


def load_oracle_local(path, bones):
    """oracle CSV -> {cap: {bone: R_local(3x3)}} (already local)."""
    out = {}
    with open(path) as f:
        for row in csv.reader(f):
            if not row or row[0] == "cap":
                continue
            cap = int(row[0]); bone = int(row[2])
            if bone not in bones:
                continue
            r = [float(x) for x in row[3:12]]
            out.setdefault(cap, {})[bone] = np.array([r[0:3], r[3:6], r[6:9]])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--soh", required=True)
    ap.add_argument("--oracle", required=True)
    ap.add_argument("--bones", default="1-21", help="bone subset (default 1-21; excludes root + aux)")
    ap.add_argument("--state", default="?", help="state label for the report")
    args = ap.parse_args()
    bones = parse_bones(args.bones)

    soh = load_soh_local(args.soh, set(bones))
    ora = load_oracle_local(args.oracle, set(bones))
    if not soh or not ora:
        sys.exit(f"parity_pose_diff: empty capture (soh={len(soh)} oracle={len(ora)} frames)")

    print(f"== parity [{args.state}]  soh:{len(soh)}f  oracle:{len(ora)}f  bones:{args.bones} "
          f"(per-bone LOCAL rotation, deg) ==")
    best_means = []
    worst_bone = {}
    for ocap in sorted(ora):
        of = ora[ocap]
        best = None  # (mean_deg, scap, {bone: deg})
        for scap in sorted(soh):
            sf = soh[scap]
            common = [b for b in bones if b in of and b in sf]
            if len(common) < 4:
                continue
            per = {b: geo_angle(of[b], sf[b]) for b in common}
            mean = sum(per.values()) / len(per)
            if best is None or mean < best[0]:
                best = (mean, scap, per)
        if best is None:
            continue
        mean, scap, per = best
        best_means.append(mean)
        for b, d in per.items():
            worst_bone[b] = max(worst_bone.get(b, 0.0), d)

    med = float(np.median(best_means)) if best_means else float("nan")
    verdict = "MATCH" if med < 8 else "DIVERGENT" if med > 20 else "marginal"
    print(f"  MEDIAN best mean-angle = {med:.1f} deg   ({verdict})")
    print("  worst per-bone divergence (deg, across frames):")
    for b in sorted(worst_bone, key=lambda b: -worst_bone[b])[:10]:
        print(f"    bone {b:2d} {LABEL.get(b,'?'):10s} {worst_bone[b]:6.1f}")


if __name__ == "__main__":
    main()
