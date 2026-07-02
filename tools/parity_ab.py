#!/usr/bin/env python3
"""parity_ab.py — one-command coordinate-matched Zelda3D-vs-OoT3D-oracle A/B for a scene.

OoT3D shares N64/SoH entrance INDICES and spawn COORDS ([[zelda3d-oracle-entrance-match]]), so we can
drive BOTH engines to the same scene+coords and frame them identically. This wraps the fragile manual
steps (launch Zelda3D, warp oracle, keep Link alive, match camera, crop the 3DS top screen, composite):

  python3 tools/parity_ab.py 0xEE --time 0x6000 --name kokiri
  python3 tools/parity_ab.py 206  --time 0xC000 --name hf --cam "160 120 1850 160 30 1415"

Entrance arg = SoH/OoT3D entrance index (hex 0x.. or decimal). It launches Zelda3D there, warps the
oracle to the same index, ENSURES the immortal-Link keeper (tools/oracle_keeper.py) is running so the
oracle survives deadly spawns, screenshots both, and writes a side-by-side composite to
scratch/screenshots/ab_<name>_cmp.png (plus the raw _soh.png / _oracle_top.png).

NOTE on what to compare: lighting brightness/hue is OFF-LIMITS ([[zelda3d-stop-microtuning-lighting]],
#110/#111). Compare STRUCTURE / actor presence / poses / effects. The 3DS top screen is 5:3 (400x240);
Zelda3D is widescreen — the composite scales both to a common height, so judge content, not exact aspect.
"""
import os, sys, time, subprocess, struct

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(REPO, "tools")
SHOTDIR = os.path.join(REPO, "scratch", "screenshots")
DECOMP = os.path.expanduser("~/repo/oot3d-decomp")
sys.path.insert(0, TOOLS)


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, cwd=REPO, capture_output=True, text=True, **kw)


def keeper_running():
    out = subprocess.run(["pgrep", "-f", "oracle_keeper.py"], capture_output=True, text=True)
    return bool(out.stdout.strip())


def ensure_keeper():
    if keeper_running():
        return "already running"
    subprocess.Popen(
        "setsid nohup python3 tools/oracle_keeper.py --hz 12 "
        ">scratch/logs/oracle_keeper.log 2>&1",
        shell=True, cwd=REPO)
    time.sleep(1.5)
    return "started" if keeper_running() else "FAILED to start"


def repl(cmd, timeout=25):
    return subprocess.run(["python3", os.path.join(TOOLS, "zelda3d_repl.py"), "cmd", cmd],
                          cwd=REPO, capture_output=True, text=True, timeout=timeout).stdout.strip()


def oracle(cmds, timeout=40):
    return subprocess.run(["python3", os.path.join(TOOLS, "azahar_repl.py"), "-c", cmds],
                          cwd=REPO, capture_output=True, text=True, timeout=timeout).stdout.strip()


def compose(soh_png, oracle_top_png, out_png):
    from PIL import Image, ImageDraw
    a = Image.open(soh_png).convert("RGB")
    b = Image.open(oracle_top_png).convert("RGB")
    H = 480
    a = a.resize((int(a.width * H / a.height), H))
    b = b.resize((int(b.width * H / b.height), H))
    gap = 8
    cmp = Image.new("RGB", (a.width + gap + b.width, H + 28), (20, 20, 20))
    cmp.paste(a, (0, 28)); cmp.paste(b, (a.width + gap, 28))
    d = ImageDraw.Draw(cmp)
    d.text((6, 8), "Zelda3D", fill=(120, 220, 120))
    d.text((a.width + gap + 6, 8), "OoT3D oracle (3DS top screen)", fill=(220, 200, 120))
    cmp.save(out_png)
    return out_png


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    ent = args[0]
    dec = int(ent, 0)
    hexs = f"0x{dec:X}"
    time_arg = "0x6000"
    name = hexs
    cam = None
    orbit = None
    settle = 5.0
    i = 1
    while i < len(args):
        if args[i] == "--time": time_arg = args[i + 1]; i += 2
        elif args[i] == "--name": name = args[i + 1]; i += 2
        elif args[i] == "--cam": cam = args[i + 1]; i += 2          # "ex ey ez ax ay az"
        elif args[i] == "--orbit": orbit = args[i + 1]; i += 2      # "dist yaw pitch" (deg)
        elif args[i] == "--settle": settle = float(args[i + 1]); i += 2
        else: i += 1

    print(f"[parity_ab] entrance dec={dec} hex={hexs} time={time_arg} name={name}")
    print(f"[parity_ab] keeper: {ensure_keeper()}")

    print("[parity_ab] restarting Zelda3D…")
    r = subprocess.run(f"ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh restart {dec} {time_arg}",
                       shell=True, cwd=REPO, capture_output=True, text=True)
    print("  " + (r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()[-200:]))

    print("[parity_ab] warping oracle…")
    # Pass time_arg so the ORACLE lands at the same dayTime as Zelda3D (Market Day vs Night,
    # Kakariko Day vs Night etc. fork on this at scene-select; without pinning, oracle
    # kept whatever time its save state carried and the A/B silently compared different scenes).
    w = subprocess.run(["python3", os.path.join(DECOMP, "tools", "link_ctl.py"), "warp", hexs, time_arg],
                       cwd=DECOMP, capture_output=True, text=True, timeout=30)
    print("  " + (w.stdout.strip().splitlines()[-1] if w.stdout.strip() else "warp: no output"))

    time.sleep(settle)

    # --orbit: read the (matched) spawn pos from Zelda3D and frame Link from the same WORLD angle in
    # both engines -> identical framing with no per-scene coordinate guessing.
    if orbit and not cam:
        import math, re
        info = repl("posinfo")
        m = re.search(r"link=\(([-\d]+),([-\d]+),([-\d]+)\)", info)
        if m:
            sx, sy, sz = (float(m.group(1)), float(m.group(2)), float(m.group(3)))
            dist, yaw, pitch = (float(x) for x in orbit.split())
            ax, ay, az = sx, sy + 30.0, sz
            ry, rp = math.radians(yaw), math.radians(pitch)
            ex = ax + dist * math.cos(rp) * math.sin(ry)
            ey = ay + dist * math.sin(rp)
            ez = az + dist * math.cos(rp) * math.cos(ry)
            cam = f"{ex:.0f} {ey:.0f} {ez:.0f} {ax:.0f} {ay:.0f} {az:.0f}"
            print(f"[parity_ab] orbit cam (spawn {sx:.0f},{sy:.0f},{sz:.0f}) -> {cam}")
        else:
            print(f"[parity_ab] orbit: couldn't parse spawn from posinfo: {info!r}")

    soh_png = os.path.join(SHOTDIR, f"ab_{name}_soh.png")
    if cam:
        repl(f"cam {cam}")
    subprocess.run(["python3", os.path.join(TOOLS, "zelda3d_repl.py"), "shot", f"ab_{name}_soh"],
                   cwd=REPO, capture_output=True, text=True, timeout=30)

    if cam:
        c = cam.split()
        oc = f"cam_eye {c[0]} {c[1]} {c[2]}; cam_at {c[3]} {c[4]} {c[5]}; cam_freeze 0; sleep 1; "
    else:
        oc = ""
    oracle(oc + f"shot ab_{name}_oracle.png; cam_unfreeze")
    oracle_top = os.path.join(SHOTDIR, f"ab_{name}_oracle_top.png")

    out = os.path.join(SHOTDIR, f"ab_{name}_cmp.png")
    if os.path.exists(soh_png) and os.path.exists(oracle_top):
        compose(soh_png, oracle_top, out)
        print(f"[parity_ab] composite -> {out}")
    else:
        print(f"[parity_ab] MISSING shot(s): soh={os.path.exists(soh_png)} "
              f"oracle_top={os.path.exists(oracle_top)} — check both instances")


if __name__ == "__main__":
    main()
