#!/usr/bin/env python3
"""oracle_carry_drive.py — drive OoT3D oracle to LIFT a rock (En_Ishi) and CARRY-WALK, capturing
Link's per-frame bone LOCAL rotations across the pickup + carry-walk so we can ID the CSAB sequence
and diff geometry vs Zelda3D's carry path (#117 carry-walk / pickup).

Method: teleport Link adjacent to a target rock facing it, tap A to lift (poll heldActor @head+0x12b0
until non-zero), then hold the circle pad forward and capture the carry-walk pose. Writes the same CSV
format as oracle_link_pose.py (cap,t_ms,bone,r0..r8) plus a phase column so frames are ordered.

Usage:
  tools/oracle_carry_drive.py --rock X,Z[,Y] --face YAW --out scratch/parity/oracle_carrywalk.csv
"""
import argparse, math, os, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

OOT3D_TID = 0x0004000000033500
GPLAYSTATE = 0x0050AF34
SKELANIME_OFF = 0x254
JOINTTABLE_OFF = 0x78
BONE_STRIDE = 13 * 4
NBONE = 25
HELDACTOR_OFF = 0x12b0   # player_port.md: heldActor-ish


def link_actor(rpc):
    ps = rpc.read32(GPLAYSTATE)
    if not ps:
        sys.exit("no PlayState")
    return rpc.read32(ps + 0x208C + 0x0C + 2 * 8 + 4)


def bone_local_rots(rpc, actor):
    jt = rpc.read32(actor + SKELANIME_OFF + JOINTTABLE_OFF)
    d = rpc.read(jt, NBONE * BONE_STRIDE)
    out = []
    for i in range(NBONE):
        m = struct.unpack_from("<12f", d, i * BONE_STRIDE)
        out.append((m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]))
    return out


def pose_delta(a, b):
    tot = 0.0
    for ra, rb in zip(a, b):
        tr = sum(ra[i] * rb[i] for i in range(9))
        c = max(-1.0, min(1.0, (tr - 1.0) / 2.0))
        tot += math.degrees(math.acos(c))
    return tot


def held(rpc, actor):
    return rpc.read32(actor + HELDACTOR_OFF)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rock", required=True, help="X,Z[,Y] of the rock to lift")
    ap.add_argument("--face", type=int, default=0, help="Link yaw (binang) to face the rock")
    ap.add_argument("--standoff", type=float, default=28.0, help="distance Link stands from rock")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--hz", type=float, default=300.0)
    ap.add_argument("--eps", type=float, default=2.5)
    ap.add_argument("--out", default="scratch/parity/oracle_carrywalk.csv")
    ap.add_argument("--no-walk", action="store_true", help="capture carry-IDLE only (no forward hold)")
    ap.add_argument("--lift", action="store_true",
                    help="capture the LIFT transition: stand at the rock, then record from BEFORE the "
                         "A-press through the grab/lift (no walk), to ID the pickup CSAB sequence")
    args = ap.parse_args()

    parts = [float(v) for v in args.rock.split(",")]
    rx, rz = parts[0], parts[1]
    ry = parts[2] if len(parts) > 2 else 0.0

    rpc = A.Rpc()
    for pid, tid, _ in rpc.processes():
        if tid == OOT3D_TID:
            rpc.select(pid); break
    actor = link_actor(rpc)
    print(f"link actor @{actor:08x}")

    # place Link just behind the rock along -Z relative to facing, write pos + yaw
    lx = rx
    lz = rz + args.standoff   # stand on +Z side; yaw will face -Z toward rock if face=0x8000
    rpc.write(actor + 0x08, struct.pack("<f", lx))
    rpc.write(actor + 0x0C, struct.pack("<f", ry))
    rpc.write(actor + 0x10, struct.pack("<f", lz))
    rpc.write(actor + 0x14, struct.pack("<3h", 0, args.face, 0))   # shape.rot
    time.sleep(0.4)

    if args.lift:
        # LIFT capture: record from BEFORE the A-press through the grab so the lift CSAB sequence
        # (carryB) is captured, not just the settled carry pose. A single A-tap fires the lift.
        period = 1.0 / args.hz
        rows = []
        last = None
        pressed = False
        t0 = time.time()
        while len(rows) < args.frames and time.time() - t0 < 6.0:
            el = time.time() - t0
            # press A once at ~0.3s in, hold ~0.15s, then release (the genuine lift action)
            if 0.3 <= el < 0.45:
                rpc.set_input(A.BTN["a"], (0, 0)); pressed = True
            else:
                rpc.set_input(0, (0, 0))
            try:
                rots = bone_local_rots(rpc, actor)
            except Exception:
                time.sleep(period); continue
            if last is None or pose_delta(rots, last) > args.eps:
                rows.append((len(rows), round((time.time() - t0) * 1000, 1), rots))
                last = rots
            time.sleep(period)
        rpc.set_input(0, None)
        print(f"lift capture: heldActor=0x{held(rpc, actor):08x}")
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w") as f:
            f.write("cap,t_ms,bone,r0,r1,r2,r3,r4,r5,r6,r7,r8\n")
            for cap, tms, rots in rows:
                for b, r in enumerate(rots):
                    f.write(f"{cap},{tms},{b}," + ",".join(f"{v:.5f}" for v in r) + "\n")
        print(f"wrote {len(rows)} frames -> {args.out}")
        return

    # tap A repeatedly until heldActor set (give the lift action a few presses)
    grabbed = False
    t0 = time.time()
    while time.time() - t0 < 4.0:
        rpc.set_input(A.BTN["a"], (0, 0))
        time.sleep(0.08)
        rpc.set_input(0, (0, 0))
        time.sleep(0.08)
        if held(rpc, actor):
            grabbed = True
            break
    print(f"heldActor=0x{held(rpc, actor):08x} grabbed={grabbed}")
    if not grabbed:
        print("WARN: did not detect heldActor; capturing anyway")

    # settle carry-idle a moment
    time.sleep(0.3)

    circle = (0, 0) if args.no_walk else (0, 100)
    period = 1.0 / args.hz
    rows = []
    last = None
    t0 = time.time()
    while len(rows) < args.frames and time.time() - t0 < 8.0:
        rpc.set_input(0, circle)
        try:
            rots = bone_local_rots(rpc, actor)
        except Exception:
            time.sleep(period); continue
        if last is None or pose_delta(rots, last) > args.eps:
            rows.append((len(rows), round((time.time() - t0) * 1000, 1), rots))
            last = rots
        time.sleep(period)
    rpc.set_input(0, None)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as f:
        f.write("cap,t_ms,bone,r0,r1,r2,r3,r4,r5,r6,r7,r8\n")
        for cap, tms, rots in rows:
            for b, r in enumerate(rots):
                f.write(f"{cap},{tms},{b}," + ",".join(f"{v:.5f}" for v in r) + "\n")
    print(f"wrote {len(rows)} frames -> {args.out}")


if __name__ == "__main__":
    main()
