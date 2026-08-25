#!/usr/bin/env python3
"""endog_close_test.py — assert EnDog applies its per-material CONSTANT-color override.

Structured close-test for the visible EnDog divergence: at Market Day (ent 0xB1
dayTime 0x8001) the dogs render as SOLID BLACK silhouettes. Same root cause as
EnMu / EnHy: the CMB material's TEV stage 1 = MODULATE(PREV, CONST) with the
CMB-file default matConstant = (0, 0, 0, 1) zeroes the fragment. Without a
runtime CONSTANT-color override the dog is a pure black cutout.

The runtime observable is the `[MATCONST]` log line emitted by
`Zelda3D_GL_SetMatConstOverride` (zelda3d_gl.cpp) when `ZELDA3D_DBG_MATCONST=1`.
This test warps to Market Day, parses run.log to find the auto-loaded model id
for `zelda_dog.zar`, and greps MATCONST lines referencing that model id.

RED on HEAD: no MATCONST call for the dog model.
GREEN once the EnDog behavior module lands.
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

    m = re.search(r"\[Zelda3D\] auto-loaded model (\d+) \(/actor/zelda_dog\.zar\)", log)
    if m is None:
        print("[test] baseline missing: zelda_dog.zar was never auto-loaded — EnDog "
              "didn't spawn or Zelda3D loader is silent. Warp/scene may have regressed.")
        sys.exit(2)
    dog_model_id = int(m.group(1))
    print(f"[test] zelda_dog.zar -> model id {dog_model_id}")

    matconst_lines = re.findall(
        rf"\[MATCONST\] model={dog_model_id}\b[^\n]*", log,
    )
    print(f"[test] MATCONST calls for dog model {dog_model_id}: {len(matconst_lines)}")

    if len(matconst_lines) > 0:
        print("[test] GREEN: EnDog applies a per-material CONSTANT-color override.")
        for line in matconst_lines[:2]:
            print(f"    {line}")
        sys.exit(0)

    print("[test] RED: no CONSTANT-color override applied to dog model — "
          "EnDog renders as a solid black silhouette.")
    sys.exit(1)


if __name__ == "__main__":
    main()
