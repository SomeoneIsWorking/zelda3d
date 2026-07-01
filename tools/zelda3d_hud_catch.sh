#!/usr/bin/env bash
# #21 — relaunch cold until the HUD corruption is CAUGHT live, then LEAVE the game
# running (corruption persists the session) so we can A/B-diagnose it (hudtex toggle,
# renderer state). Classifies each launch's HUD crops against a known-clean reference.
#
# Usage: tools/zelda3d_hud_catch.sh [maxLaunches] [cleanRef.png]
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
MAX="${1:-25}"
REF="${2:-scratch/screenshots/hudscan/launch_01.png}"
OUT="scratch/screenshots/hudscan"
mkdir -p "$OUT"
ENTR="${SCAN_ENTR:-238}"; TIME="${SCAN_TIME:-0x6000}"
repl(){ tools/zelda3d_repl.py cmd "$1" 2>/dev/null; }

classify(){ # $1 = shot ; prints MSE-vs-ref
  python3 - "$REF" "$1" <<'PY'
import sys,numpy as np
from PIL import Image
HEARTS=(40,20,560,140); RUPEE=(20,900,360,1006)
def cr(p):
    im=Image.open(p).convert("RGB")
    if im.size!=(1920,1006): im=im.resize((1920,1006))
    return (np.asarray(im.crop(HEARTS),np.float32),np.asarray(im.crop(RUPEE),np.float32))
a=cr(sys.argv[1]); b=cr(sys.argv[2])
mse=((a[0]-b[0])**2).mean()+((a[1]-b[1])**2).mean()
print(f"{mse:.1f}")
PY
}

for i in $(seq 1 "$MAX"); do
  ii=$(printf "%02d" "$i")
  tools/zelda3d_game.sh stop >/dev/null 2>&1
  ZELDA3D_HEADLESS=1 ZELDA3D_AUTO=1 ZELDA3D_LINK=1 tools/zelda3d_game.sh start "$ENTR" "$TIME" >/dev/null 2>&1
  ready=0; for t in $(seq 1 40); do repl posinfo | grep -q scene && { ready=1; break; }; done
  [ "$ready" = 0 ] && { echo "catch $ii: not ready"; continue; }
  for a in 1 2 3 4 5 6; do repl "btnhold 0x8000 2" >/dev/null; repl "btnhold 0 0" >/dev/null; done
  repl "tp -66 -79 939" >/dev/null
  repl "cam -66 60 939 -66 900 1200" >/dev/null
  for s in $(seq 1 8); do repl posinfo >/dev/null; done
  tools/zelda3d_repl.py shot "hudscan/catch_$ii" >/dev/null 2>&1
  mse=$(classify "$OUT/catch_$ii.png")
  echo "catch $ii: MSE=$mse"
  awk -v m="$mse" 'BEGIN{exit !(m>150)}' && {
    echo "CAUGHT CORRUPT at launch $ii (MSE=$mse) — game LEFT RUNNING for diagnosis"
    echo "shot: $OUT/catch_$ii.png"
    exit 0
  }
done
tools/zelda3d_game.sh stop >/dev/null 2>&1
echo "no corruption caught in $MAX launches"
exit 1
