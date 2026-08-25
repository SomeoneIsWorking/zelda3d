#!/usr/bin/env python3
"""syokudai_cmb_close_test.py — assert AUTO routes Obj_Syokudai wood-torch variant to
syokudai_ki_model.cmb.

Structural close-test for the divergence recorded in
debug_journal/2026-07-02-zoras-domain-audit.md:

Obj_Syokudai (0x5E) draws ONE OF THREE N64 display lists at draw time, selected by
`(params >> 12)`: 0 = gGoldenTorchDL, 1 = gTimedTorchDL, 2 = gWoodenTorchDL. On OoT3D,
zelda_syokudai.zar contains 5 CMBs — syokudai_gn_model (golden, largest → AUTO's
default), syokudai_model, syokudai_ki_model (wooden), syokudai_isi_model (stone),
torch4_modelT (flame). Without per-params routing, every torch renders as the golden
palace variant regardless of its intended style.

This test warps to Zora's Domain (ent 0x108, dayTime 0x8001), where the sweep shows a
wooden torch instance at params 0x2000 (params >> 12 == 2) and asserts the AUTO log
carries a forced-CMB load line for zelda_syokudai.zar|syokudai_ki → syokudai_ki_model.cmb.
Red on HEAD, green when the per-actor-params forced-CMB routing lands.
"""
import os, re, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")


def main():
    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "restart", "0x108", "0x8001"],
        env={**os.environ, "ZELDA3D_HEADLESS": "1"},
        cwd=REPO, capture_output=True, text=True, timeout=90,
    )
    time.sleep(5)
    subprocess.run(["python3", "tools/zelda3d_repl.py", "cmd", "actorsnear 10000"],
                   cwd=REPO, capture_output=True, text=True, timeout=15)
    time.sleep(1)

    with open(LOG, "r", errors="replace") as f:
        log = f.read()

    wood_load = re.search(
        r"\[Zelda3D\] auto-loaded model \d+ \(/actor/zelda_syokudai\.zar\s*\|\s*syokudai_ki\)"
        r".*cmb 'Model/syokudai_ki_model\.cmb'",
        log,
    )
    default_load = re.search(
        r"\[Zelda3D\] auto-loaded model \d+ \(/actor/zelda_syokudai\.zar\)"
        r".*cmb 'Model/syokudai_gn_model\.cmb'",
        log,
    )

    print(f"[test] Golden torch AUTO default (syokudai_gn_model.cmb): {'FOUND' if default_load else 'missing'}")
    print(f"[test] Wooden torch forced-CMB (syokudai_ki_model.cmb):   {'FOUND' if wood_load else 'MISSING'}")

    if not default_load:
        print("[test] regression: AUTO default for zelda_syokudai.zar is missing — the AUTO path itself broke.")
        sys.exit(2)
    if not wood_load:
        print("[test] RED: wooden torch didn't route to syokudai_ki_model.cmb — per-params forced-CMB routing not landed.")
        sys.exit(1)
    print("[test] GREEN: wooden torch routes correctly.")
    sys.exit(0)


if __name__ == "__main__":
    main()
