#!/usr/bin/env python3
"""enmu_close_test.py — assert EnMu applies per-material CONSTANT-color overrides.

Structured close-test for the visible EnMu divergence: at Market Day (ent 0xB1
dayTime 0x8001) both EnMu haggling-townsperson instances render with SOLID BLACK
clothing where their outfits should be textured/coloured. Same root cause as EnHy
townsfolk (fixed in commit 97145451, TownsfolkBehavior::applyDrawOverrides):
`marketpeople.cmb` ships matConstant = (0, 0, 0, 1) and its material stage 1 =
MODULATE(PREV, CONST) zeroes the fragment. Without a per-material CONSTANT-color
override the clothes render as ink-black voids.

The runtime observable is the `[MATCONST]` log line emitted by
`Zelda3D_GL_SetMatConstOverride` (zelda3d_gl.cpp) when `ZELDA3D_DBG_MATCONST=1`.
This test:
  1. Warps to Market Day with the debug env set so MATCONST calls are logged.
  2. Parses run.log to find the model id auto-loaded for the marketpeople CMB.
  3. Greps MATCONST lines and checks whether any reference that model id.

RED on HEAD: no MATCONST call for the marketpeople model.
GREEN once the EnMu behavior module lands and calls SetMatConstOverride for
marketpeople clothing materials.
"""
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")


def main():
    env = os.environ.copy()
    env["ZELDA3D_HEADLESS"] = "1"
    env["ZELDA3D_DBG_MATCONST"] = "1"
    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "restart", "0xB1", "0x8001"],
        cwd=REPO, env=env, capture_output=True, text=True, timeout=90,
    )
    time.sleep(6)
    # Warm the actor set so EnMu's Draw path is exercised and any matConst overrides fire.
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

    # Parse the auto-loaded model line to find the marketpeople.cmb model id. Format:
    #   [Zelda3D] auto-loaded model <n> (/actor/zelda_mu.zar): cmb 'Model/marketpeople.cmb' ...
    m = re.search(
        r"\[Zelda3D\] auto-loaded model (\d+) \(/actor/zelda_mu\.zar\)[^\n]*"
        r"cmb 'Model/marketpeople\.cmb'",
        log,
    )
    if m is None:
        print("[test] baseline missing: marketpeople.cmb was never auto-loaded — "
              "either EnMu didn't spawn or Zelda3D loader is silent. Warp/scene may have regressed.")
        sys.exit(2)
    market_model_id = int(m.group(1))
    print(f"[test] marketpeople.cmb -> model id {market_model_id}")

    # MATCONST log format (Zelda3D_GL_SetMatConstOverride @ zelda3d_gl.cpp):
    #   [MATCONST] model=<id> mat=<i> constIdx=<c> rgba=(r,g,b,a)
    matconst_lines = re.findall(
        rf"\[MATCONST\] model={market_model_id}\b[^\n]*", log,
    )
    print(f"[test] MATCONST calls for marketpeople model {market_model_id}: "
          f"{len(matconst_lines)}")

    if len(matconst_lines) > 0:
        print("[test] GREEN: EnMu applies per-material CONSTANT-color overrides.")
        # Show the first few for confirmation
        for line in matconst_lines[:4]:
            print(f"    {line}")
        sys.exit(0)

    print("[test] RED: no CONSTANT-color override applied to marketpeople model — "
          "EnMu clothing renders black.")
    sys.exit(1)


if __name__ == "__main__":
    main()
