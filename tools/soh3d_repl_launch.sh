#!/usr/bin/env bash
# Launch a long-lived headless SoH3D instance with the interactive REPL enabled.
# Poke it with tools/soh3d_repl.py (set tint/scale, spawn, dump frames) without a
# rebuild/restart. Runs until killed; the soh3d_render.sh trap tears down Xvfb+soh.
#
# Run it in the background, e.g.:
#   tools/soh3d_repl_launch.sh &            (or via the agent's background runner)
# Override the warp target with SOH3D_ENTRANCE=<decimal> (default Gerudo Valley,
# which loads OBJECT_KIBAKO2 so `spawn kibako` works).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export SOH3D=1
export SOH3D_WARP=1
export SOH3D_ENTRANCE="${SOH3D_ENTRANCE:-279}"
export SOH3D_REPL="${SOH3D_REPL:-$REPO/scratch/soh3d.ctl}"
export SOH3D_TIMEOUT="${SOH3D_TIMEOUT:-86400}" # ~1 day; killed explicitly instead
# Fresh control channel each launch.
rm -f "$SOH3D_REPL" "$SOH3D_REPL.out"
echo "soh3d_repl_launch: fifo=$SOH3D_REPL entrance=$SOH3D_ENTRANCE"
exec "$REPO/tools/soh3d_render.sh"
