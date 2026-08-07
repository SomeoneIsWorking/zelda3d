#!/usr/bin/env bash
# Launch Zelda3D on the REAL GPU (the user's desktop X/XWayland display), with the REPL
# enabled, for radeonsi-vs-llvmpipe debugging. Headless Xvfb uses llvmpipe (software)
# and HIDES driver-specific bugs (e.g. the ACO skinning miscompile); this runs on the
# actual GPU so those reproduce. Drive it with tools/zelda3d_repl.py (dump/posinfo/tp/...).
#
# Use via the agent's background runner (run_in_background) so it survives across calls:
#   tools/zelda3d_gpu_launch.sh [entrance]
# Auto-detects DISPLAY/XAUTHORITY (KDE Wayland XWayland) and the ROM (env / .env /
# ./oot3d.3ds). Kills a stale OoT instance first; a trap tears everything down on exit so no
# instance is left on the user's desktop. Log -> scratch/logs/gpu.log (line-buffered).
#
# ONE program binary now runs both games (`zelda3d oot` / `zelda3d mm`); this script always runs
# the OoT side. Cleanup discriminates by argv (tools/zelda3d_proc.sh) so it can never kill a
# concurrently-running MM instance or a parallel agent's OoT instance.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/tools/zelda3d_proc.sh"
SOH="$REPO/Shipwright/build-cmake/soh"
ZELDA3D_BIN="${ZELDA3D_SOH:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
ENTR="${1:-${ZELDA3D_ENTRANCE:-219}}" # default Kakariko Village front gate

cleanup() { local pid; for pid in $(zelda3d_pids "$ZELDA3D_BIN" oot); do kill -9 "$pid" 2>/dev/null; done; }
trap cleanup EXIT INT TERM
cleanup; sleep 1 # clear any stale instance before we start

# ROM provisioning: env -> .env -> any *.3ds / *.z64 dropped in the repo dir, same as run.sh.
. "$REPO/tools/rom_provision.sh"
zelda3d_provision_roms "$REPO" "$SOH"
: "${ZELDA3D_OOT3D_ROM:?no OoT3D .3ds found — set ZELDA3D_OOT3D_ROM, add ./.env, or drop a *.3ds in $REPO}"

# Real-GPU display: prefer an already-set DISPLAY, else the KDE XWayland :0 + its xauth.
if [ -z "${DISPLAY:-}" ]; then export DISPLAY=:0; fi
if [ -z "${XAUTHORITY:-}" ]; then
    XAUTHORITY="$(ls -t /run/user/"$(id -u)"/xauth_* 2>/dev/null | head -1)"
    [ -n "$XAUTHORITY" ] && export XAUTHORITY
fi

mkdir -p "$REPO/scratch/logs"
export SOH3D=1
export ZELDA3D_WARP=1
export ZELDA3D_ENTRANCE="$ENTR"
export ZELDA3D_REPL="${ZELDA3D_REPL:-$REPO/scratch/zelda3d.ctl}"
# Pin time-of-day to noon so scenes load in DAYLIGHT (override with ZELDA3D_TIME, e.g.
# 0x4000 dawn / 0xC000 dusk / 0 midnight, or -1 to release the game clock). REPL: `time`.
export ZELDA3D_TIME="${ZELDA3D_TIME:-0x8000}"
rm -f "$ZELDA3D_REPL" "$ZELDA3D_REPL.out"
echo "zelda3d_gpu_launch: DISPLAY=$DISPLAY XAUTHORITY=${XAUTHORITY:-<none>} entrance=$ENTR time=$ZELDA3D_TIME fifo=$ZELDA3D_REPL"
cd "$SOH"
# stdbuf line-buffers so the log (and any crash dump) survives even on a hard exit.
exec stdbuf -oL -eL "$ZELDA3D_BIN" oot
