#!/usr/bin/env python3
"""zelda3d_warp_audit.py — quantify the terrain warp's BENEFIT vs HARM for a scene room.

The terrain warp (zelda3d_model.cpp warpRoomMesh / zelda3d_warp.py) shifts every render-mesh
vertex Y by a per-XZ field D(x,z) = N64_floor - OoT3D_floor, built on a grid with
structure outliers (|D|>REJECT) rejected and hole-filled (BFS) from nearby ground. It fixes
Link sinking where OoT3D reshaped ground (Kakariko), but in scenes where the OoT3D ground
already matches N64 (Kokiri) it does little good while still:
  (1) shifting climbable fences/ladders off their UNWARPED N64 climb collision (desync), and
  (2) floating freestanding structures: a tree/post whose base cell is hole-filled inherits a
      distant neighbor's D (possibly hundreds of units) and the whole column lifts/sinks.

This tool reports, for a scene + REPL floorgrid CSV:
  BENEFIT  - raw vs warped ground-floor |delta| (how much the walkable ground improves).
  HARM     - hole-filled grid coverage + inherited-|D| distribution (structure-float risk);
             and per-vertex displacement, split into GROUND vs ELEVATED (structure) verts,
             flagging elevated verts displaced far from the local ground (the floating ones).
Gives a per-scene "net positive?" read so the warp can be applied only where it helps.

Usage:
  ZELDA3D_OOT3D_ROM=<rom> python3 tools/zelda3d_warp_audit.py /scene/spot04_0_info.zsi scratch/floor/n64_kokiri.csv
"""
import os, sys, math, csv
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zsi as zsimod
import cmb as cmbmod
from ctr_romfs import CtrRom

STEP = float(os.environ.get("ZELDA3D_WARP_STEP", "100"))
REJECT = float(os.environ.get("ZELDA3D_WARP_REJECT", "120"))


def load_tris(path):
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = zsimod.Zsi(rom.read(rom.get(path)))
    m = cmbmod.Cmb(z.cmb_bytes())
    tris = []
    verts = []
    for _, _, tri in m.triangles():
        p = [tri[k][1] for k in range(3)]
        verts.extend(p)
        ux, uy, uz = (p[1][0]-p[0][0], p[1][1]-p[0][1], p[1][2]-p[0][2])
        vx, vy, vz = (p[2][0]-p[0][0], p[2][1]-p[0][1], p[2][2]-p[0][2])
        ny = uz*vx - ux*vz
        nl = math.sqrt((uy*vz-uz*vy)**2 + ny*ny + (ux*vy-uy*vx)**2)
        is_floor = nl > 1e-9 and ny/nl > 0.5
        tris.append((p, is_floor))
    return tris, verts


def mesh_floor(tris, x, z, target):
    best = None
    for p, is_floor in tris:
        if not is_floor:
            continue
        ax, az, bx, bz, cx, cz = p[0][0], p[0][2], p[1][0], p[1][2], p[2][0], p[2][2]
        d = (bz-cz)*(ax-cx) + (cx-bx)*(az-cz)
        if -1e-6 < d < 1e-6:
            continue
        u = ((bz-cz)*(x-cx) + (cx-bx)*(z-cz)) / d
        w = ((cz-az)*(x-cx) + (ax-cx)*(z-cz)) / d
        t = 1 - u - w
        if u < -1e-4 or w < -1e-4 or t < -1e-4:
            continue
        y = u*p[0][1] + w*p[1][1] + t*p[2][1]
        if best is None or abs(y-target) < abs(best-target):
            best = y
    return best


def pct(vals, p):
    if not vals:
        return float('nan')
    s = sorted(vals)
    return s[min(len(s)-1, int(p/100*len(s)))]


def main():
    scene, n64csv = sys.argv[1], sys.argv[2]
    tris, verts = load_tris(scene)
    n64 = {}
    for r in csv.DictReader(open(n64csv)):
        if r['y'] != 'nan':
            n64[(round(float(r['x'])), round(float(r['z'])))] = float(r['y'])
    xs = [v[0] for v in verts]; zs = [v[2] for v in verts]
    minx, maxx, minz, maxz = min(xs), max(xs), min(zs), max(zs)
    nx = int((maxx-minx)/STEP) + 2
    nz = int((maxz-minz)/STEP) + 2

    # Build D grid exactly like the engine: sample N64 (nearest grid pt from CSV) and OoT3D.
    D = [0.0]*(nx*nz)
    valid = [False]*(nx*nz)
    def n64_at(x, z):
        # nearest CSV sample within half a CSV step
        best = None; bestd = 1e30
        for (cx, cz), y in n64.items():
            dd = (cx-x)**2 + (cz-z)**2
            if dd < bestd:
                bestd = dd; best = y
        return best if bestd <= (STEP*1.5)**2 else None
    for j in range(nz):
        for i in range(nx):
            x, z = minx + i*STEP, minz + j*STEP
            ny = n64_at(x, z)
            if ny is None:
                continue
            oot = mesh_floor(tris, x, z, ny)
            if oot is None:
                continue
            d = ny - oot
            if abs(d) <= REJECT:
                D[j*nx+i] = d; valid[j*nx+i] = True
    nvalid = sum(valid)
    # BFS hole-fill
    filled = list(valid)
    q = [k for k in range(nx*nz) if valid[k]]
    inherited = []
    head = 0
    while head < len(q):
        k = q[head]; head += 1
        i, j = k % nx, k // nx
        for di, dj in ((1,0),(-1,0),(0,1),(0,-1)):
            ni, nj = i+di, j+dj
            if 0 <= ni < nx and 0 <= nj < nz:
                nk = nj*nx+ni
                if not filled[nk]:
                    D[nk] = D[k]; filled[nk] = True; q.append(nk)
                    inherited.append(abs(D[nk]))

    def sample(x, z):
        fx, fz = (x-minx)/STEP, (z-minz)/STEP
        ix, iz = int(math.floor(fx)), int(math.floor(fz))
        tx, tz = fx-ix, fz-iz
        def cell(i, j):
            i = max(0, min(nx-1, i)); j = max(0, min(nz-1, j))
            return D[j*nx+i]
        return (cell(ix,iz)*(1-tx)*(1-tz) + cell(ix+1,iz)*tx*(1-tz) +
                cell(ix,iz+1)*(1-tx)*tz + cell(ix+1,iz+1)*tx*tz)

    # BENEFIT: raw vs warped ground-floor delta on cells with both floors
    raw, warped = [], []
    for (x, z), ny in n64.items():
        oot = mesh_floor(tris, x, z, ny)
        if oot is None:
            continue
        raw.append(oot - ny)
        warped.append((oot + sample(x, z)) - ny)
    def band(vals, t):
        return 100*sum(1 for v in vals if abs(v) <= t)/len(vals) if vals else 0
    print(f"{scene}: {len(tris)} tris, grid {nx}x{nz} step {STEP:.0f}, {nvalid} valid ground cells, "
          f"{sum(filled)-nvalid} hole-filled (reject |D|>{REJECT:.0f})")
    print(f"  BENEFIT ground-floor |delta|  RAW  <=1:{band(raw,1):.0f}% <=10:{band(raw,10):.0f}%  "
          f"WARPED <=1:{band(warped,1):.0f}% <=10:{band(warped,10):.0f}%  (n={len(raw)})")

    # HARM 1: hole-filled inherited D magnitude (structure-float risk)
    print(f"  HARM hole-filled inherited |D|: median={pct(inherited,50):.0f} p90={pct(inherited,90):.0f} "
          f"max={max(inherited) if inherited else 0:.0f}  (>50u floats a structure ~that much)")

    # HARM 2: per-vertex displacement, ground vs elevated. "local ground" = lowest floor y near xz.
    disp_ground, disp_elev, float_verts = [], [], 0
    # crude local ground per vertex: the mesh floor at its xz (target = its own y)
    miny = min(v[1] for v in verts)
    for v in verts:
        d = sample(v[0], v[2])
        gy = mesh_floor(tris, v[0], v[2], miny)  # lowest-ish floor under this xz
        elev = (gy is not None) and (v[1] - gy > 150)  # >150u above local ground = structure
        if elev:
            disp_elev.append(abs(d))
            if abs(d) > 50:
                float_verts += 1
        else:
            disp_ground.append(abs(d))
    print(f"  HARM vertex displacement |dY|: GROUND median={pct(disp_ground,50):.0f} p90={pct(disp_ground,90):.0f}  "
          f"ELEVATED median={pct(disp_elev,50):.0f} p90={pct(disp_elev,90):.0f}")
    print(f"  HARM elevated (structure) verts displaced >50u (visibly floating): {float_verts} "
          f"of {len(disp_elev)} elevated verts")


if __name__ == "__main__":
    main()
