#!/usr/bin/env python3
"""azahar_repl.py — interactive console for the SoH3D 3DS oracle (OoT3D headless in Azahar).

One scriptable tool for everything the oracle needs over Azahar's RPC (UDP :45987, needs
enable_rpc_server=true + the SoH3D Azahar mods: Screenshot + Input packets): drive input, capture
the screen, and read emulated 3DS RAM. See issue #89, memory soh3d-azahar-oracle.

Launch OoT3D first (headless):
  source ./.env
  DISPLAY=:95 QT_QPA_PLATFORM=xcb setsid Azahar/build/bin/Release/azahar "$SOH3D_3DS_ROM" &
Then: tools/azahar_repl.py           # interactive
  or: tools/azahar_repl.py -c "start; sleep 1; shot t.png; read 8000000 32"   # batch (;-separated)

Commands:
  procs                     list emulated 3DS processes (auto-selects OoT3D on connect)
  sel <pid>                 select process for memory ops
  tap <btns> [ms]           press+release buttons (e.g. `tap start`, `tap a`, `tap up`)
  hold <btns>               hold buttons until `release` (e.g. `hold a`)
  release                   release all held buttons + circle
  circle <cx> <cy>          hold circle pad at cx,cy percent (-100..100); `circle off` to release
  walk <cx> <cy> <secs>     hold circle pad for N seconds then release (move in-world)
  shot [path.png]           capture framebuffer (PPM->PNG); default scratch/screenshots/azahar.png
  read <hexaddr> <size>     hexdump RAM at a 3DS vaddr
  r32 <hexaddr>             read a u32
  rf <hexaddr>              read an f32
  sleep <secs>              wait
  buttons                   list button names

Camera / free-cam primitives (analogous to SoH3D acam):
  cam_status                print current camera eye, at, Link pos, eye→at dist
  cam_eye <x> <y> <z>       set camera eye Vec3f (one frame; use before `shot`)
  cam_at  <x> <y> <z>       set camera look-at Vec3f (one frame; use before `shot`)
  cam_freeze [secs]         spam-write current eye+at at 100 Hz; secs=0 → background thread,
                            auto-released on next cam_freeze 0 or `cam_unfreeze`; secs>0 → blocks
  cam_unfreeze              stop any running cam_freeze background thread
  cam_frame <hexaddr> [secs]  teleport Link next to actor, freeze him, hold cam, screenshot
  cam_orbit <tx> <ty> <tz> [dist [yawDeg [pitchDeg]]]
                            place camera at polar angle around target (tx,ty,tz), freeze it, and
                            screenshot. Defaults: dist=200, yaw=0 (north), pitch=20 (slightly above).
                            yaw=0 → +Z side, yaw=90 → +X side. Analogous to SoH3D `acam`.
                            Use `cam_orbit link` to orbit current Link position automatically.

  quit / exit
Button names: a b x y start select up down left right l r  (combine with + e.g. `tap l+r`)
"""
import os, sys, time, struct, threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTDIR = os.path.join(REPO, "scratch", "screenshots")

# ── camera constants (from oot3d-decomp docs/ram_map.md, verified 2026-06-23) ──
GPLAYSTATE   = 0x0050AF34
CAM_EYE_OFF  = 0x1b8   # PlayState + offset → Vec3f eye
CAM_AT_OFF   = 0x1c4   # PlayState + offset → Vec3f look-at
CAM_EYE_NEXT = 0x3f0   # duplicate / interpolation buffer
CAM_AT_NEXT  = 0x3e4
ACTORCTX_OFF = 0x208C  # PlayState + offset → actorCtx
A_POS        = 0x08    # Actor + offset → world.pos Vec3f

# Background cam-freeze thread state
_cam_freeze_stop: threading.Event = threading.Event()
_cam_freeze_stop.set()  # "stopped" initially
_cam_freeze_thread: threading.Thread = None


def _cam_read_ps(rpc):
    return rpc.read32(GPLAYSTATE)


def _cam_read_eye(rpc, ps):
    return struct.unpack("<3f", rpc.read(ps + CAM_EYE_OFF, 12))


def _cam_read_at(rpc, ps):
    return struct.unpack("<3f", rpc.read(ps + CAM_AT_OFF, 12))


def _cam_write_eye(rpc, ps, x, y, z):
    data = struct.pack("<3f", x, y, z)
    rpc.write(ps + CAM_EYE_OFF, data)
    rpc.write(ps + CAM_EYE_NEXT, data)


def _cam_write_at(rpc, ps, x, y, z):
    data = struct.pack("<3f", x, y, z)
    rpc.write(ps + CAM_AT_OFF, data)
    rpc.write(ps + CAM_AT_NEXT, data)


def _link_head(rpc, ps):
    return rpc.read32(ps + ACTORCTX_OFF + 0x0C + 2 * 8 + 4)  # PLAYER category head


def _link_pos(rpc, h):
    return struct.unpack("<3f", rpc.read(h + A_POS, 12))


def _link_write_pos(rpc, h, x, y, z):
    rpc.write(h + A_POS, struct.pack("<3f", x, y, z))


def _cam_freeze_bg(eye_xyz, at_xyz, stop_evt):
    """Background thread: spam-write eye+at at ~125 Hz until stop_evt is set."""
    r2 = A.Rpc()
    try:
        for pid, tid, _ in r2.processes():
            if tid == 0x0004000000033500:
                r2.select(pid)
                break
    except Exception:
        return
    ex, ey, ez = eye_xyz
    ax, ay, az = at_xyz
    while not stop_evt.is_set():
        try:
            ps2 = r2.read32(GPLAYSTATE)
            _cam_write_eye(r2, ps2, ex, ey, ez)
            _cam_write_at(r2, ps2, ax, ay, az)
        except Exception:
            pass
        time.sleep(0.008)


def save_png(rpc, path):
    if not path.endswith(".png"):
        path = path + ".png"
    if not os.path.isabs(path):
        path = os.path.join(SHOTDIR, path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    ppm = path[:-4] + ".ppm"
    ok = rpc.screenshot(ppm)
    if not ok:
        return None
    try:
        from PIL import Image
        Image.open(ppm).save(path)
        return path
    except Exception:
        return ppm  # PIL missing: leave the PPM


def do(rpc, line):  # noqa: C901
    global _cam_freeze_stop, _cam_freeze_thread
    line = line.strip()
    if not line or line.startswith("#"):
        return True
    parts = line.split()
    cmd, args = parts[0].lower(), parts[1:]
    if cmd in ("quit", "exit", "q"):
        return False
    elif cmd == "procs":
        for pid, tid, name in rpc.processes():
            print(f"  pid={pid:<4} title_id=0x{tid:016x} {name}")
    elif cmd == "sel":
        rpc.select(int(args[0]))
        print(f"  selected pid {rpc.selected()}")
    elif cmd == "buttons":
        print("  " + " ".join(A.BTN))
    elif cmd == "tap":
        ms = float(args[1]) if len(args) > 1 else 120
        rpc.tap(args[0], hold_s=ms / 1000.0)
        print(f"  tapped {args[0]}")
    elif cmd == "hold":
        rpc.set_input(args[0])
        print(f"  holding {args[0]} (use `release`)")
    elif cmd == "release":
        rpc.set_input(0, None)
        rpc.set_touch(False)
        print("  released")
    elif cmd == "touch":
        if args and args[0] == "off":
            rpc.set_touch(False)
            print("  touch released")
        else:
            rpc.tap_touch(int(args[0]), int(args[1]))
            print(f"  touched ({args[0]},{args[1]})")
    elif cmd == "touchhold":
        rpc.set_touch(True, int(args[0]), int(args[1]))
        print(f"  touch held ({args[0]},{args[1]})")
    elif cmd == "circle":
        if args and args[0] == "off":
            rpc.set_input(0, None)
            print("  circle released")
        else:
            rpc.set_input(0, (int(args[0]), int(args[1])))
            print(f"  circle held ({args[0]},{args[1]})")
    elif cmd == "walk":
        cx, cy, secs = int(args[0]), int(args[1]), float(args[2])
        rpc.set_input(0, (cx, cy))
        time.sleep(secs)
        rpc.set_input(0, None)
        print(f"  walked ({cx},{cy}) for {secs}s")
    elif cmd == "shot":
        path = args[0] if args else "azahar.png"
        out = save_png(rpc, path)
        print(f"  shot -> {out}" if out else "  shot FAILED")
    elif cmd == "read":
        addr, size = int(args[0], 16), int(args[1])
        A._hexdump(addr, rpc.read(addr, size))
    elif cmd == "r32":
        addr = int(args[0], 16)
        v = rpc.read32(addr)
        print(f"  {addr:08x}: {v:08x} ({v})" if v is not None else "  (failed)")
    elif cmd == "rf":
        addr = int(args[0], 16)
        v = rpc.read_f32(addr)
        print(f"  {addr:08x}: {v}" if v is not None else "  (failed)")
    elif cmd == "save":
        slot = int(args[0]) if args else 1
        print(f"  savestate slot {slot}: {'OK' if rpc.savestate(slot, True) else 'FAILED'}")
    elif cmd == "load":
        slot = int(args[0]) if args else 1
        print(f"  loadstate slot {slot}: {'OK' if rpc.savestate(slot, False) else 'FAILED'}")
    elif cmd == "sleep":
        time.sleep(float(args[0]))
    # ── camera / free-cam primitives (analogous to SoH3D acam) ───────────────
    elif cmd == "cam_status":
        import math
        ps  = _cam_read_ps(rpc)
        eye = _cam_read_eye(rpc, ps)
        at  = _cam_read_at(rpc, ps)
        h   = _link_head(rpc, ps)
        lp  = _link_pos(rpc, h)
        d   = math.sqrt(sum((at[i] - eye[i]) ** 2 for i in range(3)))
        frozen = not _cam_freeze_stop.is_set()
        print(f"  PlayState @ {ps:#010x}")
        print(f"  Link:       ({lp[0]:8.2f},{lp[1]:8.2f},{lp[2]:8.2f})")
        print(f"  Camera eye: ({eye[0]:8.2f},{eye[1]:8.2f},{eye[2]:8.2f})")
        print(f"  Camera at:  ({at[0]:8.2f},{at[1]:8.2f},{at[2]:8.2f})  dist={d:.1f}")
        print(f"  cam_freeze: {'ACTIVE (background)' if frozen else 'off'}")
    elif cmd == "cam_eye":
        x, y, z = float(args[0]), float(args[1]), float(args[2])
        ps = _cam_read_ps(rpc)
        _cam_write_eye(rpc, ps, x, y, z)
        print(f"  cam eye set ({x:.1f},{y:.1f},{z:.1f})")
    elif cmd == "cam_at":
        x, y, z = float(args[0]), float(args[1]), float(args[2])
        ps = _cam_read_ps(rpc)
        _cam_write_at(rpc, ps, x, y, z)
        print(f"  cam at set ({x:.1f},{y:.1f},{z:.1f})")
    elif cmd == "cam_freeze":
        # Stop any existing background freeze first
        if not _cam_freeze_stop.is_set():
            _cam_freeze_stop.set()
            if _cam_freeze_thread:
                _cam_freeze_thread.join(timeout=0.3)
        secs = float(args[0]) if args else 0.0
        ps  = _cam_read_ps(rpc)
        eye = _cam_read_eye(rpc, ps)
        at  = _cam_read_at(rpc, ps)
        print(f"  cam_freeze eye=({eye[0]:.1f},{eye[1]:.1f},{eye[2]:.1f}) "
              f"at=({at[0]:.1f},{at[1]:.1f},{at[2]:.1f})")
        if secs > 0:
            # Blocking timed freeze — useful in -c batch scripts before a shot
            ex, ey, ez = eye
            ax, ay, az = at
            end = time.time() + secs
            while time.time() < end:
                ps2 = _cam_read_ps(rpc)
                _cam_write_eye(rpc, ps2, ex, ey, ez)
                _cam_write_at(rpc, ps2, ax, ay, az)
                time.sleep(0.008)
            print("  cam_freeze stopped.")
        else:
            # Background freeze until cam_unfreeze
            _cam_freeze_stop = threading.Event()
            _cam_freeze_thread = threading.Thread(
                target=_cam_freeze_bg, args=(eye, at, _cam_freeze_stop), daemon=True)
            _cam_freeze_thread.start()
            print("  cam_freeze running in background — use cam_unfreeze to stop")
    elif cmd == "cam_unfreeze":
        if not _cam_freeze_stop.is_set():
            _cam_freeze_stop.set()
            if _cam_freeze_thread:
                _cam_freeze_thread.join(timeout=0.3)
            print("  cam_freeze stopped.")
        else:
            print("  cam_freeze was not running.")
    elif cmd == "cam_frame":
        # cam_frame <hexaddr> [freeze_secs]: teleport Link near actor, freeze, screenshot.
        actor_addr  = int(args[0], 0)
        freeze_secs = float(args[1]) if len(args) > 1 else 1.5
        ps = _cam_read_ps(rpc)
        h  = _link_head(rpc, ps)
        axv, ayv, azv = struct.unpack("<3f", rpc.read(actor_addr + A_POS, 12))
        print(f"  actor @ {actor_addr:#010x}: ({axv:.1f},{ayv:.1f},{azv:.1f})")
        lx, ly, lz = axv, ayv, azv + 120
        _link_write_pos(rpc, h, lx, ly, lz)
        time.sleep(0.4)
        lk_stop = threading.Event()
        def _freeze_link_bg(stop_e, lx=lx, ly=ly, lz=lz):
            r2 = A.Rpc()
            for pid, tid, _ in r2.processes():
                if tid == 0x0004000000033500:
                    r2.select(pid)
                    break
            ps2 = r2.read32(GPLAYSTATE)
            h2  = r2.read32(ps2 + ACTORCTX_OFF + 0x0C + 2 * 8 + 4)
            while not stop_e.is_set():
                try:
                    _link_write_pos(r2, h2, lx, ly, lz)
                except Exception:
                    pass
                time.sleep(0.008)
        lt = threading.Thread(target=_freeze_link_bg, args=(lk_stop,), daemon=True)
        lt.start()
        time.sleep(freeze_secs)
        shot_path = os.path.join(SHOTDIR, f"actor_{actor_addr:#010x}.png")
        os.makedirs(SHOTDIR, exist_ok=True)
        ppm = shot_path[:-4] + ".ppm"
        ok  = rpc.screenshot(ppm)
        lk_stop.set()
        lt.join(timeout=0.3)
        if ok:
            try:
                from PIL import Image
                Image.open(ppm).save(shot_path)
                try:
                    os.unlink(ppm)
                except OSError:
                    pass
                print(f"  cam_frame -> {shot_path}")
            except ImportError:
                print(f"  cam_frame (PPM) -> {ppm}")
        else:
            print("  cam_frame shot FAILED")
    elif cmd == "cam_orbit":
        import math
        # cam_orbit <tx|link> <ty> <tz> [dist [yawDeg [pitchDeg]]]
        # If first arg is "link", read Link's current position as target.
        if args and args[0].lower() == "link":
            ps = _cam_read_ps(rpc)
            h  = _link_head(rpc, ps)
            tx, ty, tz = _link_pos(rpc, h)
            rest = args[1:]
        else:
            tx, ty, tz = float(args[0]), float(args[1]), float(args[2])
            rest = args[3:]
        dist     = float(rest[0]) if len(rest) > 0 else 200.0
        yaw_deg  = float(rest[1]) if len(rest) > 1 else 0.0
        pitch_deg = float(rest[2]) if len(rest) > 2 else 20.0
        # Convert polar to Cartesian eye position.
        # yaw=0 → eye on +Z side of target; yaw=90 → eye on +X side.
        # pitch>0 → eye above target plane.
        yaw   = math.radians(yaw_deg)
        pitch = math.radians(pitch_deg)
        ex = tx + dist * math.sin(yaw) * math.cos(pitch)
        ey = ty + dist * math.sin(pitch)
        ez = tz + dist * math.cos(yaw) * math.cos(pitch)
        ps = _cam_read_ps(rpc)
        _cam_write_eye(rpc, ps, ex, ey, ez)
        _cam_write_at(rpc, ps, tx, ty, tz)
        print(f"  cam_orbit target=({tx:.1f},{ty:.1f},{tz:.1f}) "
              f"dist={dist:.0f} yaw={yaw_deg:.0f}° pitch={pitch_deg:.0f}°")
        print(f"           eye=({ex:.1f},{ey:.1f},{ez:.1f})")
        # Freeze in background (replaces any existing freeze)
        if not _cam_freeze_stop.is_set():
            _cam_freeze_stop.set()
            if _cam_freeze_thread:
                _cam_freeze_thread.join(timeout=0.3)
        _cam_freeze_stop = threading.Event()
        _cam_freeze_thread = threading.Thread(
            target=_cam_freeze_bg, args=((ex, ey, ez), (tx, ty, tz), _cam_freeze_stop),
            daemon=True)
        _cam_freeze_thread.start()
        time.sleep(0.3)  # let the freeze take hold before returning
        # Take a screenshot automatically
        shot_name = f"orbit_{yaw_deg:.0f}yaw_{pitch_deg:.0f}pitch.png"
        shot_path = os.path.join(SHOTDIR, shot_name)
        out = save_png(rpc, shot_path)
        print(f"  cam_orbit shot -> {out}" if out else "  cam_orbit shot FAILED")
        print("  (cam_freeze now running; use cam_unfreeze to release)")
    else:
        print(f"  unknown command '{cmd}' (try: help/quit, see --help)")
    return True


def main():
    rpc = A.Rpc()
    # auto-select OoT3D if present
    try:
        for pid, tid, _ in rpc.processes():
            if tid == 0x0004000000033500:
                rpc.select(pid)
                print(f"connected: OoT3D pid={pid}")
                break
    except Exception as e:
        sys.exit(f"cannot reach Azahar RPC (:45987) — is it running with enable_rpc_server? {e}")

    if len(sys.argv) > 2 and sys.argv[1] == "-c":
        for line in sys.argv[2].split(";"):
            print(f"> {line.strip()}")
            if not do(rpc, line):
                break
        return
    print("azahar oracle REPL — type commands (quit to exit, see --help)")
    while True:
        try:
            line = input("azahar> ")
        except (EOFError, KeyboardInterrupt):
            print()
            break
        try:
            if not do(rpc, line):
                break
        except Exception as e:
            print(f"  error: {e}")


if __name__ == "__main__":
    if "--help" in sys.argv or "-h" in sys.argv:
        print(__doc__)
    else:
        main()
