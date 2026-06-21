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
  quit / exit
Button names: a b x y start select up down left right l r  (combine with + e.g. `tap l+r`)
"""
import os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import azahar_rpc as A

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTDIR = os.path.join(REPO, "scratch", "screenshots")


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


def do(rpc, line):
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
