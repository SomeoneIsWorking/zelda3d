#!/usr/bin/env python3
"""title_rider_traj.py — title-demo rider trajectory A/B (oracle vs SoH).

Drives the embedded harness to the TitleSyncController LOCKED state (same
arm sequence as title_sbs_verify.py), then samples the rider's world
position on BOTH engines at matched cs frames:

  - oracle: direct VA read of the RE'd title rider world.pos mirror
    (0x005AFFB0, oot3d-decomp docs/title_actor_world_pos.md), plus — when
    the deterministic heap instance at 0x09906A80 (docs/title_rider_driver.md
    §2) cross-checks against the mirror — the rider's world.rot.y (+0x36)
    and speed_xz (+0x6C).
  - SoH: the `compare player` title-actor line (SohState_PlayerPos — Link
    rides the horse, so Player.world.pos == the cue-integrator's pos).

Output: a per-sample table (csFrame, az pos, soh pos, |dXZ|), a per-frame
oracle speed profile over dense windows (reveals the oracle's true
integration law: constant-speed vs authored-rate vs ramp), and the CLOSE-
TEST METRIC:

    maxdXZ = max over sampled cs frames of |az.xz - soh.xz|

Exit 0 iff maxdXZ <= --max-dxz (default 25.0, ~3 frames of 8u/f phase).

Usage:
    source .env   # ZELDA3D_OOT3D_ROM
    tools/title_rider_traj.py                     # default sample plan
    tools/title_rider_traj.py --dense 925:1130 --coarse 188,450,588,1250,1350
"""
from __future__ import annotations

import argparse
import csv
import math
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness_process import spawn  # noqa: E402
from harness_transport import Harness  # noqa: E402

ENGINE_FRAMES_PER_CS = 2
RIDER_POS_VA = 0x005AFFB0     # static mirror of the title rider world.pos (Vec3f)
RIDER_ACTOR_VA = 0x09906A80   # deterministic heap instance (title_rider_driver.md §2)
OUTDIR = Path(__file__).resolve().parent.parent / "scratch" / "title_ab"


def step(h: Harness, n: int, timeout: float = 900.0) -> str:
    h.proc.stdin.write((f"step {n}\n").encode())
    h.proc.stdin.flush()
    line = h._readline(timeout=timeout)
    if line is None:
        raise TimeoutError(f"step {n}: no response within {timeout}s")
    return line.rstrip()


def parse_field(state_line: str, key: str) -> str:
    return state_line.split(f"{key}=")[1].split()[0]


def read_f32(h: Harness, va: int) -> float | None:
    resp = h.send(f"r32 0x{va:08x}")
    if not resp.startswith("ok "):
        return None
    return struct.unpack("<f", struct.pack("<I", int(resp.split()[1], 16)))[0]


def read_s16(h: Harness, va: int) -> int | None:
    resp = h.send(f"r16 0x{va:08x}")
    if not resp.startswith("ok "):
        return None
    v = int(resp.split()[1], 16)
    return v - 0x10000 if v >= 0x8000 else v


def az_rider_pos(h: Harness) -> tuple[float, float, float]:
    return tuple(read_f32(h, RIDER_POS_VA + 4 * j) for j in range(3))  # type: ignore


def az_rider_actor(h: Harness):
    """(pos, yaw, speed_xz) from the heap actor instance, or None if it no
    longer cross-checks against the static mirror (heap layout drifted)."""
    pos = tuple(read_f32(h, RIDER_ACTOR_VA + 0x28 + 4 * j) for j in range(3))
    mirror = az_rider_pos(h)
    if any(p is None for p in pos) or any(m is None for m in mirror):
        return None
    if max(abs(a - b) for a, b in zip(pos, mirror)) > 1.0:
        return None
    yaw = read_s16(h, RIDER_ACTOR_VA + 0x36)
    spd = read_f32(h, RIDER_ACTOR_VA + 0x6C)
    return pos, yaw, spd


def soh_player_pos(h: Harness):
    lines = h.send_multiline("compare player")
    for ln in lines:
        ln = ln.strip()
        if ln.startswith("title-actor:") and "soh_world=" in ln:
            part = ln.split("soh_world=(")[1].split(")")[0]
            return tuple(float(v) for v in part.split(","))
        if ln.startswith("soh:") and "pos=(" in ln:
            part = ln.split("pos=(")[1].split(")")[0]
            return tuple(float(v) for v in part.split(","))
    return None


def parse_dense(spec: str) -> list[int]:
    a, b = spec.split(":")
    return list(range(int(a), int(b) + 1))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dense", default="925:1130",
                    help="dense cs-frame window start:end, every cs frame (default 925:1130)")
    ap.add_argument("--coarse", default="188,450,588,700,850,1200,1300,1360",
                    help="comma-separated extra cs frames (default spans other cues)")
    ap.add_argument("--max-dxz", type=float, default=25.0,
                    help="close-test threshold on max |dXZ| (default 25.0)")
    ap.add_argument("--name", default="rider_traj", help="output csv name prefix")
    ap.add_argument("--loop-len", type=int, default=2400)
    args = ap.parse_args()

    dense = parse_dense(args.dense)
    coarse = [int(x) for x in args.coarse.split(",") if x]
    targets = sorted(set(coarse) | set(dense))

    OUTDIR.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR", f"scratch/logs/title_rider_traj_{args.name}.log")

    h = spawn(save_state=None)
    rows = []
    try:
        # drive to LOCKED (same loop as title_sbs_verify.drive_to_locked)
        while True:
            step(h, 20)
            t = h.send("titlesync")
            if parse_field(t, "state") == "LOCKED":
                break
            if int(parse_field(t, "sohFrame")) > 3000:
                raise RuntimeError(f"title-sync never locked: {t}")
        az_lock_cs = int(parse_field(t, "azLockCs"))
        print(f"[rider_traj] locked: {t}", file=sys.stderr)
        cur_cs = int(parse_field(t, "csFrame"))

        for target in targets:
            if target < az_lock_cs + 5:
                continue  # inside the by-design re-HOLD window
            delta = target - cur_cs
            if delta < 0:
                delta += args.loop_len
            if delta > 0:
                step(h, delta * ENGINE_FRAMES_PER_CS)
            t = h.send("titlesync")
            cur_cs = int(parse_field(t, "csFrame"))
            az = az_rider_pos(h)
            actor = az_rider_actor(h)
            soh = soh_player_pos(h)
            dxz = None
            if soh is not None and az[0] is not None:
                dxz = math.hypot(az[0] - soh[0], az[2] - soh[2])
            rows.append({
                "cs": cur_cs, "target": target,
                "az_x": az[0], "az_y": az[1], "az_z": az[2],
                "soh_x": soh[0] if soh else None,
                "soh_y": soh[1] if soh else None,
                "soh_z": soh[2] if soh else None,
                "az_yaw": actor[1] if actor else None,
                "az_spd": actor[2] if actor else None,
                "dxz": dxz,
            })
            if len(rows) % 25 == 1:
                print(f"[rider_traj] cs={cur_cs} az=({az[0]:.1f},{az[2]:.1f}) "
                      f"soh=({soh[0]:.1f},{soh[2]:.1f}) dxz={dxz:.1f}"
                      if soh and dxz is not None else f"[rider_traj] cs={cur_cs} (partial)",
                      file=sys.stderr)
    finally:
        h.close()

    out = OUTDIR / f"{args.name}.csv"
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    # Mark FRESH oracle samples: the embedded Az core executes in multi-frame
    # bursts, so the VA read can be stale (frozen) for several sampled cs
    # frames and then jump the accumulated distance. Only fresh samples (pos
    # changed vs the previous sample) are same-cs-frame comparable; stale ones
    # would fabricate huge dXZ spikes that are a sampling artifact, not a
    # trajectory divergence.
    # fresh = changed vs the PREVIOUS sample AND vs the NEXT one — the first
    # sample of a freeze-run reads a position that stopped updating mid-burst
    # and is just as stale as the frozen samples after it.
    for i, r in enumerate(rows):
        cur = (r["az_x"], r["az_z"])
        prev_ok = i > 0 and cur != (rows[i-1]["az_x"], rows[i-1]["az_z"])
        next_ok = i + 1 >= len(rows) or cur != (rows[i+1]["az_x"], rows[i+1]["az_z"])
        r["fresh"] = prev_ok and next_ok and r["az_x"] is not None

    # per-frame oracle speed over consecutive dense samples
    print("\n=== oracle per-frame XZ speed (dense window, consecutive cs frames) ===")
    prev = None
    speeds = []
    for r in rows:
        if prev is not None and r["cs"] == prev["cs"] + 1 and r["az_x"] is not None:
            v = math.hypot(r["az_x"] - prev["az_x"], r["az_z"] - prev["az_z"])
            speeds.append((r["cs"], v))
        prev = r
    for cs, v in speeds:
        if cs % 10 == 0 or v < 6.0 or v > 10.0:
            print(f"  cs={cs:5d} v={v:7.3f}")
    if speeds:
        vs = [v for _, v in speeds]
        print(f"  n={len(vs)} mean={sum(vs)/len(vs):.3f} min={min(vs):.3f} max={max(vs):.3f}")

    print("\n=== A/B table (every 10th dense + all coarse; * = stale oracle read) ===")
    print(f"{'cs':>5}  {'az_x':>9} {'az_z':>9}  {'soh_x':>9} {'soh_z':>9}  {'dXZ':>7}")
    worst = (None, -1.0)
    n_fresh = 0
    for i, r in enumerate(rows):
        if r["dxz"] is None:
            continue
        if r["fresh"]:
            n_fresh += 1
            if r["dxz"] > worst[1]:
                worst = (r["cs"], r["dxz"])
        if r["cs"] % 10 == 0 or r["target"] in coarse:
            mark = "" if r["fresh"] else " *"
            print(f"{r['cs']:>5}  {r['az_x']:>9.1f} {r['az_z']:>9.1f}  "
                  f"{r['soh_x']:>9.1f} {r['soh_z']:>9.1f}  {r['dxz']:>7.1f}{mark}")

    print(f"\ncsv: {out}")
    print(f"maxdXZ (fresh oracle samples only, n={n_fresh}) = "
          f"{worst[1]:.1f} u at cs={worst[0]}  (threshold {args.max_dxz})")
    ok = worst[1] >= 0 and worst[1] <= args.max_dxz
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
