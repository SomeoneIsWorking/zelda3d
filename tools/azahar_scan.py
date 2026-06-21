#!/usr/bin/env python3
"""azahar_scan.py — Cheat-Engine-style RAM search over the Azahar oracle (OoT3D, #89).

Finds a struct field by how its value reacts to actions we drive (e.g. Link's world position changes
when we walk him). Reads emulated 3DS RAM via tools/azahar_rpc, narrows a candidate set across passes.
Used to RE the OoT3D actor list / Actor+SkelAnime offsets / entrance index without symbols.
Documented in docs/oot3d_ram_re.md.

This module is meant to be imported & scripted, but `find_player_pos()` runs the standard move-and-diff
to locate Link's position floats. Run directly: `azahar_scan.py findpos` (Link must be controllable).
"""
import os, struct, sys, time
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

# 3DS mapped regions worth scanning for actor/player allocations.
REGIONS = [(0x08000000, 0x00A00000)]  # main heap; OoT3D actor allocations live here. Widen if needed.


def snapshot(rpc, base, size):
    return rpc.read(base, size)


def iter_f32(buf, base):
    """Yield (addr, value) for every 4-byte-aligned float in buf."""
    for off in range(0, len(buf) - 3, 4):
        (v,) = struct.unpack_from("<f", buf, off)
        yield base + off, v


def plausible_coord(v):
    import math
    return (not math.isnan(v)) and (not math.isinf(v)) and 0.5 < abs(v) < 100000.0


def as_f32(buf):
    n = (len(buf) // 4) * 4
    return np.frombuffer(buf[:n], dtype="<f4")


def find_player_pos(rpc, regions=REGIONS):
    """Move-and-diff (vectorized): f32s that moved on a walk and returned when we walked back =
    Link's world position (X/Z). Returns [(addr, v0, v1, v2), ...]."""
    def snap():
        return {base: as_f32(snapshot(rpc, base, size)) for base, size in regions}

    def still(label):
        print(f"[scan] still ({label})...", flush=True)
        rpc.set_input(0, None); time.sleep(0.5)
        return snap()

    def move(label, c):
        print(f"[scan] move {label}...", flush=True)
        rpc.set_input(0, c); time.sleep(1.3); rpc.set_input(0, None); time.sleep(0.5)
        return snap()

    # Alternate still/move/still/move/still/move/still. Position = unchanged across EVERY still interval
    # AND changed across EVERY move interval. (Camera/anim jitter fails "unchanged while still";
    # constants fail "changed while moving".) Move different directions so it really moves each time.
    seq = []  # (snapshot, moved_since_prev)
    seq.append((still("0"), None))
    seq.append((move("+Z", (0, 95)), True))
    seq.append((still("1"), False))
    seq.append((move("+X", (95, 0)), True))
    seq.append((still("2"), False))
    seq.append((move("-Z", (0, -95)), True))
    seq.append((still("3"), False))

    cands = []
    for base, size in regions:
        cols = [s[base] for s, _ in seq]
        n = min(len(c) for c in cols)
        cols = [c[:n] for c in cols]
        mask = np.isfinite(cols[0]) & (np.abs(cols[0]) > 0.5) & (np.abs(cols[0]) < 100000.0)
        for k in range(1, len(seq)):
            prev, cur, moved = cols[k - 1], cols[k], seq[k][1]
            mask &= np.isfinite(cur)
            if moved:
                mask &= np.abs(cur - prev) > 1.0     # must change while walking
            else:
                mask &= np.abs(cur - prev) < 0.01    # must hold while standing still
        idx = set(int(i) for i in np.where(mask)[0])
        for i in sorted(idx):
            # Vec3f run length: how many of {i, i+1, i+2} are also flagged (a clean world.pos x,y,z
            # shows as 2-3 consecutive flagged floats; scattered matrix floats show as 1).
            run = 1 + (1 if (i + 1) in idx else 0) + (1 if (i + 2) in idx else 0)
            cands.append((base + i * 4, [round(float(c[i]), 2) for c in cols], run))
    return cands


def main():
    rpc = A.Rpc(timeout=2.0)
    for pid, tid, _ in rpc.processes():
        if tid == 0x0004000000033500:
            rpc.select(pid)
    if len(sys.argv) > 1 and sys.argv[1] == "findpos":
        c = find_player_pos(rpc)
        print(f"[scan] {len(c)} candidates. Vec3f runs (run>=2) first — likely Actor.world.pos:")
        c.sort(key=lambda t: -t[2])
        for addr, vals, run in c[:80]:
            tag = f"  <-- Vec3f run={run}" if run >= 2 else ""
            print(f"  {addr:08x} (run{run}): {vals}{tag}")
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
