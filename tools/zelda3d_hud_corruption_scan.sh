#!/usr/bin/env bash
# #21 HUD-corruption repro scanner.
#
# The injected raw-RGBA HUD textures (heart row, rupee counter) garble on ~10% of
# COLD launches and stay garbled the whole session (clears only on restart). This
# loops N cold headless launches, fixes an identical vantage each time (so the HUD
# crops differ only when corrupted), screenshots, then runs the median-baseline
# detector to flag the corrupted launches.
#
# Usage: tools/zelda3d_hud_corruption_scan.sh [N]   (default 12)
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
N="${1:-12}"
OUT="scratch/screenshots/hudscan"
mkdir -p "$OUT"
ENTR="${SCAN_ENTR:-238}"      # card repro: entrance 238 (0xEE Kokiri Forest)
TIME="${SCAN_TIME:-0x6000}"
rm -f "$OUT"/launch_*.png

repl(){ tools/zelda3d_repl.py cmd "$1" 2>/dev/null; }

for i in $(seq 1 "$N"); do
  ii=$(printf "%02d" "$i")
  echo "=== launch $ii/$N ==="
  tools/zelda3d_game.sh stop >/dev/null 2>&1
  # COLD: each launch is a fresh process (the race lives at boot/texture-upload).
  ZELDA3D_HEADLESS=1 ZELDA3D_AUTO=1 ZELDA3D_LINK=1 tools/zelda3d_game.sh start "$ENTR" "$TIME" >/dev/null 2>&1
  # poll until the REPL responds (scene loaded + controllable)
  ready=0
  for t in $(seq 1 40); do
    if repl posinfo | grep -q scene; then ready=1; break; fi
  done
  if [ "$ready" = 0 ]; then echo "launch $ii: never became ready, skipping"; continue; fi
  # clear the Navi intro textbox/cutscene so control + HUD are in the steady state
  for a in 1 2 3 4 5 6; do repl "btnhold 0x8000 2" >/dev/null; repl "btnhold 0 0" >/dev/null; done
  # fix an identical vantage: pin Link + frame mostly-uniform sky behind the HUD so
  # the heart/rupee crops vary ONLY when the HUD texture itself is corrupted.
  repl "tp -66 -79 939" >/dev/null
  repl "cam -66 60 939 -66 900 1200" >/dev/null
  # settle long enough that the lifemeter hearts + rupee counter actually draw/upload (so their
  # first-draw HUDTEXDBG line is captured), not just the button disc.
  for s in $(seq 1 30); do repl posinfo >/dev/null; done
  tools/zelda3d_repl.py shot "hudscan/launch_$ii" >/dev/null 2>&1
  # capture this launch's HUD-texture-cache decision log (#21 diagnostic) before it's overwritten
  grep "HUDTEXDBG" scratch/logs/run.log > "$OUT/dbg_$ii.log" 2>/dev/null
  # also stash the raw posinfo for the record
  repl posinfo
done
tools/zelda3d_game.sh stop >/dev/null 2>&1

echo
echo "=== detector ==="
shopt -s nullglob
python3 tools/zelda3d_hud_detect.py "$OUT"/launch_*.png
