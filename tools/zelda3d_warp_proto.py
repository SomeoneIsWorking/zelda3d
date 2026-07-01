#!/usr/bin/env python3
"""zelda3d_warp_proto.py — prototype + measure improved terrain-warp variants offline.

Validates warp algorithm changes on real scene data BEFORE porting to C++
(zelda3d_model.cpp warpRoomMesh). Reports, for a scene + REPL floorgrid CSV:
  BENEFIT - warped walkable-ground |delta| vs N64 (higher <=1u % = better).
  HARM    - elevated (structure) verts displaced >50u (floating posts/trees; lower = better)
            and max elevated displacement.

Knobs (env):
  STEP    grid spacing (default 100)               REJECT  |D| reject threshold (default 120)
  HOLEFILL 1=BFS fill empty cells (default 1)      BLEND   1=height-fade correction (default 0)
  H0/H1   blend full below H0, zero above H1 above local ground (default 60/400)

The current engine algorithm == STEP=100 REJECT=120 HOLEFILL=1 BLEND=0.
Usage: ZELDA3D_OOT3D_ROM=<rom> python3 tools/zelda3d_warp_proto.py <scene.zsi> <n64.csv>
"""
import os, sys, math, csv
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zsi as zsimod, cmb as cmbmod
from ctr_romfs import CtrRom

STEP = float(os.environ.get("STEP", "100"))
REJECT = float(os.environ.get("REJECT", "120"))
HOLEFILL = os.environ.get("HOLEFILL", "1") == "1"
BLEND = os.environ.get("BLEND", "0") == "1"
H0 = float(os.environ.get("H0", "60"))
H1 = float(os.environ.get("H1", "400"))


def load(path):
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = zsimod.Zsi(rom.read(rom.get(path)))
    m = cmbmod.Cmb(z.cmb_bytes())
    floor_tris, all_verts = [], []
    for _, _, tri in m.triangles():
        p = [tri[k][1] for k in range(3)]
        all_verts.extend(p)
        ux, uy, uz = (p[1][0]-p[0][0], p[1][1]-p[0][1], p[1][2]-p[0][2])
        vx, vy, vz = (p[2][0]-p[0][0], p[2][1]-p[0][1], p[2][2]-p[0][2])
        ny = uz*vx - ux*vz
        nl = math.sqrt((uy*vz-uz*vy)**2 + ny*ny + (ux*vy-uy*vx)**2)
        if nl > 1e-9 and ny/nl > 0.5:
            xs = [q[0] for q in p]; zs = [q[2] for q in p]
            floor_tris.append((p, min(xs), max(xs), min(zs), max(zs)))
    return floor_tris, all_verts


def floor_at(tris, x, z, target, lowest=False):
    best = None
    for p, mnx, mxx, mnz, mxz in tris:
        if x < mnx or x > mxx or z < mnz or z > mxz:
            continue
        ax, az, bx, bz, cx, cz = p[0][0], p[0][2], p[1][0], p[1][2], p[2][0], p[2][2]
        d = (bz-cz)*(ax-cx) + (cx-bx)*(az-cz)
        if -1e-6 < d < 1e-6:
            continue
        u = ((bz-cz)*(x-cx) + (cx-bx)*(z-cz))/d
        w = ((cz-az)*(x-cx) + (ax-cx)*(z-cz))/d
        t = 1-u-w
        if u < -1e-4 or w < -1e-4 or t < -1e-4:
            continue
        y = u*p[0][1] + w*p[1][1] + t*p[2][1]
        if best is None or (y < best if lowest else abs(y-target) < abs(best-target)):
            best = y
    return best


def pctband(vals, t):
    return 100*sum(1 for v in vals if abs(v) <= t)/len(vals) if vals else 0


def main():
    scene, n64csv = sys.argv[1], sys.argv[2]
    tris, verts = load(scene)
    n64 = {}
    for r in csv.DictReader(open(n64csv)):
        if r['y'] != 'nan':
            n64[(float(r['x']), float(r['z']))] = float(r['y'])
    xs = [v[0] for v in verts]; zs = [v[2] for v in verts]
    minx, maxx, minz, maxz = min(xs), max(xs), min(zs), max(zs)
    nx, nz = int((maxx-minx)/STEP)+2, int((maxz-minz)/STEP)+2
    # nearest-N64 lookup (bucketed)
    from collections import defaultdict
    buck = defaultdict(list)
    for (x, z), y in n64.items():
        buck[(int(x//STEP), int(z//STEP))].append((x, z, y))
    def n64_at(x, z):
        best, bd = None, (STEP*1.5)**2
        bi, bj = int(x//STEP), int(z//STEP)
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                for (cx, cz, y) in buck.get((bi+di, bj+dj), []):
                    dd = (cx-x)**2 + (cz-z)**2
                    if dd < bd:
                        bd, best = dd, y
        return best

    D = [0.0]*(nx*nz); valid = [False]*(nx*nz)
    for j in range(nz):
        for i in range(nx):
            x, z = minx+i*STEP, minz+j*STEP
            ny = n64_at(x, z)
            if ny is None:
                continue
            oot = floor_at(tris, x, z, ny)
            if oot is None:
                continue
            d = ny - oot
            if abs(d) <= REJECT:
                D[j*nx+i] = d; valid[j*nx+i] = True
    nvalid = sum(valid)
    if HOLEFILL:
        filled = list(valid); q = [k for k in range(nx*nz) if valid[k]]; h = 0
        while h < len(q):
            k = q[h]; h += 1; i, j = k % nx, k//nx
            for di, dj in ((1,0),(-1,0),(0,1),(0,-1)):
                ni, nj = i+di, j+dj
                if 0 <= ni < nx and 0 <= nj < nz:
                    nk = nj*nx+ni
                    if not filled[nk]:
                        D[nk] = D[k]; filled[nk] = True; q.append(nk)

    def sample(x, z):
        fx, fz = (x-minx)/STEP, (z-minz)/STEP
        ix, iz = int(math.floor(fx)), int(math.floor(fz)); tx, tz = fx-ix, fz-iz
        def c(i, j):
            i = max(0, min(nx-1, i)); j = max(0, min(nz-1, j)); return D[j*nx+i]
        return (c(ix,iz)*(1-tx)*(1-tz)+c(ix+1,iz)*tx*(1-tz)+c(ix,iz+1)*(1-tx)*tz+c(ix+1,iz+1)*tx*tz)

    # ground Y under each vertex (lowest floor) for height-blend; cache per coarse xz
    gcache = {}
    def ground_y(x, z):
        key = (round(x/40), round(z/40))
        if key not in gcache:
            gcache[key] = floor_at(tris, x, z, 0.0, lowest=True)
        return gcache[key]

    def disp(v):
        d = sample(v[0], v[2])
        if BLEND:
            gy = ground_y(v[0], v[2])
            if gy is not None:
                h = v[1] - gy
                f = 1.0 if h <= H0 else (0.0 if h >= H1 else (H1-h)/(H1-H0))
                d *= f
        return d

    # BENEFIT: warped ground delta on cells with both floors
    warped = []
    for (x, z), ny in n64.items():
        oot = floor_at(tris, x, z, ny)
        if oot is None:
            continue
        # ground verts are at/just above local ground -> blend ~1; use sample (ground)
        warped.append((oot + sample(x, z)) - ny)
    # HARM: elevated structure verts
    elev_disp = []
    for v in verts:
        gy = ground_y(v[0], v[2])
        if gy is not None and v[1]-gy > 150:
            elev_disp.append(abs(disp(v)))
    floats = sum(1 for d in elev_disp if d > 50)
    print(f"STEP={STEP:.0f} REJECT={REJECT:.0f} HOLEFILL={int(HOLEFILL)} BLEND={int(BLEND)}"
          f"{f' H0={H0:.0f} H1={H1:.0f}' if BLEND else ''}  grid {nx}x{nz} valid={nvalid}")
    print(f"  BENEFIT warped ground |delta| <=1u:{pctband(warped,1):.0f}% <=10u:{pctband(warped,10):.0f}% (n={len(warped)})")
    print(f"  HARM elevated verts floating >50u: {floats}/{len(elev_disp)}  maxDisp={max(elev_disp) if elev_disp else 0:.0f}")


if __name__ == "__main__":
    main()
