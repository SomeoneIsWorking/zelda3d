#!/usr/bin/env python3
"""fireglow_ab.py — matched-frame A/B measurement of the title fire-glow (g_title.cmb).

Walks ONE harness session (both engines embedded, forward-only) through several
(az, soh=az+408) matched pairs chosen to sample the g_title_fire.cmab gold-flicker
curve at distinct phases, snapshots both panes at each, and reports the mean RGB of
the GLOW REGION (the warm pixels around the wordmark) per pane — the quantitative
before/after surface for the fire-glow combiner port
(oot3d-decomp/docs/title_logo_fireglow_cmab.md §3.2).

Glow-region definition (same on both panes so the comparison is symmetric): within
the central wordmark box, pixels that are "warm" (R > B + 16 and R > 40) — i.e. the
gold wash + wordmark glyphs, excluding the night sky/terrain backdrop. Reported as
mean R,G,B over that mask plus the mask pixel count.

Usage:  source .env && tools/fireglow_ab.py [--tag before|after] [--frames 594 730 936]
Outputs: scratch/title_ab/fireglow_<tag>_az<N>.{az,soh}.ppm (+ printed table).
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from harness_process import spawn  # noqa: E402
from harness_paths import TITLE_STATE  # noqa: E402
from title_oracle_context import initial_title_cs  # noqa: E402

OUTDIR = REPO / "scratch" / "title_ab"
SAVESTATE = TITLE_STATE
SOH_TITLE_BOOT_STEPS = 232


def soh_step_intercept() -> int:
    return 2 * initial_title_cs() + SOH_TITLE_BOOT_STEPS


def read_ppm(path: Path) -> np.ndarray:
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        maxv = int(f.readline())
        assert maxv == 255
        return np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)


def glow_stats(img: np.ndarray) -> tuple[float, float, float, int]:
    """Mean RGB over the GOLD-hue flame mask inside the logo box.

    Box (full-frame 400x240 coords): x 110..300, y 40..190 — the shield + flame wash area
    both panes place the logo in during the title demo. Gold-hue mask separates the FLAME
    (orange/gold: G between 30% and 90% of R, B well below R) from the deep-red wordmark
    letters (G < 30% of R) and the green/blue scenery; threshold R > 60 rejects the dim
    night grass. Same box+mask on both panes so the comparison is symmetric."""
    box = img[40:190, 110:300].astype(np.int32)
    r, g, b = box[..., 0], box[..., 1], box[..., 2]
    mask = (r > 60) & (g * 10 > r * 3) & (g * 10 < r * 9) & (b * 2 < r)
    n = int(mask.sum())
    if n == 0:
        return 0.0, 0.0, 0.0, 0
    sel = box[mask]
    return float(sel[..., 0].mean()), float(sel[..., 1].mean()), float(sel[..., 2].mean()), n


def step_chunked(h, cmd: str, n: int, chunk: int = 20) -> None:
    while n > 0:
        k = min(chunk, n)
        h.send(f"{cmd} {k}")
        n -= k


# cs-frame <-> az_step conversion, reconstructed and cross-checked in
# oot3d-decomp/docs/title_logo_actor.md §8.1 against this tool's own matched-pair table:
# cs = checkpoint_cs + az / 2  =>  az = 2 * (cs - checkpoint_cs).


def cs_to_az(cs: int) -> int:
    return 2 * (cs - initial_title_cs())


def delta_stats(pre: np.ndarray, post: np.ndarray, box=(40, 190, 110, 300),
                 thresh: int = 8) -> tuple[float, float, float, int]:
    """Mean RGB *increase* (post-pre) over pixels that got measurably brighter, within
    the logo box. This isolates an ADDITIVE element's own contribution (the glow mesh
    blends src=SRC_ALPHA dst=ONE, i.e. it can only ADD light) independent of any RGB
    hue heuristic — so it can't confuse the wordmark's gold color with the glow's.
    `thresh` rejects small camera/background drift between the two frames (day/night
    scene keeps moving even though the 2D overlay itself is screen-locked)."""
    y0, y1, x0, x1 = box
    d = post[y0:y1, x0:x1].astype(np.int32) - pre[y0:y1, x0:x1].astype(np.int32)
    bright = d.max(axis=-1)
    mask = bright > thresh
    n = int(mask.sum())
    if n == 0:
        return 0.0, 0.0, 0.0, 0
    sel = d[mask]
    return float(sel[..., 0].mean()), float(sel[..., 1].mean()), float(sel[..., 2].mean()), n


def run_diff(argv):
    """Frame-difference mode: capture a PRE frame (glow alpha==0, cs just before the
    +0x1D0 ramp starts at cf466) and a POST frame (glow alpha==255, ramp complete at
    cf525) in both engines at the SAME az/soh pair math as the rest of this tool, then
    report the mean per-channel brightness DELTA over pixels that got brighter — the
    glow mesh's own additive contribution, with the wordmark (unchanged alpha across
    the window, ramp already finished at cf465) subtracted out by construction."""
    p = argparse.ArgumentParser(description=run_diff.__doc__)
    p.add_argument("--cs-pre", type=int, default=460, help="cs frame with glow alpha ~0")
    p.add_argument("--cs-post", type=int, nargs="+", default=[530],
                    help="cs frame(s) with glow alpha ramped/saturated")
    p.add_argument("--tag", default="diff")
    args = p.parse_args(argv)

    OUTDIR.mkdir(parents=True, exist_ok=True)
    (REPO / "scratch" / "logs").mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR",
                          str(REPO / "scratch" / "logs" / "fireglow_ab_harness.log"))

    az_pre = cs_to_az(args.cs_pre)
    posts = [(cs, cs_to_az(cs)) for cs in sorted(args.cs_post)]
    all_az = sorted({az_pre} | {az for _, az in posts})

    h = spawn(save_state=str(SAVESTATE))
    caps = {}
    try:
        r = h.send("soh_boot")
        if not r.startswith("ok"):
            sys.exit(f"soh_boot failed: {r}")
        az_cur = soh_cur = 0
        for az in all_az:
            soh = az + soh_step_intercept()
            step_chunked(h, "run", az - az_cur)
            step_chunked(h, "soh_step", soh - soh_cur)
            az_cur, soh_cur = az, soh
            base = OUTDIR / f"fireglow_{args.tag}_az{az}"
            h.send_multiline(f"snapshot {base}")
            caps[az] = (read_ppm(Path(str(base) + ".az.ppm")),
                        read_ppm(Path(str(base) + ".soh.ppm")))
    finally:
        h.quit()

    az_pre_az, az_pre_soh = caps[az_pre]
    print(f"\n== fire-glow additive-delta measurement (glow-only, wordmark subtracted) ==")
    print(f"pre: cs={args.cs_pre} az_step={az_pre} (glow alpha~0, wordmark ramp done)")
    print(f"{'cs':>5} {'az':>5} | {'Az dR':>6} {'dG':>6} {'dB':>6} {'px':>6} | "
          f"{'SoH dR':>6} {'dG':>6} {'dB':>6} {'px':>6} | R ratio soh/az")
    for cs, az in posts:
        post_az, post_soh = caps[az]
        a = delta_stats(az_pre_az, post_az)
        s = delta_stats(az_pre_soh, post_soh)
        ratio = (s[0] / a[0]) if a[0] > 0 else float("nan")
        print(f"{cs:>5} {az:>5} | {a[0]:6.1f} {a[1]:6.1f} {a[2]:6.1f} {a[3]:6d} | "
              f"{s[0]:6.1f} {s[1]:6.1f} {s[2]:6.1f} {s[3]:6d} | {ratio:6.3f}")


def main(argv):
    if argv and argv[0] == "--diff":
        return run_diff(argv[1:])
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--tag", default="run", help="label for output files (before/after)")
    p.add_argument("--frames", type=int, nargs="+", default=[594, 730, 936],
                   help="az frame numbers (ascending); soh pairs at az+408")
    args = p.parse_args(argv)

    if not SAVESTATE.exists():
        sys.exit(f"missing {SAVESTATE}")
    OUTDIR.mkdir(parents=True, exist_ok=True)
    (REPO / "scratch" / "logs").mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HARNESS_STDERR",
                          str(REPO / "scratch" / "logs" / "fireglow_ab_harness.log"))

    frames = sorted(args.frames)
    h = spawn(save_state=str(SAVESTATE))
    rows = []
    try:
        r = h.send("soh_boot")
        if not r.startswith("ok"):
            sys.exit(f"soh_boot failed: {r}")
        az_cur = soh_cur = 0
        for az in frames:
            soh = az + soh_step_intercept()
            step_chunked(h, "run", az - az_cur)
            step_chunked(h, "soh_step", soh - soh_cur)
            az_cur, soh_cur = az, soh
            base = OUTDIR / f"fireglow_{args.tag}_az{az}"
            h.send_multiline(f"snapshot {base}")
            a = glow_stats(read_ppm(Path(str(base) + ".az.ppm")))
            s = glow_stats(read_ppm(Path(str(base) + ".soh.ppm")))
            rows.append((az, soh, a, s))
    finally:
        h.quit()

    print(f"\n== fire-glow glow-region mean RGB ({args.tag}) — warm mask in central box ==")
    print(f"{'az':>5} {'soh':>5} | {'Az R':>6} {'G':>6} {'B':>6} {'px':>6} | "
          f"{'SoH R':>6} {'G':>6} {'B':>6} {'px':>6} | R ratio soh/az")
    for az, soh, a, s in rows:
        ratio = (s[0] / a[0]) if a[0] > 0 else float("nan")
        print(f"{az:>5} {soh:>5} | {a[0]:6.1f} {a[1]:6.1f} {a[2]:6.1f} {a[3]:6d} | "
              f"{s[0]:6.1f} {s[1]:6.1f} {s[2]:6.1f} {s[3]:6d} | {ratio:6.3f}")


if __name__ == "__main__":
    main(sys.argv[1:])
