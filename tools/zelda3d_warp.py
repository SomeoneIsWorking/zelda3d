#!/usr/bin/env python3
"""zelda3d_warp.py — reference implementation + offline verification of the terrain warp.

Goal: make the OoT3D *render* mesh's walkable ground sit at the N64 *collision* floor
(so Link, who walks on N64 collision, stands on the visible ground) WITHOUT flattening
OoT3D's cliff/mountain relief. Approach: a per-XZ vertical displacement field

    D(x,z) = N64_floor(x,z) - OoT3D_floor(x,z)

built on a grid, with building/structure outliers rejected and hole-filled from nearby
ground, then bilinearly sampled per vertex: v.y += D(v.x, v.z). A whole vertical column
(ground + any building/cliff on it) shifts by the SAME local ground correction, so relief
is preserved; only the ground baseline is re-leveled to N64.

This tool is the ORACLE for the in-engine C++ port: it loads the N64 field (from the
REPL `floorgrid` CSV) and the OoT3D room mesh, builds D, applies it, and reports the
*warped* floor-delta distribution. If warped |delta| collapses toward 0 on ground cells
while the mesh stays sane, the algorithm is validated.

Usage:
  ZELDA3D_OOT3D_ROM=<rom> tools/zelda3d_warp.py /scene/spot01_0_info.zsi scratch/floor/n64_spot01.csv
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zsi as zsimod
import cmb as cmbmod
from ctr_romfs import CtrRom

REJECT = float(os.environ.get("ZELDA3D_WARP_REJECT", "120"))  # |D| above this = structure, not ground


def load_mesh(path):
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = zsimod.Zsi(rom.read(rom.get(path)))
    m = cmbmod.Cmb(z.cmb_bytes())
    tris = []  # (p0,p1,p2, minx,maxx,minz,maxz, ny)
    for _, _, tri in m.triangles():
        p0, p1, p2 = tri[0][1], tri[1][1], tri[2][1]
        ux, uy, uz = (p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2])
        vx, vy, vz = (p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2])
        ny = uz*vx - ux*vz
        nl = math.sqrt((uy*vz-uz*vy)**2 + ny*ny + (ux*vy-uy*vx)**2) or 1.0
        tris.append((p0, p1, p2,
                     min(p0[0],p1[0],p2[0]), max(p0[0],p1[0],p2[0]),
                     min(p0[2],p1[2],p2[2]), max(p0[2],p1[2],p2[2]), ny/nl))
    return tris


def floor_at(x, z, tris, target=None):
    best = None
    for p0, p1, p2, minx, maxx, minz, maxz, ny in tris:
        if x < minx or x > maxx or z < minz or z > maxz or ny <= 0.5:
            continue
        ax, az = p0[0], p0[2]; bx, bz = p1[0], p1[2]; cx, cz = p2[0], p2[2]
        d = (bz-cz)*(ax-cx) + (cx-bx)*(az-cz)
        if abs(d) < 1e-9:
            continue
        u = ((bz-cz)*(x-cx) + (cx-bx)*(z-cz)) / d
        v = ((cz-az)*(x-cx) + (ax-cx)*(z-cz)) / d
        w = 1.0 - u - v
        if u < -1e-4 or v < -1e-4 or w < -1e-4:
            continue
        y = u*p0[1] + v*p1[1] + w*p2[1]
        if best is None or (abs(y-target) < abs(best-target) if target is not None else y > best):
            best = y
    return best


class WarpGrid:
    """A regular XZ grid of vertical displacements D, with hole-fill so structure cells
    inherit nearby ground's correction. Bilinear sample at any (x,z)."""
    def __init__(self, x0, z0, step, nx, nz, D, valid):
        self.x0, self.z0, self.step, self.nx, self.nz = x0, z0, step, nx, nz
        self.D, self.valid = D, valid

    def sample(self, x, z):
        fx = (x - self.x0) / self.step
        fz = (z - self.z0) / self.step
        ix, iz = int(math.floor(fx)), int(math.floor(fz))
        tx, tz = fx - ix, fz - iz
        def g(i, j):
            i = min(max(i, 0), self.nx-1); j = min(max(j, 0), self.nz-1)
            return self.D[j*self.nx + i]
        return (g(ix,iz)*(1-tx)*(1-tz) + g(ix+1,iz)*tx*(1-tz)
                + g(ix,iz+1)*(1-tx)*tz + g(ix+1,iz+1)*tx*tz)


def build_warp(csv, tris):
    # N64 field on its native grid (from floorgrid CSV).
    cells = {}
    xs, zs = set(), set()
    with open(csv) as f:
        next(f)
        for ln in f:
            x, z, y, ny = ln.strip().split(",")
            x, z = float(x), float(z)
            xs.add(x); zs.add(z)
            cells[(x, z)] = None if y == "nan" else float(y)
    xs = sorted(xs); zs = sorted(zs)
    x0, z0 = xs[0], zs[0]
    step = xs[1]-xs[0] if len(xs) > 1 else 100.0
    nx, nz = len(xs), len(zs)
    D = [0.0]*(nx*nz)
    valid = [False]*(nx*nz)
    for j, z in enumerate(zs):
        for i, x in enumerate(xs):
            n64 = cells.get((x, z))
            if n64 is None:
                continue
            o = floor_at(x, z, tris, target=n64)
            if o is None:
                continue
            d = n64 - o
            if abs(d) <= REJECT:
                D[j*nx+i] = d
                valid[j*nx+i] = True
    # Hole-fill: BFS from valid cells so invalid (structure / off-mesh) cells inherit the
    # nearest valid ground displacement (a building then shifts with its surrounding ground).
    from collections import deque
    q = deque(k for k in range(nx*nz) if valid[k])
    filled = list(valid)
    while q:
        k = q.popleft()
        i, j = k % nx, k // nx
        for di, dj in ((1,0),(-1,0),(0,1),(0,-1)):
            ni, nj = i+di, j+dj
            if 0 <= ni < nx and 0 <= nj < nz:
                nk = nj*nx+ni
                if not filled[nk]:
                    D[nk] = D[k]
                    filled[nk] = True
                    q.append(nk)
    return WarpGrid(x0, z0, step, nx, nz, D, valid)


def main():
    scene, csv = sys.argv[1], sys.argv[2]
    tris = load_mesh(scene)
    wg = build_warp(csv, tris)
    nvalid = sum(1 for v in wg.valid if v)
    print(f"{scene}: {len(tris)} tris; warp grid {wg.nx}x{wg.nz} step {wg.step:.0f}, "
          f"{nvalid} valid ground cells (reject |D|>{REJECT:.0f})")

    # Verify: re-measure delta at the N64 sample points using WARPED OoT3D floor.
    raw, warped = [], []
    with open(csv) as f:
        next(f)
        for ln in f:
            x, z, y, ny = ln.strip().split(",")
            if y == "nan":
                continue
            x, z, n64 = float(x), float(z), float(y)
            o = floor_at(x, z, tris, target=n64)
            if o is None:
                continue
            d = wg.sample(x, z)  # displacement applied to mesh at this column
            raw.append(o - n64)
            warped.append((o + d) - n64)

    def stats(lst, label):
        lst = sorted(lst); n = len(lst)
        p = lambda q: lst[min(n-1, int(q*n))]
        within = lambda t: 100*sum(1 for d in lst if abs(d) <= t)/n
        print(f"  {label}: median={p(.5):+.1f} p90={p(.9):+.1f} |min|..|max|=[{lst[0]:+.0f},{lst[-1]:+.0f}]  "
              f"<=1:{within(1):.0f}% <=5:{within(5):.0f}% <=10:{within(10):.0f}%")
    stats(raw, "RAW   ")
    stats(warped, "WARPED")

    # Spot-check the known sink.
    o = floor_at(-1067, 429, tris, target=10.31)
    print(f"  sink (-1067,429): OoT3D raw={o:.1f} +D({wg.sample(-1067,429):+.1f}) "
          f"-> {o+wg.sample(-1067,429):.1f}  (N64=10.3)")


if __name__ == "__main__":
    main()
