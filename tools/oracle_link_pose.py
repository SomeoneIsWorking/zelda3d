#!/usr/bin/env python3
"""oracle_link_pose.py — capture OoT3D Link's LIVE per-bone LOCAL rotations per frame from Azahar.

The ORACLE half of the geometry-level Link animation parity sweep (SoH3D half = REPL `skindump`;
diff = tools/parity_pose_diff.py).

Reads Link's pose from the live **jointTable** at `PLAYER+0x254+0x78` (the SkelAnime controller; see
oot3d-decomp docs/player_anim_states.md). OoT3D stores per-bone LOCAL 3x4 float transforms, stride 13
floats/bone, in skeleton order: rotation 3x3 = floats {0,1,2 / 4,5,6 / 8,9,10}, translation = {3,7,11}.

CRITICAL: this jointTable updates every LOGIC frame regardless of rendering. The render-side bone-WORLD
matrix array (actor+0x25c chain, tools/link_skel_live.py) is a GPU-skinning product that stays FROZEN
under headless Azahar — do NOT use it for per-frame pose. We capture the local ROTATION per bone (the
pure anim output, parent-relative, directly comparable across rigs).

Azahar runs ~60fps; we oversample and keep only rows where the pose changed. Optionally hold the circle
pad + buttons for the whole capture to drive a state (locomotion, etc.) — the oracle analog of SoH3D's
`walkhold` / `btnhold`. Combine with `oot3d-decomp tools/link_ctl.py warp <entrance>` to reach open
ground (e.g. Kokiri Forest 0xEE) where Link can sustain a run.

Usage:
  tools/oracle_link_pose.py --frames 60 --out scratch/parity/oracle_run.csv --hold-circle 0,100
  tools/oracle_link_pose.py --frames 40 --out scratch/parity/oracle_idle.csv     # standing idle
Options: --hold-circle CX,CY (-100..100), --hold-buttons 'a b', --hz, --settle, --timeout, --stall.
"""
import argparse, math, os, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

OOT3D_TID = 0x0004000000033500
GPLAYSTATE = 0x0050AF34
SKELANIME_OFF = 0x254   # PLAYER -> embedded SkelAnime controller
JOINTTABLE_OFF = 0x78   # SkelAnime -> live local-transform array
BONE_STRIDE = 13 * 4    # 13 floats per bone (3x4 matrix + 1 pad)
NBONE = 25              # child/adult Link childlink_v2 rig


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


def bone_local_rots(rpc, actor):
    """Return [r0..r8]*NBONE — each bone's live LOCAL 3x3 rotation (row-major) from the jointTable."""
    jt = rpc.read32(actor + SKELANIME_OFF + JOINTTABLE_OFF)
    d = rpc.read(jt, NBONE * BONE_STRIDE)
    out = []
    for i in range(NBONE):
        m = struct.unpack_from("<12f", d, i * BONE_STRIDE)  # 3x4 row-major
        out.append((m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]))  # 3x3 rotation
    return out


def _pose_delta(a, b):
    """Total per-bone rotation change (deg) between two captures — for distinct-frame detection."""
    tot = 0.0
    for ra, rb in zip(a, b):
        tr = sum(ra[i] * rb[i] for i in range(9))
        c = max(-1.0, min(1.0, (tr - 1.0) / 2.0))
        tot += math.degrees(math.acos(c))
    return tot


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
    ap.add_argument("--eps", type=float, default=3.0,
                    help="min summed per-bone rotation change (deg) to count a new distinct frame")
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
    rows = []      # (cap, t_ms, [r0..r8]*NBONE)
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
            rots = bone_local_rots(rpc, actor)
        except Exception:
            misses += 1
            if misses > 200:
                print("oracle_link_pose: too many RPC read errors; aborting")
                break
            time.sleep(period)
            continue
        # distinct-frame key: total per-bone local-rotation change (deg) vs last kept frame
        changed = (last is None) or (_pose_delta(rots, last) > args.eps)
        if changed:
            rows.append((len(rows), round((now - t0) * 1000, 1), rots))
            last = rots
            last_change = now
        if circle is not None:
            rpc.set_input(btn_mask, circle)
        time.sleep(period)

    if circle is not None:
        rpc.set_input(0, None)

    with open(args.out, "w") as f:
        f.write("cap,t_ms,bone,r0,r1,r2,r3,r4,r5,r6,r7,r8\n")
        for cap, tms, rots in rows:
            for b, r in enumerate(rots):
                f.write(f"{cap},{tms},{b}," + ",".join(f"{v:.5f}" for v in r) + "\n")
    span = rows[-1][1] - rows[0][1] if len(rows) > 1 else 0.0
    print(f"oracle_link_pose: wrote {len(rows)} distinct frames over {span:.0f}ms -> {args.out}")


if __name__ == "__main__":
    main()
