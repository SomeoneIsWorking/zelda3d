#!/usr/bin/env python3
"""Per-draw mask brightness ratio: ours vs the oracle, at a MATCHED camera.

Consumes a tools/oracle_draw_isolate.py output dir (base.az.ppm + masks.npz, the
oracle's per-draw screen footprints) and one of OUR screenshots captured at the same
camera/time, and prints, per oracle draw, the mean-RGB luminance of both frames inside
that draw's own isolation mask and the ratio ours/oracle.

THREE things this tool enforces, each of which has already produced a bogus "renderer
deficit" when it was left to the operator:

1. HI-RES TEXTURE PACK SYMMETRY (hard error).  The 2026-07-22 `render.zora-ground-
   deficit` frontier step -- "ground 0.79 / walls 0.86, cause unknown" -- was ENTIRELY
   this: the oracle frames were captured before the harness learned to enable custom
   textures, so Azahar rendered the ROM's texels while our live game (launched from the
   repo root, where textures/ lives) rendered Henriko's 4K pack, whose Zora rock/ground
   replacements are ~20% darker.  Vanilla on both sides the same draws measure 0.99 and
   1.00.  So the oracle dir's texpack.txt and our run log must AGREE or this exits 2.
   Override only with --texpack-unchecked, and then only if you enjoy fiction.

2. HUD EXCLUSION (default on).  OoT3D's HUD lives on the 3DS BOTTOM screen, so the
   oracle's captured top-screen frame carries none of it while ours is fully overlaid.
   Every mask that reaches the screen edges is otherwise contaminated.

3. EXCLUSIVE PIXELS (--exclusive).  A mask is "pixels this draw CHANGES", so a mask
   under a translucent layer inherits that layer's error: at Zora, draw d3 (rock walls)
   measures 0.88 over its whole mask but 1.002 over the pixels no other draw touches --
   the deficit belonged to the water/waterfall drawn on top of it, not to the wall.
   Use --exclusive to attribute a residual to the draw that actually owns it.

Usage:
  python3 tools/tev_mask_ratio.py <drawiso_dir> <our_screenshot.png> [minpx]
        [--exclusive] [--no-hud] [--our-log scratch/logs/run.log]
        [--texpack-unchecked]
"""
import os
import re
import sys

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_img(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def oracle_texpack(d):
    """'on' / 'off' / None, from the texpack.txt oracle_draw_isolate.py records."""
    p = os.path.join(d, "texpack.txt")
    if not os.path.exists(p):
        return None
    m = re.search(r"mode=(\w+)", open(p).read())
    return m.group(1) if m else None


def our_texpack(log):
    """'on' / 'off' / None, from the game's own startup line in run.log.

    texpack.cpp prints exactly one of:
      [Zelda3D] texpack: N textures indexed under <root> (flip=F)   -> on
      [Zelda3D] texpack: no pack found (...)                        -> off
    """
    if not os.path.exists(log):
        return None
    for line in open(log, errors="replace"):
        if "[Zelda3D] texpack:" in line:
            return "off" if "no pack found" in line else "on"
    return None


def hud_mask(h, w):
    """Conservative box around OUR HUD; the oracle frame has none (3DS bottom screen)."""
    m = np.zeros((h, w), bool)
    m[0:70, :] = True          # hearts, magic bar, item slots
    m[h - 60:, 0:200] = True   # rupee counter
    return m


def main():
    args = list(sys.argv[1:])
    flags = {a for a in args if a.startswith("--")}
    ourlog = os.path.join(REPO, "scratch/logs/run.log")
    if "--our-log" in args:
        ourlog = args[args.index("--our-log") + 1]
        args.remove(ourlog)
    pos = [a for a in args if not a.startswith("--")]
    if len(pos) < 2:
        sys.exit(__doc__)
    d = pos[0].rstrip("/")
    ours_path = pos[1]
    minpx = int(pos[2]) if len(pos) > 2 else 500

    # --- 1. texture-pack symmetry -------------------------------------------------
    o_tp, u_tp = oracle_texpack(d), our_texpack(ourlog)
    print(f"texpack: oracle={o_tp}  ours={u_tp}  (log {ourlog})")
    if "--texpack-unchecked" not in flags:
        if o_tp is None or u_tp is None:
            sys.exit("REFUSING: texpack state unknown on one side (oracle needs texpack.txt "
                     "from a current oracle_draw_isolate.py run; ours needs the "
                     "'[Zelda3D] texpack' line in the run log). Pass --texpack-unchecked "
                     "only if you have verified both by hand.")
        if o_tp != u_tp:
            sys.exit(f"REFUSING: texpack ASYMMETRY (oracle={o_tp}, ours={u_tp}). This is not a "
                     "comparison, it is a measurement of the texture pack. Re-capture with "
                     "ZELDA3D_TEXPACK=off on our side (and ZELDA3D_HARNESS_TEXPACK=off on the "
                     "oracle) so both are vanilla, or turn the pack on for both.")

    oracle = read_img(f"{d}/base.az.ppm")
    ours = read_img(ours_path)
    if oracle.shape != ours.shape:
        sys.exit(f"shape mismatch: oracle {oracle.shape} vs ours {ours.shape} — not a matched capture")
    masks = np.load(f"{d}/masks.npz")
    H, W, _ = oracle.shape

    drop = np.zeros((H, W), bool) if "--no-hud" in flags else hud_mask(H, W)

    keys = list(masks.keys())
    raw = {k: masks[k].astype(bool) for k in keys}
    exclusive = "--exclusive" in flags
    if exclusive:
        union = np.zeros((H, W), np.int16)
        for m in raw.values():
            union += m
        sel = {k: (m & (union == 1)) for k, m in raw.items()}
        # SAY SO when a draw has no exclusive pixels. --exclusive is inert for a fully-overlapped
        # draw, and "no exclusive pixels" is indistinguishable from "nothing to report" unless the
        # tool names it: at Zora, d9 has 12072 mask pixels and ZERO exclusive ones, so every
        # --exclusive number for it was silently absent while the operator read the silence as
        # agreement (instrument I002, 2026-07-28).
        starved = [(k, int(raw[k].sum())) for k in keys if raw[k].any() and not sel[k].any()]
        if starved:
            print("NOTE: --exclusive has NO pixels for these draws (fully overlapped by later "
                  "layers); they are omitted below, and any full-mask number for them is "
                  "contaminated by whatever is drawn on top:")
            for k, n in starved:
                print(f"        {k}: {n} mask px, 0 exclusive")
    else:
        sel = raw

    def lum(img):
        return img @ np.array([0.299, 0.587, 0.114])

    lo, lu = lum(oracle), lum(ours)

    rows = []
    for key in keys:
        m = sel[key] & ~drop
        n = int(m.sum())
        if n < minpx:
            continue
        mo, mu = lo[m].mean(), lu[m].mean()
        co, cu = oracle[m].mean(axis=0), ours[m].mean(axis=0)
        rows.append((key, n, mo, mu, mu / max(mo, 1e-6), co, cu))
    rows.sort(key=lambda r: r[4])
    print(f"mode: {'EXCLUSIVE pixels' if exclusive else 'full mask'}, "
          f"HUD {'kept' if '--no-hud' in flags else 'excluded'}, minpx={minpx}")
    print(f"{'draw':>5} {'px':>7} {'oracle':>7} {'ours':>7} {'ratio':>6}  oracleRGB -> oursRGB")
    for key, n, mo, mu, r, co, cu in rows:
        print(
            f"{key:>5} {n:7d} {mo:7.1f} {mu:7.1f} {r:6.3f}  "
            f"({co[0]:.0f},{co[1]:.0f},{co[2]:.0f}) -> ({cu[0]:.0f},{cu[1]:.0f},{cu[2]:.0f})"
        )


if __name__ == "__main__":
    main()
