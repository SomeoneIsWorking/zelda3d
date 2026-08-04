#!/usr/bin/env bash
# Does a routed actor's replacement CMB actually DRAW?
#   usage: ahide_check.sh <actorIdHex> <camDist> <label> [elevDeg] [maxInstances]
#
# Sweeps INSTANCES, because hiding one actor of a multi-instance prop routinely gives 0 px when that
# instance is occluded or off-screen -- measured: Bg_Mizu_Movebg has 16 instances and only instance 2
# contributed (47032 px), the other four tried read 0. A single-instance test wrongly condemns a correct
# routing, which is exactly how a working Deku Tree web routing got reverted twice.
#
# ALSO: a ZERO overall is only evidence of breakage when `autostate` shows the slot at state=2. If the
# slot never resolved, the N64 draw is still in control and the zero says nothing about the CMB.
# And for a FLAT single-sided prop, pass an elevation or orbit instead -- a side profile sees a
# zero-thickness plane edge-on.
cd "$(dirname "$0")/.." || exit 2
# REFUSE rather than report a negative if the corpus is not reachable. This script lived at ../..
# (the repo's PARENT) until 2026-08-04, so every zelda3d_repl.py call failed, every capture was
# missing, and it printed "no contribution found" for EVERY input -- a diagnostic that could only
# ever produce one answer. A missing REPL is now a hard non-zero exit, not an INCONCLUSIVE.
[ -x tools/zelda3d_repl.py ] || { echo "ahide_check: tools/zelda3d_repl.py not found from $(pwd) -- SEARCHED NOTHING" >&2; exit 2; }
if ! tools/zelda3d_repl.py cmd "autostate" >/dev/null 2>&1; then
  echo "ahide_check: the game REPL is not answering -- SEARCHED NOTHING (start it with tools/zelda3d_game.sh)" >&2
  exit 2
fi
A=$1; D=${2:-200}; L=${3:-a$1}; E=${4:-0}; N=${5:-6}
best=0; bestn=-1
tried=0
for n in $(seq 0 $((N-1))); do
  tools/zelda3d_repl.py cmd "ahide 0" >/dev/null 2>&1
  tools/zelda3d_repl.py cmd "freeze 0" >/dev/null 2>&1
  S=$(tools/zelda3d_repl.py cmd "asel $A $n" 2>&1 | tail -1)
  case "$S" in *"no match"*) break;; esac
  tried=$((tried+1))
  tools/zelda3d_repl.py cmd "acam $D 0 $E" >/dev/null 2>&1
  tools/zelda3d_repl.py cmd "settle 12" >/dev/null 2>&1; sleep 1
  tools/zelda3d_repl.py shot "ac_${L}_on" >/dev/null 2>&1; sleep 1
  tools/zelda3d_repl.py cmd "ahide 1" >/dev/null 2>&1
  tools/zelda3d_repl.py cmd "settle 5" >/dev/null 2>&1; sleep 1
  tools/zelda3d_repl.py shot "ac_${L}_off" >/dev/null 2>&1; sleep 1
  V=$(python3 - "$L" <<'PY'
import sys,numpy as np
from PIL import Image
L=sys.argv[1]
try:
    a=np.asarray(Image.open(f"scratch/screenshots/ac_{L}_on.png").convert("RGB")).astype(int)
    b=np.asarray(Image.open(f"scratch/screenshots/ac_{L}_off.png").convert("RGB")).astype(int)
    print(int((np.abs(a-b).sum(axis=2)>20).sum()))
except Exception: print(0)
PY
)
  [ "$V" -gt "$best" ] && { best=$V; bestn=$n; }
  [ "$best" -gt 200 ] && break
done
tools/zelda3d_repl.py cmd "ahide 0" >/dev/null 2>&1
if [ "$best" -gt 200 ]; then
  echo "  $L: DRAWS ($best px, instance $bestn of $tried live instances)"
elif [ "$tried" -eq 0 ]; then
  # The denominator IS the finding: actor $A is not live here, so this says nothing about the routing.
  echo "  $L: NO LIVE INSTANCE of actor $A in this scene -- 0 instances examined, NOTHING TESTED."
  echo "        Warp somewhere the actor spawns; do not read this as evidence either way."
  exit 3
else
  echo "  $L: no contribution found across $tried live instance(s), best=$best px (threshold 200)"
  echo "        INCONCLUSIVE unless autostate shows state=2 AND the prop is a closed volume framed"
  echo "        head-on (flat props need elev/orbit). Blind spots: occlusion, off-screen, edge-on planes."
fi
