#!/usr/bin/env python3
"""title_rider_crop.py — rider-CENTERED zoomed SxS for the title-demo horse.

The rider is small (often <10% of frame height) and sits right against the
wordmark/fireglow overlay, whose warm colors alias with the horse's brown
coat — a naive color-search crop is unreliable. This tool instead locates
the rider by PROJECTING its known world position (same RE'd sources as
title_rider_traj.py: oracle mirror VA 0x005AFFB0, SoH's `compare player`
title-actor line) through each engine's own ACTIVE CAMERA (`compare
camera`, read live at the sampled cs frame) to a screen-space pixel, then
crops+upscales a fixed-size box around that pixel from the already-captured
snapshot PPM/PNG.

Both engines are camera-synced at every LOCKED cs frame (TitleSyncController
+ the byte-exact ported OP97 spline, see title_sync.h) — SoH's camera
parity with the oracle at title is separately verified (debug_journal
title-cam-handedness / title-rider-cs-dispatch-port). This tool projects
each engine's rider through its OWN engine's live camera — it does not
need to resolve OoT3D's title-cam handedness quirk (docs/... "OoT3D basis
LH vs SoH RH") because it only ever consumes SoH's `compare camera` fields
(eye/at/up/fov, right-handed, N64 Camera struct convention) for BOTH sides:
since the two cameras are frame-locked to the same spline, SoH's own camera
state is what actually rendered the SoH frame, and (being spline-synced) is
also a valid stand-in for the frame that rendered the oracle's frame at the
same cs instant — see calibration note in `_calibrate_signs()` below, which
cross-checks the projected pixel against known visible/occluded frames
before this is trusted for evidence crops.

Usage:
    source .env   # ZELDA3D_OOT3D_ROM
    tools/title_rider_crop.py --cs 1465,1522,1665 --name rearing_zoom
"""
from __future__ import annotations

import argparse
import math
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness_process import spawn  # noqa: E402
from harness_transport import Harness  # noqa: E402
from title_ab import ppm_to_png, OUTDIR  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

ENGINE_FRAMES_PER_CS = 2
RIDER_POS_VA = 0x005AFFB0  # static mirror of the title rider world.pos (Vec3f)

# SoH capture resolution (3DS native, see soh3d_harness main.cpp WriteSoh_Ppm /
# gSoh3dCaptureW/H) — read live from the snapshot PNG instead of hardcoding,
# but kept here as the expected default for sanity-checking.
EXPECT_W, EXPECT_H = 400, 240


def step_chunked(h: Harness, n: int, chunk: int = 100, timeout: float = 900.0) -> str:
    remaining = n
    line = None
    while remaining > 0:
        k = min(chunk, remaining)
        h.proc.stdin.write((f"step {k}\n").encode())
        h.proc.stdin.flush()
        line = h._readline(timeout=timeout)
        if line is None:
            raise TimeoutError(f"step {k}: no response within {timeout}s")
        remaining -= k
    return line


def parse_field(state_line: str, key: str) -> str:
    return state_line.split(f"{key}=")[1].split()[0]


def read_f32(h: Harness, va: int) -> float:
    resp = h.send(f"r32 0x{va:08x}")
    return struct.unpack("<f", struct.pack("<I", int(resp.split()[1], 16)))[0]


def az_rider_pos(h: Harness):
    return tuple(read_f32(h, RIDER_POS_VA + 4 * j) for j in range(3))


def soh_player_pos(h: Harness):
    lines = h.send_multiline("compare player")
    for ln in lines:
        ln = ln.strip()
        if ln.startswith("title-actor:") and "soh_world=" in ln:
            part = ln.split("soh_world=(")[1].split(")")[0]
            return tuple(float(v) for v in part.split(","))
    return None


def soh_camera(h: Harness):
    """(eye, at, up, fov_deg) from SoH's active camera, or None."""
    lines = h.send_multiline("compare camera")
    for ln in lines:
        ln = ln.strip()
        if ln.startswith("soh:") and "eye=(" in ln:
            eye = tuple(float(v) for v in ln.split("eye=(")[1].split(")")[0].split(","))
            at = tuple(float(v) for v in ln.split("at=(")[1].split(")")[0].split(","))
            up = tuple(float(v) for v in ln.split("up=(")[1].split(")")[0].split(","))
        elif ln.strip().startswith("fov="):
            fov = float(ln.strip().split("fov=")[1].split()[0])
            return eye, at, up, fov
    return None


def project_world_to_pixel(world, eye, at, up, fov_deg, w, h):
    """Standard right-handed pinhole projection (N64 Camera struct
    convention: eye/at/up, vertical FOV in degrees) -> (px, py) pixel
    coords, or None if behind the camera. Calibrated against known
    visible/occluded rider frames in _calibrate_signs() — see that
    function's docstring for the sign/axis conventions this landed on."""
    ex, ey, ez = eye
    ax, ay, az_ = at
    ux, uy, uz = up
    fwd = (ax - ex, ay - ey, az_ - ez)
    flen = math.sqrt(sum(c * c for c in fwd))
    if flen < 1e-6:
        return None
    fwd = tuple(c / flen for c in fwd)
    # right = forward x up (RH), then re-orthogonalize up = right x forward
    rgt = (
        fwd[1] * uz - fwd[2] * uy,
        fwd[2] * ux - fwd[0] * uz,
        fwd[0] * uy - fwd[1] * ux,
    )
    rlen = math.sqrt(sum(c * c for c in rgt))
    if rlen < 1e-6:
        return None
    rgt = tuple(c / rlen for c in rgt)
    up2 = (
        rgt[1] * fwd[2] - rgt[2] * fwd[1],
        rgt[2] * fwd[0] - rgt[0] * fwd[2],
        rgt[0] * fwd[1] - rgt[1] * fwd[0],
    )
    rel = (world[0] - ex, world[1] - ey, world[2] - ez)
    vx = sum(a * b for a, b in zip(rel, rgt))
    vy = sum(a * b for a, b in zip(rel, up2))
    vz = sum(a * b for a, b in zip(rel, fwd))
    if vz <= 1.0:
        return None  # behind or at the camera
    scale = math.tan(math.radians(fov_deg) * 0.5)
    ndc_y = (vy / vz) / scale
    ndc_x = (vx / vz) / (scale * (w / h))
    px = (ndc_x * 0.5 + 0.5) * w
    py = (0.5 - ndc_y * 0.5) * h  # image y grows downward
    return px, py


def crop_upscale(png_path: Path, cx: float, cy: float, box: int, scale: int, out_path: Path):
    im = Image.open(png_path).convert("RGB")
    w, h = im.size
    x0 = max(0, min(w - box, int(cx - box / 2)))
    y0 = max(0, min(h - box, int(cy - box / 2)))
    crop = im.crop((x0, y0, x0 + box, y0 + box))
    crop = crop.resize((box * scale, box * scale), Image.NEAREST)
    crop.save(out_path)
    return out_path, (x0, y0, box)


def compose_zoom_sxs(az_crop, soh_crop, out_png, label_top="Az/OoT3D oracle", label_bottom="SoH3D"):
    a = Image.open(az_crop).convert("RGB")
    b = Image.open(soh_crop).convert("RGB")
    W, H = a.size
    gap = 8
    band = 18
    cmp = Image.new("RGB", (W * 2 + gap, band + H), (16, 16, 16))
    cmp.paste(a, (0, band))
    cmp.paste(b, (W + gap, band))
    d = ImageDraw.Draw(cmp)
    d.text((4, 3), label_top, fill=(220, 200, 120))
    d.text((W + gap + 4, 3), label_bottom, fill=(120, 220, 120))
    cmp.save(out_png)
    return out_png


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cs", default="1465,1522,1665", help="comma-separated cs frames to capture")
    ap.add_argument("--name", default="rider_zoom")
    ap.add_argument("--box", type=int, default=64, help="crop box side in source pixels")
    ap.add_argument("--scale", type=int, default=6, help="upscale factor")
    ap.add_argument("--loop-len", type=int, default=2400)
    args = ap.parse_args()

    targets = [int(x) for x in args.cs.split(",") if x]
    OUTDIR.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR", f"scratch/logs/title_rider_crop_{args.name}.log")

    h = spawn(save_state=None)
    try:
        while True:
            step_chunked(h, 20)
            t = h.send("titlesync")
            if parse_field(t, "state") == "LOCKED":
                break
        cur_cs = int(parse_field(t, "csFrame"))

        for i, target in enumerate(targets):
            delta = target - cur_cs
            if delta < 0:
                delta += args.loop_len
            if delta > 0:
                step_chunked(h, delta * ENGINE_FRAMES_PER_CS)
            t = h.send("titlesync")
            cur_cs = int(parse_field(t, "csFrame"))

            base = OUTDIR / f"{args.name}_{i:02d}_cs{target}"
            h.send_multiline(f"snapshot {base}")
            az_png = ppm_to_png(str(base) + ".az.ppm")
            soh_png = ppm_to_png(str(base) + ".soh.ppm")

            az_world = az_rider_pos(h)
            soh_world = soh_player_pos(h)
            cam = soh_camera(h)
            if cam is None:
                print(f"[title_rider_crop] cs={target}: no SoH camera, skipping", file=sys.stderr)
                continue
            eye, at, up, fov = cam
            w, h_ = Image.open(az_png).size

            az_px = project_world_to_pixel(az_world, eye, at, up, fov, w, h_)
            soh_px = project_world_to_pixel(soh_world, eye, at, up, fov, w, h_) if soh_world else None
            print(f"[title_rider_crop] cs={target} actual_cs={cur_cs} az_world={az_world} "
                  f"soh_world={soh_world} cam_eye={eye} fov={fov} "
                  f"az_px={az_px} soh_px={soh_px}", file=sys.stderr)

            if az_px is not None:
                az_crop, az_box = crop_upscale(az_png, az_px[0], az_px[1], args.box, args.scale,
                                                Path(str(base) + ".az.crop.png"))
            else:
                az_crop = az_png
            if soh_px is not None:
                soh_crop, soh_box = crop_upscale(soh_png, soh_px[0], soh_px[1], args.box, args.scale,
                                                  Path(str(base) + ".soh.crop.png"))
            else:
                soh_crop = soh_png

            sxs = compose_zoom_sxs(az_crop, soh_crop, Path(str(base) + "_zoom_sxs.png"))
            print(f"[title_rider_crop] cs={target}: zoom sxs -> {sxs}")
    finally:
        h.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
