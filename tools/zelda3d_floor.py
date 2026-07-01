#!/usr/bin/env python3
"""zelda3d_floor.py — query the OoT3D room mesh floor height at world XZ point(s).

Loads a scene/room CMB (the SAME mesh Zelda3D renders), then for each (x,z) finds
the floor Y by intersecting a vertical ray with every triangle whose XZ-projection
contains the point. Reports the topmost walkable floor (normal.y > thr) plus all
hit Ys. Use to compare the OoT3D rendered ground against the N64 collision floor
(Link's landed world.pos.y from the REPL `posinfo`) and decide whether the
mismatch is a constant Y offset (cheap scene-offset fix) or local (collision work).

Usage:
  ZELDA3D_OOT3D_ROM=<rom> tools/zelda3d_floor.py /scene/spot01_0_info.zsi x z [x z ...]
  # or pairs "x,z[,n64floor]" to also print the delta vs a known N64 floor:
  ZELDA3D_OOT3D_ROM=<rom> tools/zelda3d_floor.py /scene/spot01_0_info.zsi -2451,1062,138
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zsi as zsimod
import cmb as cmbmod
from ctr_romfs import CtrRom


def tri_floor_y(x, z, tri):
    """If (x,z) is inside triangle tri's XZ projection, return (y, normal_y); else None.
    tri = [(idx,pos,nrm,uv) x3], pos=(X,Y,Z)."""
    p0, p1, p2 = tri[0][1], tri[1][1], tri[2][1]
    ax, az = p0[0], p0[2]
    bx, bz = p1[0], p1[2]
    cx, cz = p2[0], p2[2]
    # Barycentric in XZ plane.
    d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
    if abs(d) < 1e-9:
        return None
    u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d
    v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d
    w = 1.0 - u - v
    eps = 1e-4
    if u < -eps or v < -eps or w < -eps:
        return None
    y = u * p0[1] + v * p1[1] + w * p2[1]
    # Triangle normal (world).
    ux, uy, uz = (p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2])
    vx, vy, vz = (p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2])
    nx = uy * vz - uz * vy
    ny = uz * vx - ux * vz
    nz = ux * vy - uy * vx
    nlen = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
    return (y, ny / nlen)


def main():
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    path = sys.argv[1]
    z = zsimod.Zsi(rom.read(rom.get(path)))
    m = cmbmod.Cmb(z.cmb_bytes())
    tris = [tri for _, _, tri in m.triangles()]

    pts = []
    for a in sys.argv[2:]:
        if "," in a:
            f = [float(t) for t in a.split(",")]
            pts.append((f[0], f[1], f[2] if len(f) > 2 else None))
    # also accept bare "x z" pairs
    bare = [a for a in sys.argv[2:] if "," not in a]
    for i in range(0, len(bare) - 1, 2):
        pts.append((float(bare[i]), float(bare[i + 1]), None))

    print(f"{path}: {len(tris)} tris")
    for x, zc, n64 in pts:
        hits = []
        for tri in tris:
            r = tri_floor_y(x, zc, tri)
            if r is not None:
                hits.append(r)
        hits.sort(key=lambda h: -h[0])  # topmost first
        walk = [h for h in hits if h[1] > 0.5]  # upward-facing = floor
        topwalk = walk[0][0] if walk else None
        ys = ", ".join(f"{y:.0f}{'^' if ny>0.5 else ('v' if ny<-0.5 else '|')}" for y, ny in hits[:8])
        msg = f"  ({x:.0f},{zc:.0f}): hits=[{ys}]  topFloor={topwalk if topwalk is None else round(topwalk)}"
        if n64 is not None:
            if topwalk is not None:
                msg += f"  N64={n64:.0f}  delta(3DS-N64)={topwalk - n64:+.1f}"
            else:
                msg += f"  N64={n64:.0f}  (no 3DS floor hit)"
        print(msg)


if __name__ == "__main__":
    main()
