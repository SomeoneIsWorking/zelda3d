#!/usr/bin/env python3
"""title_ab.py — CONTENT-MATCHED SoH3D-vs-oracle title-cutscene frame A/B.

THE PROBLEM THIS FIXES: SoH3D's title cutscene and the OoT3D oracle
(Azahar, embedded in tools/soh3d_harness) each free-run their own scripted
title-demo clock from the same save state. Their clocks are NOT 1:1 past
roughly step 360 (see debug_journal/2026-07-08-title-sky-color.md and
2026-07-08-title-parity-audit-ranked.md) — so "same step/frame NUMBER"
does NOT mean "same content", and comparing by number alone produced
three false "bugs" in one session (sky-color, moon-halo, a wrong-asset
overlay). `soh_titlecs <n>` is even worse: it's a raw cursor override
that also re-derives gSaveContext.dayTime from a schedule table, so
forcing it independently of Az moves the WHOLE lighting/sky/moon state
to an uncalibrated instant — never use it for A/B calibration.

THE FIX: soh3d_harness (tools/soh3d_harness) embeds BOTH engines in one
process and can step them INDEPENDENTLY (`run <n>` = Az-only, `soh_step
<n>` = SoH-only). This tool uses that to empirically CORRELATE frame
indices by CONTENT (a structure-similarity score on a small grayscale
downsample — robust to the lighting/color differences that are a
separate, already-documented divergence, sensitive to POSITION/POSE/
SILHOUETTE which is what "same instant" actually means here):

  1. Run Az forward to the target `az_frames` (fixed reference frame).
  2. Bulk-step SoH forward to an ESTIMATED matching frame (piecewise
     linear from empirically-verified anchor pairs — see ANCHORS below).
  3. Fine-sweep SoH frame-by-frame across a window around the estimate,
     scoring each candidate against the fixed Az reference.
  4. Report the best-scoring SoH frame + the local score curve (so a
     caller can see it's a genuine, unambiguous peak, not a plateau —
     that curve IS the "content-match confidence check").
  5. Emit a labeled side-by-side PNG + a coarse per-region RGB diff table.

Both directions of stepping are FORWARD-ONLY within one harness process
(no rewind), so the search always runs Az once to the target and then
walks SoH monotonically upward through the window — no engine ever
reboots mid-search.

Usage:
  source .env   # ZELDA3D_OOT3D_ROM
  tools/title_ab.py calibrate 600                  # find matching SoH frame for az=600
  tools/title_ab.py calibrate 600 --margin 40 --name mid
  tools/title_ab.py ab 600 --soh 604 --name mid     # skip search, use a known-good pair
  tools/title_ab.py list-anchors                    # print the baked correspondence table

Outputs land in scratch/title_ab/ (gitignored): <name>.az.ppm/.png,
<name>.soh.ppm/.png, <name>_sxs.png (labeled composite), and the
calibration curve is printed to stdout (not a file — it's the proof,
meant to be read in the moment).
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "tools"
sys.path.insert(0, str(TOOLS))
import harness_ctl as hc  # noqa: E402

OUTDIR = REPO / "scratch" / "title_ab"
SAVESTATE = REPO / "scratch" / "title_settled.state"

# ---------------------------------------------------------------------------
# Empirically-verified (az_frames, soh_frames) anchor pairs — same content,
# confirmed BY EYE (rider pose, hill silhouette, moon position all aligned)
# in debug_journal/2026-07-08-title-parity-audit-ranked.md and re-confirmed
# live while building this tool (scratch/title_ab/{anchor360,earlyB}_sxs.png).
# Do not add an anchor without an eyeballed SxS to back it — this table is
# the thing that keeps the whole tool honest.
#
#   az=200 -> soh=397   found by calibrate's content search (score 0.93,
#                        unambiguous peak), NOT a linear extrapolation from
#                        the other anchors — SoH's own N64-splash/boot
#                        sequence eats ~250-300 SoH-side frames of "no
#                        title-cs content yet" before its title cutscene
#                        starts, so early az targets need a SoH frame far
#                        ahead of the naive az==soh guess.
#   az=360 -> soh=449   found by calibrate with a wide (+-200) margin: the
#                        score climbs smoothly from soh~360 (0.695) all the
#                        way to a sharp, unambiguous peak at soh=449 (0.912)
#                        before falling off a cliff after soh~530 (score
#                        collapses to ~0.13-0.28 — a real content-regime
#                        change, not noise). This SUPERSEDES the older
#                        "raw step 360/360, matched by eye" claim from
#                        debug_journal/2026-07-08-title-sky-color.md /
#                        title-moon-size.md: that pairing was in the right
#                        neighborhood (same rider-crossing-field shot) but
#                        NOT the true peak — off by ~89 frames. This is
#                        exactly the failure mode this tool exists to fix:
#                        "looks about right by eye" vs "is provably the
#                        best-scoring instant".
#
# SUPERSEDED / DO NOT REUSE: the older audit anchor (az=396 -> soh=413,
# from debug_journal/2026-07-08-title-parity-audit-ranked.md "pair2") is
# INCONSISTENT with the (360,449) anchor above — soh must be non-decreasing
# in az (both clocks only run forward, no loops in this range), and 413 <
# 449 violates that. It was another "close enough by eye" measurement, not
# a content-search result; treat it as falsified, not as data.
#
# CONSEQUENCE: the az->soh relationship is NOT monotonic-slope across the
# splash region (soh lags by ~200 frames at az=200, then the lag drops to
# ~90 by az=360) — piecewise-LINEAR interpolation between anchors is only a
# rough SEARCH SEED (see estimate_soh_frame); calibrate()'s fine
# content-search is what actually establishes the match. Pass a generous
# --margin for az < ~400 (the splash-recovery zone) — the default is sized
# for the post-splash region, not this one.
ANCHORS = [(200, 397), (360, 449)]
# NOTE: no anchor below az=200 — we have not established a content match
# there (see title_parity doc); az<200 is untested territory for this tool.
# NOTE: az >~ 650-700 hits Azahar's scripted "Legend of Zelda OoT3D" logo
# overlay card, which SoH3D does not currently render at all — calibrate()
# correctly reports a low, non-peaked score there (no fix belongs in THIS
# tool; see the title_ab doc for that finding).


def estimate_soh_frame(az_frames: int) -> int:
    """Piecewise-linear extrapolation/interpolation from ANCHORS. This is
    only a SEARCH SEED — the fine sweep in calibrate() is what actually
    verifies the match by content; a wrong estimate just costs a wider
    bulk-step, never a wrong answer."""
    pts = ANCHORS
    if az_frames <= pts[0][0]:
        return pts[0][1]
    for (ax0, sx0), (ax1, sx1) in zip(pts, pts[1:]):
        if ax0 <= az_frames <= ax1:
            if ax1 == ax0:
                return sx0
            t = (az_frames - ax0) / (ax1 - ax0)
            return round(sx0 + t * (sx1 - sx0))
    # past the last anchor: extrapolate at the last segment's slope
    (ax0, sx0), (ax1, sx1) = pts[-2], pts[-1]
    slope = (sx1 - sx0) / (ax1 - ax0)
    return round(sx1 + slope * (az_frames - ax1))


def load_gray_small(path, size=(48, 28)) -> np.ndarray:
    im = Image.open(path).convert("L").resize(size, Image.BILINEAR)
    arr = np.asarray(im, dtype=np.float64).flatten()
    arr -= arr.mean()
    n = np.linalg.norm(arr)
    return arr / n if n > 1e-6 else arr


def content_score(path_a, path_b, size=(48, 28)) -> float:
    """Normalized cross-correlation in [-1, 1] on a downsampled grayscale
    structure map. Each side is independently zero-meaned + unit-norm'd,
    so overall brightness/hue differences (the SEPARATE, already-tracked
    lighting divergence — see debug_journal/2026-07-08-title-*.md) don't
    move the score; position/pose/silhouette differences do. That's the
    right invariance for "is this the same instant", not "does it look
    identical"."""
    a = load_gray_small(path_a, size)
    b = load_gray_small(path_b, size)
    return float(np.dot(a, b))


def region_grid_diff(png_a, png_b, cols=4, rows=3):
    """Coarse per-region mean-RGB delta table between two MATCHED frames —
    the quantitative half of the A/B (the visual SxS is the qualitative
    half). Large deltas here are real render-state divergences to chase,
    small ones are noise/anti-aliasing."""
    a = np.asarray(Image.open(png_a).convert("RGB"), dtype=np.float64)
    b = np.asarray(Image.open(png_b).convert("RGB"), dtype=np.float64)
    h, w, _ = a.shape
    rows_out = []
    for r in range(rows):
        for c in range(cols):
            y0, y1 = h * r // rows, h * (r + 1) // rows
            x0, x1 = w * c // cols, w * (c + 1) // cols
            pa = a[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)
            pb = b[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)
            d = pa - pb
            rows_out.append(((x0, y0, x1, y1), tuple(pa), tuple(pb), tuple(d)))
    return rows_out


def compose_sxs(az_png, soh_png, out_png, label_top="Az/OoT3D oracle", label_bottom="SoH3D"):
    a = Image.open(az_png).convert("RGB")
    b = Image.open(soh_png).convert("RGB")
    W, H = a.size
    gap = 4
    band = 18
    cmp = Image.new("RGB", (W, band + H + gap + band + H), (16, 16, 16))
    cmp.paste(a, (0, band))
    cmp.paste(b, (0, band + H + gap + band))
    d = ImageDraw.Draw(cmp)
    d.text((4, 3), label_top, fill=(220, 200, 120))
    d.text((4, band + H + gap + 3), label_bottom, fill=(120, 220, 120))
    cmp.save(out_png)
    return out_png


def ppm_to_png(ppm_path) -> Path:
    png_path = Path(str(ppm_path)[:-4] + ".png") if str(ppm_path).endswith(".ppm") else Path(str(ppm_path) + ".png")
    Image.open(ppm_path).convert("RGB").save(png_path)
    return png_path


def _require_env():
    if not os.environ.get("ZELDA3D_OOT3D_ROM"):
        sys.exit("ZELDA3D_OOT3D_ROM not set — run `source .env` first (see tools/rom_provision.sh)")
    if not SAVESTATE.exists():
        sys.exit(f"missing {SAVESTATE} — a title_settled.state save-state (Az, right before the "
                  "title cutscene starts) is required as the shared t=0 for both engines")


def _step_chunked(h, cmd: str, n: int, chunk: int = 100) -> None:
    """Send `<cmd> <k>` repeatedly in <=chunk-sized pieces instead of one
    `<cmd> <n>` call. Some title-cs frame ranges (e.g. the demo-loop's
    logo/scene-transition around az~700-900) cost noticeably more real time
    per emulated frame than the steady-state night/field frames, and a
    single big `run`/`soh_step` call can exceed harness_ctl's 60s default
    read timeout even though the harness itself is still working fine —
    chunking keeps every single wire round-trip well under that timeout."""
    remaining = n
    while remaining > 0:
        k = min(chunk, remaining)
        h.send(f"{cmd} {k}")
        remaining -= k


def _spawn():
    OUTDIR.mkdir(parents=True, exist_ok=True)
    (REPO / "scratch" / "logs").mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR", str(REPO / "scratch" / "logs" / "title_ab_harness.log"))
    h = hc.spawn(save_state=str(SAVESTATE))
    r = h.send("soh_boot")
    if not r.startswith("ok"):
        h.quit()
        sys.exit(f"soh_boot failed: {r}")
    return h


def cmd_list_anchors(_args):
    print("az_frames  soh_frames  (both stepped from the same title_settled.state, verified by eye)")
    for a, s in ANCHORS:
        print(f"{a:9d}  {s:10d}")


def cmd_calibrate(args):
    _require_env()
    if args.az < 360 and args.margin < 150:
        print(f"[title_ab] WARNING: az={args.az} is in the splash-recovery zone (SoH's own "
              "N64-splash eats ~250-300 SoH frames before title-cs content starts, and the lag "
              "collapses nonlinearly by az~360 — see ANCHORS note in this file). A margin of "
              f"{args.margin} is likely too small to find the true match; consider --margin 200+.",
              file=sys.stderr)
    h = _spawn()
    try:
        print(f"[title_ab] az: run {args.az} frames...", file=sys.stderr)
        _step_chunked(h, "run", args.az)
        az_ppm = OUTDIR / f"{args.name}.az.ppm"
        # snapshot writes BOTH; harmless — az buffer doesn't change during
        # the soh-only fine sweep below, so re-snapshotting it each
        # iteration just re-dumps the same bytes.
        h.send_multiline(f"snapshot {OUTDIR / args.name}")
        az_ref = str(az_ppm)

        guess = estimate_soh_frame(args.az)
        window_start = max(0, guess - args.margin)
        window_end = guess + args.margin
        print(f"[title_ab] estimate soh~={guess}, sweeping [{window_start},{window_end}]",
              file=sys.stderr)

        if window_start > 0:
            _step_chunked(h, "soh_step", window_start)
        cur = window_start

        best = (-2.0, None)
        curve = []
        tmp_base = OUTDIR / f"{args.name}_probe"
        while cur <= window_end:
            h.send("soh_step 1")
            cur += 1
            h.send_multiline(f"snapshot {tmp_base}")
            score = content_score(az_ref, str(tmp_base) + ".soh.ppm")
            curve.append((cur, score))
            if score > best[0]:
                best = (score, cur)
                # keep the best probe snapshot around
                Image.open(str(tmp_base) + ".soh.ppm").save(OUTDIR / f"{args.name}.soh.ppm")

        curve.sort(key=lambda t: t[0])
        print("[title_ab] score curve (soh_frame: score):", file=sys.stderr)
        for n, s in curve:
            marker = "  <== best" if n == best[1] else ""
            print(f"  {n:6d}: {s:+.4f}{marker}", file=sys.stderr)

        # peak sharpness: best vs runner-up (excluding neighbors of the peak)
        others = sorted((s for n, s in curve if abs(n - best[1]) > 2), reverse=True)
        runner_up = others[0] if others else float("nan")
        print(f"[title_ab] BEST MATCH: az={args.az} soh={best[1]}  score={best[0]:.4f}  "
              f"(runner-up outside +-2 frames: {runner_up:.4f}, margin {best[0]-runner_up:.4f})")

        az_png = ppm_to_png(az_ref)
        soh_png = ppm_to_png(OUTDIR / f"{args.name}.soh.ppm")
        sxs = compose_sxs(az_png, soh_png, OUTDIR / f"{args.name}_sxs.png")
        print(f"[title_ab] sxs -> {sxs}")

        print("[title_ab] per-region RGB delta (Az - SoH), 4x3 grid:")
        for box, pa, pb, dlt in region_grid_diff(az_png, soh_png):
            print(f"  {box}: az=({pa[0]:.0f},{pa[1]:.0f},{pa[2]:.0f}) "
                  f"soh=({pb[0]:.0f},{pb[1]:.0f},{pb[2]:.0f}) "
                  f"d=({dlt[0]:+.0f},{dlt[1]:+.0f},{dlt[2]:+.0f})")
        return best[1]
    finally:
        h.quit()


def cmd_ab(args):
    """Skip the search — drive straight to a known-good (az, soh) pair
    (e.g. one already established by `calibrate`) and just emit the SxS +
    diff. Useful once a mapping is known, to avoid re-searching."""
    _require_env()
    h = _spawn()
    try:
        print(f"[title_ab] az: run {args.az}, soh: soh_step {args.soh}", file=sys.stderr)
        _step_chunked(h, "run", args.az)
        _step_chunked(h, "soh_step", args.soh)
        base = OUTDIR / args.name
        h.send_multiline(f"snapshot {base}")
        az_png = ppm_to_png(str(base) + ".az.ppm")
        soh_png = ppm_to_png(str(base) + ".soh.ppm")
        score = content_score(str(base) + ".az.ppm", str(base) + ".soh.ppm")
        print(f"[title_ab] content-match score at az={args.az}/soh={args.soh}: {score:.4f}")
        sxs = compose_sxs(az_png, soh_png, OUTDIR / f"{args.name}_sxs.png")
        print(f"[title_ab] sxs -> {sxs}")
        print("[title_ab] per-region RGB delta (Az - SoH), 4x3 grid:")
        for box, pa, pb, dlt in region_grid_diff(az_png, soh_png):
            print(f"  {box}: az=({pa[0]:.0f},{pa[1]:.0f},{pa[2]:.0f}) "
                  f"soh=({pb[0]:.0f},{pb[1]:.0f},{pb[2]:.0f}) "
                  f"d=({dlt[0]:+.0f},{dlt[1]:+.0f},{dlt[2]:+.0f})")
    finally:
        h.quit()


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    la = sub.add_parser("list-anchors", help="print the verified (az,soh) correspondence anchors")
    la.set_defaults(func=cmd_list_anchors)

    ca = sub.add_parser("calibrate", help="find the SoH frame matching a given Az frame by content")
    ca.add_argument("az", type=int, help="target Az (oracle) title-cs frame count (from title_settled.state)")
    ca.add_argument("--margin", type=int, default=30, help="search window +-N frames around the estimate "
                    "(the splash-recovery zone below az~360 needs far more, e.g. 150-250 — see ANCHORS note)")
    ca.add_argument("--name", default=None, help="output basename (default title_az<az>)")
    ca.set_defaults(func=cmd_calibrate)

    ab = sub.add_parser("ab", help="emit SxS + diff for an already-known (az, soh) matched pair")
    ab.add_argument("az", type=int)
    ab.add_argument("--soh", type=int, required=True)
    ab.add_argument("--name", default=None)
    ab.set_defaults(func=cmd_ab)

    args = p.parse_args(argv)
    if getattr(args, "name", None) is None and args.cmd in ("calibrate", "ab"):
        args.name = f"title_az{args.az}"
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]) and 0)
