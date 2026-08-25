#!/usr/bin/env python3
"""navi_close_test.py — assert Navi emits a NON-sky billboard through the renderer.

Structured close-test for #140 (Navi still renders as the N64 fairy sprite).

The observable is the pass-owned runtime probe in `Zelda3D_Sg_DrawModel`
(`zelda3d_sdl3gpu_pass.cpp`, reached through the C ABI adapter):
each frame it logs one line per DrawModel of the sun/Navi billboard model id (2002 in
a typical run) with `sky=<0|1>`. Sun and moon emit with `sky=1`; the OoT3D Navi port
emits at Navi's world.pos with `sky=0`. So the presence of a `sky=0` DrawModel line
for modelId=2002 is proof the OoT3D billboard reached the renderer — which is what
"Navi renders as OoT3D" means at the render-pipeline layer.

RED on HEAD (and on every state where Navi still falls through to N64 EnElf_Draw):
run.log has `sgDraw ... modelId=2002 sky=1 ...` lines (sun/moon) but NO `sky=0` line
for modelId=2002 → Navi is not going through the OoT3D billboard path.

GREEN once the port lands and the emit is visible in the frame: at least one
`sgDraw ... modelId=2002 sky=0 ...` line appears. If the emit reaches DrawModel but
the sprite is invisible on screen, further checks (visual A/B) close the remaining
gap, but this test at least anchors "the render path is exercised for Navi".

Warps to Lon Lon Ranch (ent 0x157 dayTime 0x8001) where the current save spawns Navi
near Link (d ~= 39 world units), so the actor runs its Draw and any Zelda3D emit path
is exercised.
"""
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(REPO, "scratch/logs/run.log")


def repl(cmd, timeout=15):
    return subprocess.run(
        ["python3", "tools/zelda3d_repl.py", "cmd", cmd],
        cwd=REPO, capture_output=True, text=True, timeout=timeout,
    ).stdout


def main():
    subprocess.run(
        [sys.executable, "tools/zelda3d_game.py", "restart", "0x157", "0x8001"],
        env={**os.environ, "ZELDA3D_HEADLESS": "1"},
        cwd=REPO, capture_output=True, text=True, timeout=90,
    )
    time.sleep(6)
    # Give the game a few actor-draw passes so Navi's Draw path runs.
    repl("actorsnear 4000", timeout=15)
    time.sleep(1)

    if not os.path.exists(LOG):
        print(f"[test] run.log missing at {LOG}")
        sys.exit(2)
    with open(LOG, "r", errors="replace") as f:
        log = f.read()

    # Runtime probe format (RecordSubmissionProbe in zelda3d_sdl3gpu_pass.cpp):
    #   [Zelda3D sgDraw #<n>] modelId=2002 sky=<0|1> lit=<0|1> ...
    hits_sun = re.findall(r"\[Zelda3D sgDraw #\d+\] modelId=2002 sky=1\b", log)
    hits_navi = re.findall(r"\[Zelda3D sgDraw #\d+\] modelId=2002 sky=0\b", log)

    print(f"[test] modelId=2002 sky=1 (sun/moon) DrawModel hits: {len(hits_sun)}")
    print(f"[test] modelId=2002 sky=0 (Navi port) DrawModel hits: {len(hits_navi)}")

    if len(hits_sun) == 0:
        # If not even sun renders, the runtime probe isn't wired — this is a baseline
        # regression, not a Navi-specific gap. Treat as an inconclusive result.
        print("[test] baseline missing: even sun/moon (sky=1) never reach DrawModel — "
              "the render-side probe is broken or fine_sun.ctxb never resolved.")
        sys.exit(2)

    if len(hits_navi) > 0:
        print("[test] GREEN: Navi (or another non-sky user of modelId=2002) reached DrawModel.")
        sys.exit(0)

    print("[test] RED: no non-sky DrawModel for modelId=2002 — Navi is not going through "
          "the OoT3D billboard path yet.")
    sys.exit(1)


if __name__ == "__main__":
    main()
