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
  record <name> [secs] [fps] [width] [span] [pan_deg] [x0 y0 x1 y1]   capture a burst of live
                        frames and encode scratch/screenshots/<name>.mp4 (animated issue evidence).
                        Duration HARD-CAPPED at 3s. Defaults: 3s, 15fps, 960px, auto span, no pan.
                        `pan_deg` slowly orbits the (held) camera that many degrees over the clip
                        for a parallax sweep (needs acam/cam/camfreeze first). Use 60fps + a slow
                        pan for smooth, readable 3D. Attach: kanban.py evidence <#> <...>.mp4
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
# Target a parallel game instance with SOH3D_INSTANCE=<N> (matches tools/soh3d_game.sh); an
# explicit SOH3D_REPL still wins. Empty/unset = the default "main" instance (unchanged).
_inst = os.environ.get("SOH3D_INSTANCE", "")
FIFO = os.environ.get("SOH3D_REPL") or os.path.join(
    REPO, "scratch", f"soh3d.{_inst}.ctl" if _inst else "soh3d.ctl")
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


def _dump_frame(ppm, timeout=10.0):
    """Send a `dump <ppm>` and block until the PPM is fully written (size stable)."""
    pre = os.path.getmtime(ppm) if os.path.exists(ppm) else 0
    send(f"dump {ppm}")
    t0 = time.time()
    last = -1
    while time.time() - t0 < timeout:
        if os.path.exists(ppm) and os.path.getmtime(ppm) > pre:
            sz = os.path.getsize(ppm)
            if sz > 1000 and sz == last:  # size stable across two polls => fully written
                return
            last = sz
        time.sleep(0.05)
    sys.exit("dump: timed out waiting for frame")


def shot(name, timeout=10.0):
    """Trigger an on-demand frame dump, wait for it, convert to PNG, return path."""
    os.makedirs(SHOTDIR, exist_ok=True)
    ppm = os.path.join(SHOTDIR, name + ".ppm")
    png = os.path.join(SHOTDIR, name + ".png")
    _dump_frame(ppm, timeout)
    subprocess.run(["magick", ppm, png], check=True)
    return png


def record(name, seconds=3.0, fps=15, width=960, span=None, box=None, pan=0.0):
    """Capture a burst of game frames and encode an MP4 (<=3s) for issue evidence.

    OUTPUT duration is HARD-CAPPED at 3s (GitHub-friendly). Captures seconds*fps frames and
    encodes at `fps`, so the clip plays for `seconds`. `width` downscales for a small upload
    (height auto-even); `box`=(x0,y0,x1,y1) crops before scaling.

    `span` = the WALL-CLOCK seconds to spread the capture over. The headless game advances its
    logic slowly (a cucco wing-flap cycle takes ~6s of real time), and back-to-back dumps return
    the SAME frame; so the frames must be spaced out in real time to capture motion. The captured
    span is then time-compressed into the <=3s clip. Default span auto-picks ~0.22s/frame
    (enough for the game to render a new frame), i.e. a sped-up clip. Pass span==seconds for a
    real-time clip (choppy if the game is slow). Returns the .mp4 path."""
    seconds = min(float(seconds), 3.0)  # hard 3s OUTPUT cap (per issue-evidence policy)
    fps = int(fps)
    nframes = max(1, int(round(seconds * fps)))
    if span is None:
        span = max(seconds, nframes * 0.22)  # ~0.22s/frame so each dump catches a fresh game frame
    span = float(span)
    os.makedirs(SHOTDIR, exist_ok=True)
    rec_dir = os.path.join(REPO, "scratch", "record", name)
    os.makedirs(rec_dir, exist_ok=True)
    for f in os.listdir(rec_dir):  # clear any prior frames
        os.remove(os.path.join(rec_dir, f))
    period = span / nframes
    # `pan` = total degrees to orbit the (frozen) camera around its look-at over the whole clip, for
    # a slow parallax sweep that makes the 3D shape readable. Stepped a little each frame via
    # `camorbit`. The camera must already be held (acam/cam/camfreeze) so camorbit rotates that eye.
    pan_step = (float(pan) / nframes) if pan else 0.0
    for i in range(nframes):
        t0 = time.time()
        _dump_frame(os.path.join(rec_dir, f"f{i:04d}.ppm"))
        if pan_step:
            send(f"camorbit {pan_step}")
        dt = time.time() - t0
        if dt < period:
            time.sleep(period - dt)
    out = os.path.join(SHOTDIR, name + ".mp4")
    vf = []
    if box:
        x0, y0, x1, y1 = box
        vf.append(f"crop={x1 - x0}:{y1 - y0}:{x0}:{y0}")
    if width:
        vf.append(f"scale={int(width)}:-2:flags=lanczos")
    vf.append("format=yuv420p")
    subprocess.run(
        ["ffmpeg", "-y", "-framerate", str(fps), "-i", os.path.join(rec_dir, "f%04d.ppm"),
         "-vf", ",".join(vf), "-c:v", "libx264", "-preset", "medium", "-crf", "23",
         "-movflags", "+faststart", out],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


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
    elif sub == "record":
        # record <name> [seconds] [fps] [width] [span] [pan_deg] [x0 y0 x1 y1]
        name = sys.argv[2]
        seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
        fps = int(sys.argv[4]) if len(sys.argv) > 4 else 15
        width = int(sys.argv[5]) if len(sys.argv) > 5 else 960
        span = float(sys.argv[6]) if len(sys.argv) > 6 else None
        pan = float(sys.argv[7]) if len(sys.argv) > 7 else 0.0
        box = tuple(int(x) for x in sys.argv[8:12]) if len(sys.argv) >= 12 else None
        mp4 = record(name, seconds, fps, width, span, box, pan)
        print(mp4)
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
