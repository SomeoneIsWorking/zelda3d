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
cd "$(dirname "$0")/../.."
A=$1; D=${2:-200}; L=${3:-a$1}; E=${4:-0}; N=${5:-6}
best=0; bestn=-1
for n in $(seq 0 $((N-1))); do
  tools/zelda3d_repl.py cmd "ahide 0" >/dev/null 2>&1
  tools/zelda3d_repl.py cmd "freeze 0" >/dev/null 2>&1
  S=$(tools/zelda3d_repl.py cmd "asel $A $n" 2>&1 | tail -1)
  case "$S" in *"no match"*) break;; esac
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
  echo "  $L: DRAWS ($best px, instance $bestn)"
else
  echo "  $L: no contribution found across instances -- INCONCLUSIVE unless autostate shows state=2 AND"
  echo "        the prop is a closed volume framed head-on (flat props need elev/orbit)"
fi
