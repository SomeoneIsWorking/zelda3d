#!/usr/bin/env python3
"""oracle_link_pose.py — capture OoT3D Link's LIVE per-bone world matrices per frame from Azahar.

The ORACLE half of the geometry-level Link animation parity sweep (SoH3D half = REPL `skindump`,
which dumps the resolved CSAB bone-world matrices aw=skin*bind; diff = tools/parity_pose_diff.py).

Reads Link's 25-bone childlink_v2 skeleton each frame via the live bone-matrix chain
(oot3d-decomp tools/link_skel_live.py):
    PLAYER actor + 0x25c -> skeleton obj ; +0x20 -> anim player ; +0xd4 -> bone array + 0xc
Each bone matrix is column-major 4x3 (0x30 bytes); bone WORLD POSITION = (m[6], m[10], m[2]).

Azahar runs ~60fps; we oversample and keep only rows where the pose changed (recovering the per-frame
sequence without a frame counter), exactly like oracle_motion_sample.py. Optionally hold the circle pad
+ buttons for the whole capture to drive a state (locomotion, etc.) — the oracle analog of SoH3D's
`walkhold` / `btnhold`.

Usage:
  tools/oracle_link_pose.py --frames 60 --out scratch/parity/oracle_run.csv --hold-circle 0,100
  tools/oracle_link_pose.py --frames 40 --out scratch/parity/oracle_idle.csv     # standing idle
Options: --hold-circle CX,CY (-100..100), --hold-buttons 'a b', --hz, --settle, --timeout, --stall.
"""
import argparse, os, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

OOT3D_TID = 0x0004000000033500
GPLAYSTATE = 0x0050AF34
SKEL_OFF = 0x25C        # actor -> embedded skeleton object
ANIMPLAYER_OFF = 0x20   # skelobj -> anim player
MATPTR_OFF = 0xD4       # animplayer -> bone array + 0xc
NBONE = 25
STRIDE = 0x30


def select_oot3d(rpc):
    for pid, tid, _ in rpc.processes():
        if tid == OOT3D_TID:
            rpc.select(pid)
            return pid
    return None


def link_actor(rpc):
    ps = rpc.read32(GPLAYSTATE)
    if not ps:
        sys.exit("oracle_link_pose: no PlayState (title screen?) — boot into a scene first")
    # actorCtx(+0x208C) -> ActorListEntry[2] (PLAYER cat) head: +0x0C + 2*8 + 4
    return rpc.read32(ps + 0x208C + 0x0C + 2 * 8 + 4)


def bone_world_positions(rpc, actor):
    """Return [(x,y,z)]*NBONE — each bone's live world position (m[6],m[10],m[2])."""
    animplayer = rpc.read32(actor + SKEL_OFF + ANIMPLAYER_OFF)
    arr = rpc.read32(animplayer + MATPTR_OFF) - 0xC
    d = rpc.read(arr, NBONE * STRIDE)
    out = []
    for i in range(NBONE):
        m = struct.unpack_from("<12f", d, i * STRIDE)
        out.append((m[6], m[10], m[2]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=60, help="distinct frames to capture")
    ap.add_argument("--hz", type=float, default=400.0, help="poll rate (oversample)")
    ap.add_argument("--settle", type=float, default=0.3, help="seconds to wait before sampling")
    ap.add_argument("--timeout", type=float, default=10.0, help="max wall-clock seconds")
    ap.add_argument("--stall", type=float, default=1.5, help="stop early if pose static this long")
    ap.add_argument("--hold-circle", default=None, metavar="CX,CY",
                    help="hold circle pad at CX,CY (-100..100, e.g. 0,100 = full forward)")
    ap.add_argument("--hold-buttons", default="", help="buttons to hold (e.g. 'a b')")
    ap.add_argument("--eps", type=float, default=1.0, help="min summed pos change to count a new frame")
    ap.add_argument("--out", default="scratch/parity/oracle.csv")
    args = ap.parse_args()

    rpc = A.Rpc()
    if select_oot3d(rpc) is None:
        sys.exit("oracle_link_pose: OoT3D process not found on Azahar RPC (:45987)")
    actor = link_actor(rpc)
    print(f"oracle_link_pose: Link actor @{actor:08x}")

    circle = None
    btn_mask = 0
    if args.hold_circle:
        cx, cy = (int(v) for v in args.hold_circle.split(","))
        circle = (cx, cy)
        btn_mask = A.buttons_mask(args.hold_buttons) if args.hold_buttons else 0
        rpc.set_input(btn_mask, circle)
    if args.settle > 0:
        if circle is not None:
            t = time.time()
            while time.time() - t < args.settle:
                rpc.set_input(btn_mask, circle)
                time.sleep(1.0 / args.hz)
        else:
            time.sleep(args.settle)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    period = 1.0 / args.hz
    t0 = time.time()
    rows = []      # (cap, t_ms, [(x,y,z)]*NBONE)
    last = None
    last_change = t0
    misses = 0
    while len(rows) < args.frames:
        now = time.time()
        if now - t0 > args.timeout:
            print(f"oracle_link_pose: wall-clock timeout ({args.timeout}s)")
            break
        if rows and now - last_change > args.stall:
            print(f"oracle_link_pose: pose static for {args.stall}s — stopping")
            break
        try:
            pos = bone_world_positions(rpc, actor)
        except Exception:
            misses += 1
            if misses > 200:
                print("oracle_link_pose: too many RPC read errors; aborting")
                break
            time.sleep(period)
            continue
        # distinct-frame key: total movement of all bones vs last kept frame
        if last is not None:
            d = sum(abs(pos[i][k] - last[i][k]) for i in range(NBONE) for k in range(3))
            changed = d > args.eps
        else:
            changed = True
        if changed:
            rows.append((len(rows), round((now - t0) * 1000, 1), pos))
            last = pos
            last_change = now
        if circle is not None:
            rpc.set_input(btn_mask, circle)
        time.sleep(period)

    if circle is not None:
        rpc.set_input(0, None)

    with open(args.out, "w") as f:
        f.write("cap,t_ms,bone,x,y,z\n")
        for cap, tms, pos in rows:
            for b, (x, y, z) in enumerate(pos):
                f.write(f"{cap},{tms},{b},{x:.3f},{y:.3f},{z:.3f}\n")
    span = rows[-1][1] - rows[0][1] if len(rows) > 1 else 0.0
    print(f"oracle_link_pose: wrote {len(rows)} distinct frames over {span:.0f}ms -> {args.out}")


if __name__ == "__main__":
    main()
