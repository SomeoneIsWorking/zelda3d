#!/usr/bin/env python3
"""motion_parity.py — diff a Zelda3D actor trajectory against the OoT3D oracle trajectory.

The synthesis half of the BEHAVIORAL motion-parity harness:
  Zelda3D side : in-game REPL `asample <n> scratch/motion/zelda3d.csv`   (per-frame, exact)
  oracle side: sample the harness per-frame into scratch/motion/oracle.csv
  this tool  : motion_parity.py scratch/motion/zelda3d.csv scratch/motion/oracle.csv

Zelda3D and OoT3D share world coordinates + entrance spawn coords ([[zelda3d-oracle-entrance-match]]),
so positions are directly comparable in world units when both are driven to the same scene/actor.
Rotations are s16 binang in both. The two captures need NOT have the same length or frame phase —
we compare frame-rate/offset-tolerant SHAPE metrics (path length, net displacement, per-frame speed
profile correlation, rotation sweep) plus, when lengths are close, a direct per-frame position error.

Exit status is informational only (0). Read the printed report; large divergence = a behavioral gap.
"""
import argparse, csv, math, sys


def load(path):
    rows = []
    speeds = []  # engine-authoritative speedXZ when the capture has that column (Zelda3D side)
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.append((
                float(r["posx"]), float(r["posy"]), float(r["posz"]),
                int(r["rotx"]), int(r["roty"]), int(r["rotz"]),
            ))
            if r.get("speedXZ") not in (None, ""):
                speeds.append(float(r["speedXZ"]))
    return rows, speeds


def median(xs):
    xs = sorted(xs)
    n = len(xs)
    if n == 0:
        return float("nan")
    return xs[n // 2] if n % 2 else 0.5 * (xs[n // 2 - 1] + xs[n // 2])


def path_metrics(rows):
    if not rows:
        return None
    pos = [(x, y, z) for (x, y, z, *_) in rows]
    rotY = [r[4] for r in rows]
    steps = [math.dist(pos[i], pos[i - 1]) for i in range(1, len(pos))]
    total = sum(steps)
    net = math.dist(pos[0], pos[-1])
    netvec = tuple(pos[-1][k] - pos[0][k] for k in range(3))
    # unwrap rotY (binang wraps at +-32768) to measure true angular sweep
    sweep = 0
    for i in range(1, len(rotY)):
        d = rotY[i] - rotY[i - 1]
        d = (d + 32768) % 65536 - 32768
        sweep += d
    return {
        "n": len(rows), "start": pos[0], "end": pos[-1], "net": net, "netvec": netvec,
        "path": total, "mean_speed": total / max(1, len(steps)),
        "median_speed": median(steps), "steps": steps,
        "rotY_sweep_deg": sweep / 65536.0 * 360.0,
    }


def corr(a, b):
    n = min(len(a), len(b))
    if n < 2:
        return float("nan")
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    if va == 0 or vb == 0:
        return 1.0 if va == vb else 0.0
    cov = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    return cov / math.sqrt(va * vb)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zelda3d")
    ap.add_argument("oracle")
    args = ap.parse_args()

    s, s_spd = load(args.zelda3d)
    o, o_spd = load(args.oracle)
    ms = path_metrics(s)
    mo = path_metrics(o)
    if ms is None or mo is None:
        sys.exit("motion_parity: one of the captures is empty")

    def fmt(v):
        return f"({v[0]:.1f},{v[1]:.1f},{v[2]:.1f})"

    print(f"{'metric':<22}{'Zelda3D':>16}{'oracle':>16}")
    print(f"{'frames':<22}{ms['n']:>16}{mo['n']:>16}")
    print(f"{'start pos':<22}{fmt(ms['start']):>16}{fmt(mo['start']):>16}")
    print(f"{'end pos':<22}{fmt(ms['end']):>16}{fmt(mo['end']):>16}")
    print(f"{'path length (u)':<22}{ms['path']:>16.1f}{mo['path']:>16.1f}")
    print(f"{'net displacement (u)':<22}{ms['net']:>16.1f}{mo['net']:>16.1f}")
    print(f"{'net vec':<22}{fmt(ms['netvec']):>16}{fmt(mo['netvec']):>16}")
    print(f"{'mean speed (u/frame)':<22}{ms['mean_speed']:>16.3f}{mo['mean_speed']:>16.3f}")
    print(f"{'median speed (u/frame)':<22}{ms['median_speed']:>16.3f}{mo['median_speed']:>16.3f}")
    print(f"{'rotY sweep (deg)':<22}{ms['rotY_sweep_deg']:>16.1f}{mo['rotY_sweep_deg']:>16.1f}")

    # Engine-authoritative steady speed: prefer the logged speedXZ on the side that has it
    # (the positional per-frame delta is confounded when the two engines log at different
    # wall-clock fps, or when gcam curves a driven Link). The oracle logs one row per game
    # frame, so its positional median IS its per-frame speed.
    s_auth = median(s_spd) if s_spd else ms["median_speed"]
    o_auth = median(o_spd) if o_spd else mo["median_speed"]
    print(f"\nsteady per-frame speed (authoritative): Zelda3D={s_auth:.3f}  oracle={o_auth:.3f}"
          f"  ({'speedXZ col' if s_spd else 'pos-delta'} vs {'speedXZ col' if o_spd else 'pos-delta'})")
    if o_auth:
        print(f"  ratio SoH/ora = {s_auth / o_auth:.3f}")
    # Framerate-compensation gap — ROOT-CAUSED (session 2026-06-24m), not a divergence:
    # z_actor.c Actor_UpdatePos does `pos += velocity * (R_UPDATE_RATE * 0.5)`, and normal play sets
    # R_UPDATE_RATE = 3 (game.c:437 / z_play.c) -> speedRate = 1.5. So EVERY actor in SoH/N64 advances
    # its position 1.5x its velocity counter per logic frame, on all axes (verified live: Link run AND
    # free-fall both show pos-delta == 1.5*velocity, gframe incrementing by exactly 1 = no undersample).
    # This is authentic vanilla-OoT 20fps behavior. OoT3D (oracle, 30fps) integrates with a different
    # update rate so its per-frame pos-delta == its velocity (~1.0x); per-SECOND ground speed matches
    # (20fps*1.5 == 30fps*1.0). => the velocity field (speedXZ) is the frame-rate-INDEPENDENT parity
    # metric; ALWAYS verdict on speedXZ, never on per-frame positional deltas (they differ by design).
    if s_spd and ms["median_speed"] > 1.3 * s_auth:
        print(f"  [Zelda3D pos-delta {ms['median_speed']:.2f}/frame = {ms['median_speed']/s_auth:.2f}x its "
              f"speedXZ — the 20->30fps position compensation, not a divergence]")

    print("\n--- divergence ---")
    sc = corr(ms["steps"], mo["steps"])
    print(f"speed-profile correlation : {sc:.3f}  (1.0 = identical motion shape)")
    path_ratio = ms["path"] / mo["path"] if mo["path"] else float("inf")
    print(f"path-length ratio (SoH/ora): {path_ratio:.3f}")
    if ms["netvec"] != (0, 0, 0) and mo["netvec"] != (0, 0, 0):
        dot = sum(ms["netvec"][k] * mo["netvec"][k] for k in range(3))
        cos = dot / (math.dist((0, 0, 0), ms["netvec"]) * math.dist((0, 0, 0), mo["netvec"]))
        print(f"net-direction angle (deg) : {math.degrees(math.acos(max(-1,min(1,cos)))):.1f}")
    n = min(ms["n"], mo["n"])
    if n >= 2:
        perr = sum(math.dist(s[i][:3], o[i][:3]) for i in range(n)) / n
        print(f"mean per-frame pos error  : {perr:.2f} u  (over first {n} frames, phase-aligned)")
    # Verdict keys on the authoritative steady per-frame speed (robust to fps/curve confounds)
    # plus the speed-profile shape correlation. Path-length ratio is reported but NOT a verdict
    # driver — it is confounded by wall-clock fps differences and driven-Link curving.
    flags = []
    if o_auth and (s_auth / o_auth > 1.15 or s_auth / o_auth < 0.87):
        flags.append(f"steady per-frame speed differs ({s_auth:.2f} vs {o_auth:.2f})")
    if not math.isnan(sc) and sc < 0.5:
        flags.append("speed-profile decorrelated")
    print("\nVERDICT:", "PARITY (within tolerance)" if not flags else "DIVERGENCE: " + "; ".join(flags))


if __name__ == "__main__":
    main()
