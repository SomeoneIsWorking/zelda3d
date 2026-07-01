#!/usr/bin/env python3
"""scene_geom_anomaly.py — find STRAY/oversized geometry in room CMBs, quantitatively (no eyeball).

The offline twin of the live `geomscan` REPL command: instead of reading per-draw world AABBs out
of the running renderer, it parses every room CMB straight from the ROM and computes the AABB of
each MESH (sepd group). A "weird geometrical object" shows up as a mesh that is a clear outlier in
its room — a lone giant extent, a vertex far outside the room bounds, or NaN/inf — none of which a
normal room mesh produces.

It first measures the per-mesh extent distribution across EVERY scene (the baseline of "normal"),
then reports, for the requested scene prefix, each room's meshes ranked by extent and flags:
  GIANT   mesh max-extent above the global percentile AND >2.5x the room's next-largest mesh
  STRAY   a vertex whose |coord| exceeds the room's bulk by a wide margin (pokes far outside)
  BAD     NaN / inf vertex

Usage:
  ZELDA3D_OOT3D_ROM=<rom> python3 tools/scene_geom_anomaly.py [scene_prefix=jyasinzou]
"""
import os, sys, re, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ctr_romfs import CtrRom
import zsi as zsimod, cmb as cmbmod


def room_files(rom):
    out = []
    for f in rom.iter_files():
        p = f if isinstance(f, str) else getattr(f, "path", str(f))
        if re.match(r"/scene/[a-zA-Z0-9_]+_\d+_info\.zsi$", p):
            out.append(p)
    return sorted(set(out))


def mesh_aabbs(cmb):
    """Return {sepd_index: [minx,miny,minz,maxx,maxy,maxz, nverts, nbad]}."""
    agg = {}
    for sepd_i, _mat, verts in cmb.triangles():
        a = agg.get(sepd_i)
        if a is None:
            a = [math.inf, math.inf, math.inf, -math.inf, -math.inf, -math.inf, 0, 0]
            agg[sepd_i] = a
        for _idx, pos, _n, _uv in verts:
            x, y, z = pos
            if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
                a[7] += 1
                continue
            a[0] = min(a[0], x); a[1] = min(a[1], y); a[2] = min(a[2], z)
            a[3] = max(a[3], x); a[4] = max(a[4], y); a[5] = max(a[5], z)
            a[6] += 1
    return agg


def extent(a):
    if a[6] == 0:
        return 0.0
    return max(a[3] - a[0], a[4] - a[1], a[5] - a[2])


def main():
    prefix = sys.argv[1] if len(sys.argv) > 1 else "jyasinzou"
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    rooms = room_files(rom)

    # Pass 1: global baseline of per-mesh extents.
    all_ext = []
    for p in rooms:
        try:
            z = zsimod.Zsi(rom.read(rom.get(p)))
            if not z.has_geometry():
                continue
            m = cmbmod.Cmb(z.cmb_bytes())
            for a in mesh_aabbs(m).values():
                if a[6]:
                    all_ext.append(extent(a))
        except Exception:
            continue
    all_ext.sort()
    n = len(all_ext)
    def pct(q):
        return all_ext[min(n - 1, int(q * n))] if n else 0.0
    p50, p99, p999 = pct(0.50), pct(0.99), pct(0.999)
    print(f"baseline: {n} meshes across all scenes | extent p50={p50:.0f} p99={p99:.0f} p99.9={p999:.0f} max={all_ext[-1]:.0f}")
    print(f"flagging GIANT > max(p99.9={p999:.0f}, 2.5x room-next-largest)\n")

    # Pass 2: the requested scene.
    scene_rooms = [p for p in rooms if re.match(rf"/scene/{prefix}_\d+_info\.zsi$", p)]
    nflag = 0
    for p in scene_rooms:
        rno = re.search(r"_(\d+)_info", p).group(1)
        try:
            z = zsimod.Zsi(rom.read(rom.get(p)))
            if not z.has_geometry():
                print(f"room {rno}: no geometry"); continue
            m = cmbmod.Cmb(z.cmb_bytes())
        except Exception as e:
            print(f"room {rno}: PARSE FAIL {e!r}"); continue
        ms = mesh_aabbs(m)
        ranked = sorted(((extent(a), si, a) for si, a in ms.items()), reverse=True)
        # room bulk bounds = union of all but the single largest mesh
        bulk = [math.inf, math.inf, math.inf, -math.inf, -math.inf, -math.inf]
        for _e, _si, a in ranked[1:]:
            if not a[6]:
                continue
            for k in range(3):
                bulk[k] = min(bulk[k], a[k]); bulk[k + 3] = max(bulk[k + 3], a[k + 3])
        next_ext = ranked[1][0] if len(ranked) > 1 else 0.0
        flags = []
        for e, si, a in ranked:
            tag = None
            if a[7]:
                tag = f"BAD({a[7]} nan/inf)"
            elif e > max(p999, 2.5 * next_ext) and e > p99:
                tag = "GIANT"
            else:
                # stray vertex: mesh AABB pokes far beyond the room bulk
                if a[6] and all(math.isfinite(v) for v in bulk):
                    poke = max(bulk[0] - a[0], bulk[1] - a[1], bulk[2] - a[2],
                               a[3] - bulk[3], a[4] - bulk[4], a[5] - bulk[5])
                    if poke > max(1000.0, 1.5 * (bulk[3] - bulk[0])):
                        tag = f"STRAY(+{poke:.0f} beyond bulk)"
            if tag:
                flags.append((tag, e, si, a))
        if flags:
            nflag += len(flags)
            print(f"room {rno}: {len(ms)} meshes, largest ext={ranked[0][0]:.0f}, next={next_ext:.0f}")
            for tag, e, si, a in flags:
                print(f"  ** {tag} sepd={si} ext={e:.0f} aabb=({a[0]:.0f},{a[1]:.0f},{a[2]:.0f})..({a[3]:.0f},{a[4]:.0f},{a[5]:.0f}) nverts={a[6]}")
    print(f"\n{prefix}: {len(scene_rooms)} rooms, {nflag} flagged mesh(es)")


if __name__ == "__main__":
    main()
