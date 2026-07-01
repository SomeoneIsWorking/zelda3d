#!/usr/bin/env python3
"""zelda3d_terrain_diff.py — characterize the OoT3D-render vs N64-collision floor mismatch.

Input: a CSV of N64 floor heights produced by the in-game REPL `floorgrid` command
(authoritative — it raycasts SoH's BgCheck, the surface Link actually stands on). For
each sampled (x,z) we compute the OoT3D room-mesh floor (same raycast as zelda3d_floor.py,
the surface Zelda3D *draws*) and report delta = OoT3D - N64.

This validates the terrain-warp approach BEFORE wiring it: if delta is a smooth, mostly
zero field with localized bumps (OoT3D reshaped a few areas), a per-(x,z) vertical warp
of delta will snap the drawn ground to the N64 floor while preserving cliff/mountain
relief. If delta is wild noise, the approach is wrong.

Usage:
  ZELDA3D_OOT3D_ROM=<rom> tools/zelda3d_terrain_diff.py /scene/spot01_0_info.zsi scratch/floor/n64_spot01.csv
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zsi as zsimod
import cmb as cmbmod
from ctr_romfs import CtrRom


def load_tris(path):
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = zsimod.Zsi(rom.read(rom.get(path)))
    m = cmbmod.Cmb(z.cmb_bytes())
    tris = []
    for _, _, tri in m.triangles():
        p0, p1, p2 = tri[0][1], tri[1][1], tri[2][1]
        minx = min(p0[0], p1[0], p2[0]); maxx = max(p0[0], p1[0], p2[0])
        minz = min(p0[2], p1[2], p2[2]); maxz = max(p0[2], p1[2], p2[2])
        tris.append((p0, p1, p2, minx, maxx, minz, maxz))
    return tris


def oot3d_floor(x, z, tris, target=None):
    """Upward-facing (floor) triangle Y at (x,z), or None. If target is given, return
    the floor hit whose Y is CLOSEST to target (isolates the same surface across the two
    datasets, avoiding building-roof vs ground confounding); else the topmost floor."""
    best = None
    for p0, p1, p2, minx, maxx, minz, maxz in tris:
        if x < minx or x > maxx or z < minz or z > maxz:
            continue
        ax, az = p0[0], p0[2]; bx, bz = p1[0], p1[2]; cx, cz = p2[0], p2[2]
        d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
        if abs(d) < 1e-9:
            continue
        u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d
        v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d
        w = 1.0 - u - v
        eps = 1e-4
        if u < -eps or v < -eps or w < -eps:
            continue
        y = u * p0[1] + v * p1[1] + w * p2[1]
        # normal.y for floor test
        ux, uy, uz = (p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2])
        vx, vy, vz = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
        ny = uz * vx - ux * vz
        nl = math.sqrt((uy * vz - uz * vy) ** 2 + ny ** 2 + (ux * vy - uy * vx) ** 2) or 1.0
        if ny / nl <= 0.5:  # not a floor
            continue
        if target is None:
            if best is None or y > best:
                best = y
        else:
            if best is None or abs(y - target) < abs(best - target):
                best = y
    return best


def main():
    scene = sys.argv[1]
    csv = sys.argv[2]
    tris = load_tris(scene)
    print(f"{scene}: {len(tris)} tris")

    rows = []  # (x,z,n64,oot3d,delta)
    n_nofloor = 0
    with open(csv) as f:
        next(f)  # header
        for ln in f:
            x, z, y, ny = ln.strip().split(",")
            if y == "nan":
                continue
            x = float(x); z = float(z); n64 = float(y)
            o = oot3d_floor(x, z, tris, target=n64)
            if o is None:
                n_nofloor += 1
                continue
            rows.append((x, z, n64, o, o - n64))

    deltas = [r[4] for r in rows]
    deltas.sort()
    n = len(deltas)
    print(f"sampled {n} cells with both floors ({n_nofloor} N64 cells had no OoT3D floor)")
    if not n:
        return
    mean = sum(deltas) / n
    p = lambda q: deltas[min(n - 1, int(q * n))]
    print(f"delta (OoT3D - N64): min={deltas[0]:+.1f} p10={p(.1):+.1f} median={p(.5):+.1f} "
          f"p90={p(.9):+.1f} max={deltas[-1]:+.1f} mean={mean:+.1f}")
    within = lambda t: sum(1 for d in deltas if abs(d) <= t)
    for t in (1, 2, 5, 10, 20, 50):
        print(f"  |delta| <= {t:3d}: {within(t):4d} ({100*within(t)/n:.0f}%)")
    # worst offenders
    rows.sort(key=lambda r: -abs(r[4]))
    print("worst cells (x,z): N64 -> OoT3D (delta)")
    for x, z, n64, o, d in rows[:12]:
        print(f"  ({x:7.0f},{z:7.0f})  {n64:7.1f} -> {o:7.1f}  ({d:+.1f})")


if __name__ == "__main__":
    main()
