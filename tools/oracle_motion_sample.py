#!/usr/bin/env python3
"""oracle_motion_sample.py — sample a live OoT3D actor's per-frame pos/rot from the Azahar oracle.

The 3DS-side half of the BEHAVIORAL motion-parity harness (SoH3D side = REPL `asample`, diff =
tools/motion_parity.py). Walks the durable static actor chain (see oot3d-decomp docs/actor_layout.md):

    gPlayState (0x0050af34, .data)  -> PlayState
    PlayState + 0x208C              -> actorCtx
    actorCtx + 0x0C                  = ActorListEntry[12]{ s32 count; Actor* head }  (stride 8)
    walk each list via Actor.next (+0x130); per-actor pos@0x08 (Vec3f), rot@0x14 (Vec3s), id@0x00.

Resolves ONE target actor's RAM address, then polls just its pos/rot at high wall-clock rate.
Azahar runs at fixed ~60fps, so we OVERSAMPLE and keep only rows where (pos,rot) changed from the
previous kept row — that recovers the per-frame state sequence for a moving actor without a frame
counter. (A truly static actor collapses to one row; that's fine — motion parity targets motion.)

Usage:
  tools/oracle_motion_sample.py --id 0x6F --frames 120 --out scratch/motion/oracle.csv
  tools/oracle_motion_sample.py --nearest --frames 120          # nearest non-player to Link
  tools/oracle_motion_sample.py --player  --frames 120          # Link himself
  tools/oracle_motion_sample.py --addr 0x098f4010 --frames 120  # explicit actor vaddr
Options: --hz <poll Hz, default 400>, --settle <s before sampling, default 0>.
"""
import argparse, math, os, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

OOT3D_TID = 0x0004000000033500
GPLAYSTATE = 0x0050AF34
ACTORCTX_OFF = 0x208C
SCENENUM_OFF = 0x104
A_NEXT = 0x130
CATS = ["SWITCH", "BG", "PLAYER", "EXPLOSIVE", "NPC", "ENEMY", "PROP",
        "ITEMACTION", "MISC", "BOSS", "DOOR", "CHEST"]


def select_oot3d(rpc):
    for pid, tid, _ in rpc.processes():
        if tid == OOT3D_TID:
            rpc.select(pid)
            return pid
    return None


def read_state(rpc, addr):
    """Return (pos(x,y,z), rot(x,y,z)) for the actor at vaddr by reading +0x08..+0x1A."""
    d = rpc.read(addr + 8, 18)
    px, py, pz = struct.unpack_from("<fff", d, 0)
    rx, ry, rz = struct.unpack_from("<hhh", d, 12)
    return (px, py, pz), (rx, ry, rz)


def enumerate_actors(rpc):
    ps = rpc.read32(GPLAYSTATE)
    ctx = ps + ACTORCTX_OFF
    scene = rpc.read32(ps + SCENENUM_OFF) & 0xFFFF
    lists = rpc.read(ctx + 0xC, 12 * 8)
    actors = []
    for c in range(12):
        cnt, head = struct.unpack_from("<iI", lists, c * 8)
        a, guard = head, 0
        while a and guard < cnt + 4:
            d = rpc.read(a, 0x1A)
            aid = struct.unpack_from("<H", d, 0)[0]
            pos = struct.unpack_from("<fff", d, 8)
            actors.append({"addr": a, "id": aid, "cat": c, "pos": pos})
            a = rpc.read32(a + A_NEXT)
            guard += 1
    return ps, scene, actors


def pick_target(rpc, args):
    if args.addr is not None:
        return args.addr, None
    ps, scene, actors = enumerate_actors(rpc)
    player = next((a for a in actors if a["cat"] == 2), None)
    if args.player:
        if player is None:
            sys.exit("oracle_motion_sample: no PLAYER actor found")
        return player["addr"], player
    pool = [a for a in actors if a["cat"] != 2]
    if args.id is not None:
        pool = [a for a in pool if a["id"] == args.id]
        if not pool:
            ids = sorted({f"0x{a['id']:X}" for a in actors})
            sys.exit(f"oracle_motion_sample: no actor id 0x{args.id:X} in scene {scene}. live ids: {ids}")
    if player is not None:
        px, _, pz = player["pos"]
        pool.sort(key=lambda a: (a["pos"][0] - px) ** 2 + (a["pos"][2] - pz) ** 2)
    if not pool:
        sys.exit(f"oracle_motion_sample: no candidate actors in scene {scene}")
    return pool[0]["addr"], pool[0]


def scan_movers(rpc, dwell=2.0):
    """Sample every live actor's pos/rot at t0 and t0+dwell; report the ones that moved.
    Finds autonomous (Link-independent) motion targets for the parity harness."""
    ps, scene, a0 = enumerate_actors(rpc)
    by_addr = {a["addr"]: a for a in a0}
    rot0 = {}
    for a in a0:
        try:
            _, rot0[a["addr"]] = read_state(rpc, a["addr"])
        except Exception:
            pass
    time.sleep(dwell)
    print(f"scan: scene {scene}, {len(a0)} actors, dwell {dwell}s — movers:")
    found = 0
    for a in a0:
        try:
            pos1, rot1 = read_state(rpc, a["addr"])
        except Exception:
            continue
        dpos = math.dist(a["pos"], pos1)
        drot = abs((rot1[1] - rot0.get(a["addr"], rot1)[1] + 32768) % 65536 - 32768)
        if dpos > 0.5 or drot > 50:
            found += 1
            print(f"  @{a['addr']:08x} id=0x{a['id']:X} cat={CATS[a['cat']]:5} "
                  f"dpos={dpos:7.2f} drotY={drot:6d}  pos={tuple(round(v,0) for v in pos1)}")
    if not found:
        print("  (none moved — scene has no autonomous movers)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true",
                    help="report which live actors moved over a 2s dwell (find motion targets)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--id", type=lambda s: int(s, 0), default=None, help="nearest actor with this id")
    g.add_argument("--addr", type=lambda s: int(s, 0), default=None, help="explicit actor vaddr")
    g.add_argument("--player", action="store_true", help="sample Link (PLAYER) himself")
    g.add_argument("--nearest", action="store_true", help="nearest non-player actor to Link")
    ap.add_argument("--frames", type=int, default=120, help="distinct frames to capture")
    ap.add_argument("--hz", type=float, default=400.0, help="poll rate (oversample)")
    ap.add_argument("--settle", type=float, default=0.0, help="seconds to wait before sampling")
    ap.add_argument("--timeout", type=float, default=8.0, help="max wall-clock seconds to sample")
    ap.add_argument("--stall", type=float, default=1.5,
                    help="stop early if no new distinct frame for this many seconds "
                         "(actor went static — e.g. a fall that has landed)")
    ap.add_argument("--lift-y", type=float, default=0.0,
                    help="add this to the target's world Y immediately before sampling "
                         "(e.g. free-fall parity test); pos.y is at actor+0x0C")
    ap.add_argument("--hold-circle", default=None, metavar="CX,CY",
                    help="hold the circle pad at CX,CY (-100..100, e.g. 0,100 = full forward) "
                         "for the whole capture — drive Link's locomotion (parity vs SoH3D walkhold)")
    ap.add_argument("--hold-buttons", default="", help="buttons to hold with --hold-circle (e.g. 'a')")
    ap.add_argument("--out", default="scratch/motion/oracle.csv")
    args = ap.parse_args()

    rpc = A.Rpc()
    if select_oot3d(rpc) is None:
        sys.exit("oracle_motion_sample: OoT3D process not found on Azahar RPC (:45987)")

    if args.scan:
        scan_movers(rpc)
        return

    addr, info = pick_target(rpc, args)
    if info:
        print(f"oracle_motion_sample: target @{addr:08x} id=0x{info['id']:X} "
              f"cat={CATS[info['cat']]} pos={tuple(round(v,1) for v in info['pos'])}")
    else:
        print(f"oracle_motion_sample: target @{addr:08x} (explicit)")

    if args.settle > 0:
        time.sleep(args.settle)

    if args.lift_y:
        cur = rpc.read_f32(addr + 0x0C)
        rpc.write_f32(addr + 0x0C, cur + args.lift_y)
        print(f"oracle_motion_sample: lifted Y {cur:.1f} -> {cur + args.lift_y:.1f}")

    circle = None
    btn_mask = 0
    if args.hold_circle:
        cx, cy = (int(v) for v in args.hold_circle.split(","))
        circle = (cx, cy)
        btn_mask = A.buttons_mask(args.hold_buttons) if args.hold_buttons else 0
        rpc.set_input(btn_mask, circle)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    period = 1.0 / args.hz
    t0 = time.time()
    rows = []
    last = None
    misses = 0
    last_change = t0
    while len(rows) < args.frames:
        now = time.time()
        if now - t0 > args.timeout:
            print(f"oracle_motion_sample: wall-clock timeout ({args.timeout}s)")
            break
        if rows and now - last_change > args.stall:
            print(f"oracle_motion_sample: actor static for {args.stall}s — stopping")
            break
        try:
            pos, rot = read_state(rpc, addr)
        except Exception:
            misses += 1
            if misses > 200:
                print("oracle_motion_sample: too many RPC read errors; aborting")
                break
            time.sleep(period)
            continue
        key = (round(pos[0], 3), round(pos[1], 3), round(pos[2], 3), rot[0], rot[1], rot[2])
        if key != last:
            rows.append((len(rows), round((now - t0) * 1000, 1), pos, rot))
            last = key
            last_change = now
        if circle is not None:
            rpc.set_input(btn_mask, circle)  # re-assert held input each poll
        time.sleep(period)

    if circle is not None:
        rpc.set_input(0, None)  # release

    with open(args.out, "w") as f:
        f.write("frame,t_ms,posx,posy,posz,rotx,roty,rotz\n")
        for fr, tms, pos, rot in rows:
            f.write(f"{fr},{tms},{pos[0]:.3f},{pos[1]:.3f},{pos[2]:.3f},{rot[0]},{rot[1]},{rot[2]}\n")
    span = rows[-1][1] - rows[0][1] if len(rows) > 1 else 0.0
    print(f"oracle_motion_sample: wrote {len(rows)} distinct frames over {span:.0f}ms -> {args.out}")


if __name__ == "__main__":
    main()
