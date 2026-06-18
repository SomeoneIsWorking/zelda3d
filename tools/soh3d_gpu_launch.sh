#!/usr/bin/env bash
# Launch SoH3D on the REAL GPU (the user's desktop X/XWayland display), with the REPL
# enabled, for radeonsi-vs-llvmpipe debugging. Headless Xvfb uses llvmpipe (software)
# and HIDES driver-specific bugs (e.g. the ACO skinning miscompile); this runs on the
# actual GPU so those reproduce. Drive it with tools/soh3d_repl.py (dump/posinfo/tp/...).
#
# Use via the agent's background runner (run_in_background) so it survives across calls:
#   tools/soh3d_gpu_launch.sh [entrance]
# Auto-detects DISPLAY/XAUTHORITY (KDE Wayland XWayland) and the ROM (env / .env /
# ./oot3d.3ds). Kills stale soh.elf first; a trap tears everything down on exit so no
# instance is left on the user's desktop. Log -> scratch/logs/gpu.log (line-buffered).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOH="$REPO/Shipwright/build-cmake/soh"
ENTR="${1:-${SOH3D_ENTRANCE:-219}}" # default Kakariko Village front gate

cleanup() { pkill -9 -f "$SOH/soh.elf" 2>/dev/null; pkill -9 -f "soh.elf" 2>/dev/null; }
trap cleanup EXIT INT TERM
cleanup; sleep 1 # clear any stale instance before we start

# ROM provisioning: env -> .env -> any *.3ds / *.z64 dropped in the repo dir, same as run.sh.
. "$REPO/tools/rom_provision.sh"
soh3d_provision_roms "$REPO" "$SOH"
: "${SOH3D_3DS_ROM:?no OoT3D .3ds found — set SOH3D_3DS_ROM, add ./.env, or drop a *.3ds in $REPO}"

# Real-GPU display: prefer an already-set DISPLAY, else the KDE XWayland :0 + its xauth.
if [ -z "${DISPLAY:-}" ]; then export DISPLAY=:0; fi
if [ -z "${XAUTHORITY:-}" ]; then
    XAUTHORITY="$(ls -t /run/user/"$(id -u)"/xauth_* 2>/dev/null | head -1)"
    [ -n "$XAUTHORITY" ] && export XAUTHORITY
fi

mkdir -p "$REPO/scratch/logs"
export SOH3D=1
export SOH3D_WARP=1
export SOH3D_ENTRANCE="$ENTR"
export SOH3D_REPL="${SOH3D_REPL:-$REPO/scratch/soh3d.ctl}"
# Pin time-of-day to noon so scenes load in DAYLIGHT (override with SOH3D_TIME, e.g.
# 0x4000 dawn / 0xC000 dusk / 0 midnight, or -1 to release the game clock). REPL: `time`.
export SOH3D_TIME="${SOH3D_TIME:-0x8000}"
rm -f "$SOH3D_REPL" "$SOH3D_REPL.out"
echo "soh3d_gpu_launch: DISPLAY=$DISPLAY XAUTHORITY=${XAUTHORITY:-<none>} entrance=$ENTR time=$SOH3D_TIME fifo=$SOH3D_REPL"
cd "$SOH"
# stdbuf line-buffers so the log (and any crash dump) survives even on a hard exit.
exec stdbuf -oL -eL ./soh.elf
