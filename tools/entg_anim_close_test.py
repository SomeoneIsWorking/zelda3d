#!/usr/bin/env python3
"""entg_anim_close_test.py — assert EN_TG plays the tg_matsu CSAB, not mu_matsu.

Structured close-test for the named EN_TG animation divergence: the audit
(debug_journal/2026-07-02-market-day-actor-audit.md) records that the dancing
couple's OoT3D CSAB is `tg_matsu.csab`. But zelda3d_animmap.inc:661 currently
maps `gDancingCoupleAnim` -> `mu_matsu` — the HAGGLING-TOWNSPERSON wait animation
(also in `zelda_mu.zar`, drives EN_MU). Because EN_TG and EN_MU share the ZAR
and there is no per-CMB scoping on that map entry, EN_TG's live `gDancingCoupleAnim`
resolves to `mu_matsu` and the dance clip never plays — the couple stands in
EN_MU's idle pose.

The runtime observable is a low-volume `[Zelda3D animPlay]` log line added to
`Zelda3D_UpdateAnim` (zelda3d_anim.cpp): one line per (modelId, animName) pair,
per process. This test:
  1. Warps to Market Day (ent 0xB1 dayTime 0x8001) where the single EN_TG spawns.
  2. Parses run.log to find the model id auto-loaded for `couple.cmb`.
  3. Grep the animPlay probe for that model id.

RED on HEAD: the animPlay line for the couple model references `mu_matsu`.
GREEN once the animap fix routes `gDancingCoupleAnim` -> `tg_matsu` for the
couple.cmb specifically.
"""
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")


def main():
    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "restart", "0xB1", "0x8001"],
        env={**os.environ, "ZELDA3D_HEADLESS": "1"},
        cwd=REPO, capture_output=True, text=True, timeout=90,
    )
    time.sleep(6)
    # Warm the actor set so EN_TG's Draw path (and Zelda3D_UpdateAnim) run.
    subprocess.run(
        ["python3", "tools/zelda3d_repl.py", "cmd", "actorsnear 10000"],
        cwd=REPO, capture_output=True, text=True, timeout=15,
    )
    time.sleep(1)

    if not os.path.exists(LOG):
        print(f"[test] run.log missing at {LOG}")
        sys.exit(2)
    with open(LOG, "r", errors="replace") as f:
        log = f.read()

    m = re.search(
        r"\[Zelda3D\] auto-loaded model (\d+) \(/actor/zelda_mu\.zar\s*\|\s*couple\)",
        log,
    )
    if m is None:
        print("[test] baseline missing: couple.cmb was never auto-loaded — EN_TG "
              "didn't spawn or the forced-CMB routing regressed.")
        sys.exit(2)
    couple_model_id = int(m.group(1))
    print(f"[test] couple.cmb -> model id {couple_model_id}")

    # Anim probe format (Zelda3D_UpdateAnim @ zelda3d_anim.cpp):
    #   [Zelda3D animPlay] model=<n> anim='<name>'
    anim_lines = re.findall(
        rf"\[Zelda3D animPlay\] model={couple_model_id} anim='([^']+)'",
        log,
    )
    print(f"[test] animPlay entries for couple model {couple_model_id}: {anim_lines}")

    if not anim_lines:
        print("[test] baseline missing: the couple model was never driven by "
              "Zelda3D_UpdateAnim. Anim resolver may not have fired for EN_TG.")
        sys.exit(2)

    plays_tg = any(a == "tg_matsu" for a in anim_lines)
    plays_mu = any(a == "mu_matsu" for a in anim_lines)

    if plays_tg:
        print("[test] GREEN: EN_TG's couple model plays tg_matsu.")
        sys.exit(0)
    if plays_mu:
        print("[test] RED: EN_TG's couple model plays mu_matsu (EN_MU wait pose) "
              "instead of tg_matsu (dancing couple animation).")
        sys.exit(1)
    print(f"[test] RED: EN_TG's couple model plays {anim_lines!r} — not tg_matsu.")
    sys.exit(1)


if __name__ == "__main__":
    main()
