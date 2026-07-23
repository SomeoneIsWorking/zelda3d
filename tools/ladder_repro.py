#!/usr/bin/env python3
"""Deterministic LADDER / WALL-CLIMB repro for Link (Zelda3D) — headless.

Closes the recorded tooling gap (docs/re-frontier.md `player.draw-anchor` gap 3, and #201 c2):
"real ladder-grab climb never engaged headless (approaches slid past)".  Two causes, both measured
here 2026-07-23, neither of them the grab gate itself:

  1. `walkhold`'s stick is CAMERA-relative AND ITS X AXIS IS INVERTED.  The world heading Link takes
     is  `camFwd - atan2(stickX, stickY)`,  where `camFwd = atan2(link.x-eye.x, link.z-eye.z)` from
     `posinfo`.  Calibrated on 5 stick vectors in open ground (Kokiri, flat): (0,60)->camFwd+0,
     (0,-60)->camFwd+180, (60,0)->camFwd-90, (-60,0)->camFwd+90, (42,42)->camFwd-45 — all five fit
     the minus form and none fit the plus form.  Aiming with the plus sign walks Link along the wall
     instead of into it, which is exactly the "slid past" symptom.
  2. Plain `tp` is SWEPT BY COLLISION: it moves world.pos but the bg-check line test from the frame's
     prevPos stops Link at the first wall in between, so a long teleport silently lands him partway
     (measured: tp (-29,975)->(1080,-606) landed at (656,3), 62% along the segment) and can drag him
     through a door trigger or a void floor (that is what warped an earlier session into Mido's House
     and void-out-respawned another in Kakariko).  Use `tpf x z [yawDeg]` (snaps to floor, zeroes
     velocity, forces idle) and HOP in short legs so no leg crosses a wall.

Finding climbables in any scene: REPL `wallscan <csv>` dumps every wall poly with its climb flags —
bit0 (=1) climbable wall/vine, bit3 (=8) real LADDER.  z_player.c sAgeProperties.unk_AC selects the
up-clip as unk_AC[actionVar1 + actionVar2]: a real ladder (actionVar1=0) alternates indices 0/1 =
the age-specific climb_upL/upR, a forced/vine wall (actionVar1=1) alternates 1/2.

Usage:  python3 tools/ladder_repro.py [--target wall|ladder] [--instance 1] [--log] [--poll N]
"""
import argparse
import math
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Kokiri Forest (entrance 0xEE / 238, scene 0x55) climb targets, from `wallscan`:
#   wall   — static polys 508/509, normal (0,0,-1), y -80..115, climbFlags 3 (climbable, NOT a
#            ladder). 30 units north of the spawn, so one short hop reaches it.
#   ladder — static polys 295/296, normal (-0.998,.,0.068), y 60..201, climbFlags 8 (REAL LADDER,
#            Mido's house). Needs hops: the direct line crosses Mido's door trigger.
TARGETS = {
    "wall": dict(entrance=238, hops=[(-29.0, 975.0)], heading=0.0),
    "ladder": dict(entrance=238,
                   hops=[(-120.0, 700.0), (-260.0, 300.0), (-380.0, -150.0),
                         (-470.0, -560.0), (-560.0, -900.0), (-576.0, -1031.0)],
                   heading=94.0),
}


def repl(cmd, fifo):
    env = dict(os.environ, ZELDA3D_REPL=fifo)
    out = subprocess.run([sys.executable, os.path.join(REPO, "tools", "zelda3d_repl.py"), "cmd", cmd],
                         capture_output=True, text=True, env=env, cwd=REPO).stdout.strip()
    return out.splitlines()[-1] if out else ""


def cam_fwd(fifo):
    """World heading (deg, 0=+Z) that stick (0,+60) walks Link along = eye->Link in XZ."""
    s = repl("posinfo", fifo)
    try:
        eye = [float(v) for v in s.split("eye=(")[1].split(")")[0].split(",")]
        lnk = [float(v) for v in s.split("link=(")[1].split(")")[0].split(",")]
    except (IndexError, ValueError):
        return None
    return math.degrees(math.atan2(lnk[0] - eye[0], lnk[2] - eye[2]))


def walk(fifo, world_heading_deg, frames):
    """Hold the stick so Link walks along a WORLD heading (see the X-inversion note above)."""
    cf = cam_fwd(fifo)
    if cf is None:
        return
    rel = math.radians(cf - world_heading_deg)      # world = camFwd - atan2(sx, sy)
    repl(f"walkhold {frames} {int(round(60 * math.sin(rel)))} {int(round(60 * math.cos(rel)))}", fifo)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", choices=sorted(TARGETS), default="wall")
    ap.add_argument("--instance", default="1")
    ap.add_argument("--poll", type=int, default=8)
    ap.add_argument("--log", action="store_true", help="enable the `link` log channel over the climb")
    ap.add_argument("--down", action="store_true", help="climb back down after reaching the top")
    a = ap.parse_args()
    t = TARGETS[a.target]
    suf = f".{a.instance}" if a.instance else ""
    fifo = os.path.join(REPO, "scratch", f"zelda3d{suf}.ctl")

    repl("walkhold 0", fifo)
    repl("freeze 0", fifo)
    repl(f"warp {t['entrance']}", fifo)
    time.sleep(12)
    for hx, hz in t["hops"]:                        # short legs only — `tpf` is collision-swept too
        repl(f"tpf {hx} {hz} {t['heading']}", fifo)
        time.sleep(1.2)
    print("at:", repl("posinfo", fifo))

    if a.log:
        repl("log link 1", fifo)
    walk(fifo, t["heading"], 40 * a.poll)
    grabbed = False
    for i in range(a.poll):
        time.sleep(0.8)
        info = repl("climbinfo", fifo)
        st1 = int(info.split("st1=")[1].split()[0], 16) if "st1=" in info else 0
        climbing = bool(st1 & 0x00200000)   # PLAYER_STATE1_CLIMBING_LADDER
        if climbing and not grabbed:
            # ONCE ON THE WALL the stick stops being camera-relative: Player_Action_8084BF1C reads
            # sControlInput->rel.stick_y/x RAW (+y = up a rung, -y = down, x = side-shuffle). The
            # camera-corrected approach vector usually has a NEGATIVE y, which reads as "climb down"
            # and drops Link straight back off the wall — that was the second half of the "approaches
            # slid past" symptom. Switch to a raw full-up hold the moment the grab lands.
            repl(f"walkhold {40 * a.poll} 0 60", fifo)
            grabbed = True
        if a.down and grabbed and i == a.poll // 2:
            repl(f"walkhold {40 * a.poll} 0 -60", fifo)
        print(f"[{i}]", info)
        print("    ", repl("linkanimstate", fifo))
    if a.log:
        repl("log link 0", fifo)
    repl("walkhold 0", fifo)


if __name__ == "__main__":
    main()
