#!/usr/bin/env python3
"""soh3d_repl.py — driver for the live SoH3D REPL (see soh3d.c SoH3D_ReplPoll).

Talks to a long-lived headless soh.elf (launched via tools/soh3d_repl_launch.sh)
over a control FIFO, so tint/scale/model experiments cost seconds instead of a
rebuild + 7-min headless render. Tooling-first: never hand-run xvfb again.

Subcommands:
  ready                 wait until the instance has warped in and the REPL is live
  cmd "<text>"          send a raw REPL command, print the reply
                        (mul/diff/tint/enable/scale/spawn/dump/state)
  shot <name> [x0 y0 x1 y1]   dump the current frame -> scratch/screenshots/<name>.png
                        (optional crop box: also writes <name>_crop.png, upscaled)
  zoom <name> x0 y0 x1 y1 [scale]   crop+upscale an existing shot for inspection
                        -> scratch/screenshots/<name>_zoom.png (default scale 3)
  region <name> x0 y0 x1 y1   mean RGB of a region of an existing shot
  isolate <nameA> <nameB> [thr]   diff two shots; report the changed-pixel region
                        (bbox + mean RGB in each). Use for same-instance A/B where
                        only one object changed (e.g. two tint levels isolate it).
  probe <name> [spawncmd]   spawn (default 'spawn kibako'), shot at mul 1 and mul 10,
                        isolate the changed object, print its full-bright mean
                        (brown R>G>B => texture applied; flat => not). One command.
  model <n64png> <s3dpng> [thr]   isolate model via A/B diff (wraps compare_render)

Examples:
  tools/soh3d_repl.py ready
  tools/soh3d_repl.py cmd "spawn kibako"
  tools/soh3d_repl.py cmd "mul 2.0"
  tools/soh3d_repl.py shot kibako_test
  tools/soh3d_repl.py region kibako_test 300 220 360 260
"""
import sys, os, time, subprocess

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIFO = os.environ.get("SOH3D_REPL", os.path.join(REPO, "scratch", "soh3d.ctl"))
OUT = FIFO + ".out"
SHOTDIR = os.path.join(REPO, "scratch", "screenshots")


def _out_size():
    return os.path.getsize(OUT) if os.path.exists(OUT) else 0


def send(cmd, timeout=3.0):
    """Send one command line; return any reply text appended to <fifo>.out."""
    if not os.path.exists(FIFO):
        sys.exit(f"FIFO {FIFO} not present — is the instance running? (soh3d_repl_launch.sh)")
    pre = _out_size()
    # Non-blocking open so a DEAD instance fails fast (ENXIO) instead of blocking
    # forever on the FIFO write — a blocking open(FIFO,"w") hangs with no reader,
    # which silently wedges interactive REPL iteration when soh.elf has crashed.
    try:
        fd = os.open(FIFO, os.O_WRONLY | os.O_NONBLOCK)
    except OSError as e:
        sys.exit(f"FIFO {FIFO} has no reader — instance dead/not ready? ({e.strerror})")
    try:
        os.write(fd, (cmd + "\n").encode())
    finally:
        os.close(fd)
    t0 = time.time()
    while time.time() - t0 < timeout:
        if _out_size() > pre:
            with open(OUT) as f:
                f.seek(pre)
                return f.read().strip()
        time.sleep(0.03)
    return "(no reply)"


def wait_ready(timeout=180):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if os.path.exists(OUT):
            return True
        time.sleep(0.25)
    return False


def shot(name, timeout=10.0):
    """Trigger an on-demand frame dump, wait for it, convert to PNG, return path."""
    os.makedirs(SHOTDIR, exist_ok=True)
    ppm = os.path.join(SHOTDIR, name + ".ppm")
    png = os.path.join(SHOTDIR, name + ".png")
    pre = os.path.getmtime(ppm) if os.path.exists(ppm) else 0
    send(f"dump {ppm}")
    t0 = time.time()
    last = -1
    while time.time() - t0 < timeout:
        if os.path.exists(ppm) and os.path.getmtime(ppm) > pre:
            sz = os.path.getsize(ppm)
            if sz > 1000 and sz == last:  # size stable across two polls => fully written
                break
            last = sz
        time.sleep(0.05)
    else:
        sys.exit("shot: timed out waiting for frame dump")
    subprocess.run(["magick", ppm, png], check=True)
    return png


def zoom(name, box, scale=3, suffix="_zoom"):
    """Crop a shot to box and upscale it, for inspection. Returns the path."""
    from PIL import Image
    src = os.path.join(SHOTDIR, name + ".png")
    im = Image.open(src).convert("RGB").crop(box)
    out = os.path.join(SHOTDIR, name + suffix + ".png")
    im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(out)
    return out


def region_mean(png, box):
    from PIL import Image
    im = Image.open(png).convert("RGB").crop(box)
    px = list(im.getdata())
    n = len(px) or 1
    return (sum(p[0] for p in px) / n, sum(p[1] for p in px) / n, sum(p[2] for p in px) / n, n)


def isolate(pngA, pngB, thr=20):
    """Pixels that differ between two shots of the SAME scene (only one object
    changed). Returns (count, bbox, meanA, meanB) over exactly those pixels."""
    from PIL import Image, ImageChops
    a = Image.open(pngA).convert("RGB")
    b = Image.open(pngB).convert("RGB")
    W, H = a.size
    dp = list(ImageChops.difference(a, b).getdata())
    co = [i for i, p in enumerate(dp) if max(p) > thr]
    if not co:
        return 0, None, (0, 0, 0), (0, 0, 0)
    pa, pb = a.load(), b.load()
    n = len(co)
    sa = [0, 0, 0]
    sb = [0, 0, 0]
    xs, ys = [], []
    for i in co:
        x, y = i % W, i // W
        xs.append(x)
        ys.append(y)
        ca, cb = pa[x, y], pb[x, y]
        for k in range(3):
            sa[k] += ca[k]
            sb[k] += cb[k]
    bbox = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
    return n, bbox, tuple(s / n for s in sa), tuple(s / n for s in sb)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    sub = sys.argv[1]
    if sub == "ready":
        print("ready" if wait_ready() else "TIMEOUT (instance not up)")
    elif sub == "cmd":
        print(send(sys.argv[2]))
    elif sub == "shot":
        png = shot(sys.argv[2])
        print(png)
        if len(sys.argv) >= 7:
            box = tuple(int(x) for x in sys.argv[3:7])
            print(zoom(sys.argv[2], box, suffix="_crop"))
    elif sub == "zoom":
        name = sys.argv[2]
        box = tuple(int(x) for x in sys.argv[3:7])
        scale = int(sys.argv[7]) if len(sys.argv) > 7 else 3
        print(zoom(name, box, scale))
    elif sub == "region":
        name = sys.argv[2]
        box = tuple(int(x) for x in sys.argv[3:7])
        png = os.path.join(SHOTDIR, name + ".png")
        r, g, b, n = region_mean(png, box)
        print(f"{name} {box} mean RGB=({r:.1f},{g:.1f},{b:.1f}) n={n}")
    elif sub == "isolate":
        a = os.path.join(SHOTDIR, sys.argv[2] + ".png")
        b = os.path.join(SHOTDIR, sys.argv[3] + ".png")
        thr = int(sys.argv[4]) if len(sys.argv) > 4 else 20
        n, bbox, ma, mb = isolate(a, b, thr)
        print(f"changed px={n} bbox={bbox}")
        print(f"  {sys.argv[2]} mean RGB=({ma[0]:.1f},{ma[1]:.1f},{ma[2]:.1f})")
        print(f"  {sys.argv[3]} mean RGB=({mb[0]:.1f},{mb[1]:.1f},{mb[2]:.1f})")
    elif sub == "probe":
        spawncmd = sys.argv[3] if len(sys.argv) > 3 else "spawn kibako"
        name = sys.argv[2]
        print("spawn:", send(spawncmd))
        time.sleep(0.4)
        send("mul 1")
        d1 = shot(name + "_d1")
        send("mul 10")
        fb = shot(name + "_fb")
        send("mul 1")
        n, bbox, m1, mfb = isolate(d1, fb)
        print(f"object px={n} bbox={bbox}")
        print(f"  full-bright mean RGB=({mfb[0]:.1f},{mfb[1]:.1f},{mfb[2]:.1f})  "
              f"(brown R>G>B => texture applied; flat/green => not)")
    elif sub == "model":
        thr = sys.argv[5] if len(sys.argv) > 5 else "24"
        subprocess.run(["python3", os.path.join(REPO, "tools", "compare_render.py"),
                        "model", sys.argv[2], sys.argv[3], thr])
    else:
        sys.exit(f"unknown subcommand {sub!r}")


if __name__ == "__main__":
    main()
