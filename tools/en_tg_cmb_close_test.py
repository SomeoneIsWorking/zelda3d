#!/usr/bin/env python3
"""en_tg_cmb_close_test.py — assert AUTO resolves EN_TG's object_mu to couple.cmb.

Structured close-test for the divergence recorded in
debug_journal/2026-07-02-market-day-actor-audit.md #1:

EN_TG (0x1AC dancing couple) shares zelda_mu.zar with EN_MU (0x1AD haggling townspeople);
the ZAR contains two CMBs (couple.cmb, marketpeople.cmb). The AUTO picker's
"most-vertices" heuristic picks marketpeople.cmb — the correct match for EN_MU but
NOT for EN_TG. Currently dormant because skinned->skip drops both to N64 fallback,
but any port of EN_TG will inherit the wrong CMB.

This test warps to a scene where BOTH actors are present (Market Day, ent 0xB1
dayTime 0x8001) and asserts the AUTO log carries a load line specifically for
zelda_mu.zar|couple → couple.cmb (the forced-CMB routing the fix must install for
EN_TG). Red on HEAD, green when the per-actor CMB override lands.
"""
import os, re, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")


def main():
    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "restart", "0xB1", "0x8001"],
        env={**os.environ, "ZELDA3D_HEADLESS": "1"},
        cwd=REPO, capture_output=True, text=True, timeout=90,
    )
    time.sleep(5)
    # Warm the actor set so EN_TG's draw path is exercised; a wide actorsnear tickles
    # every visible actor's Draw and forces the AUTO resolve for its object.
    subprocess.run(["python3", "tools/zelda3d_repl.py", "cmd", "actorsnear 10000"],
                   cwd=REPO, capture_output=True, text=True, timeout=15)
    time.sleep(1)

    with open(LOG, "r", errors="replace") as f:
        log = f.read()

    entg_load = re.search(
        # forced-CMB loader logs "<zar> | <cmb>" (spaces around the '|' — distinct from
        # the default AUTO log which has no selector), followed by the resolved cmb name.
        r"\[Zelda3D\] auto-loaded model \d+ \(/actor/zelda_mu\.zar\s*\|\s*couple\)"
        r".*cmb 'Model/couple\.cmb'",
        log,
    )
    enmu_load = re.search(
        r"\[Zelda3D\] auto-loaded model \d+ \(/actor/zelda_mu\.zar\).*"
        r"cmb 'Model/marketpeople\.cmb'",
        log,
    )

    print(f"[test] EN_MU  auto-load (marketpeople.cmb): {'FOUND' if enmu_load else 'missing'}")
    print(f"[test] EN_TG  auto-load (couple.cmb via forced-CMB routing): "
          f"{'FOUND' if entg_load else 'MISSING'}")

    if not enmu_load:
        print("[test] regression: baseline EN_MU auto-load line absent — the AUTO path itself broke.")
        sys.exit(2)
    if not entg_load:
        print("[test] RED: EN_TG did not route to couple.cmb — per-actor forced-CMB override not landed.")
        sys.exit(1)
    print("[test] GREEN: EN_TG routes to couple.cmb.")
    sys.exit(0)


if __name__ == "__main__":
    main()
