#!/usr/bin/env bash
# Launch a long-lived headless Zelda3D instance with the interactive REPL enabled.
# Poke it with tools/zelda3d_repl.py (set tint/scale, spawn, dump frames) without a
# rebuild/restart. Runs until killed; the zelda3d_render.sh trap tears down Xvfb+the game process.
#
# Run it in the background, e.g.:
#   tools/zelda3d_repl_launch.sh &            (or via the agent's background runner)
# Override the warp target with ZELDA3D_ENTRANCE=<decimal> (default Gerudo Valley,
# which loads OBJECT_KIBAKO2 so `spawn kibako` works).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export SOH3D=1
export ZELDA3D_WARP=1
export ZELDA3D_ENTRANCE="${ZELDA3D_ENTRANCE:-279}"
export ZELDA3D_REPL="${ZELDA3D_REPL:-$REPO/scratch/zelda3d.ctl}"
export ZELDA3D_TIMEOUT="${ZELDA3D_TIMEOUT:-86400}" # ~1 day; killed explicitly instead
# Fresh control channel each launch.
rm -f "$ZELDA3D_REPL" "$ZELDA3D_REPL.out"
echo "zelda3d_repl_launch: fifo=$ZELDA3D_REPL entrance=$ZELDA3D_ENTRANCE"
exec "$REPO/tools/zelda3d_render.sh"
