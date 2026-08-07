#!/usr/bin/env bash
# Headless Zelda3D render helper with GUARANTEED teardown.
#
# Why this exists: the game exits 139 (segfault) on its normal teardown *after* the
# framedump is written. When run via `xvfb-run` directly, that abnormal exit leaks
# the backing `Xvfb` server (and sometimes the game process itself), leaving processes
# behind run after run. This wrapper starts its own private Xvfb and a `trap`
# kills both the Xvfb and this run's game process on EXIT/INT/TERM, so nothing is left behind
# even when it crashes or the run is killed.
#
# Usage:
#   SOH3D=1 ZELDA3D_WARP=1 [other ZELDA3D_* / SOH_FRAMEDUMP* env] \
#     tools/zelda3d_render.sh
# Honors ZELDA3D_TIMEOUT (default 420) and ZELDA3D_DISPLAY (default 99). Run ONE at a
# time (the fixed display number self-enforces this). All ZELDA3D_*/SOH_* env vars
# set by the caller are inherited by the game process.
#
# ONE program binary now runs both games (`zelda3d oot` / `zelda3d mm`); this wrapper always runs
# the OoT side. Cleanup discriminates by argv (tools/zelda3d_proc.sh) so it can never kill a
# concurrently-running MM instance or a parallel agent's OoT instance -- name/path-only matching
# (the old `pkill -f soh.elf`) is no longer safe once both games share one exe.
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/tools/zelda3d_proc.sh"
SOH_DIR="$REPO/Shipwright/build-cmake/soh"
ZELDA3D_BIN="${ZELDA3D_SOH:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
DISP="${ZELDA3D_DISPLAY:-99}"
TIMEOUT="${ZELDA3D_TIMEOUT:-420}"

# Force the X11 (Xvfb) backend. Without this, if the caller's environment has
# WAYLAND_DISPLAY set (e.g. a KDE/GNOME Wayland session), SDL2 prefers the real
# Wayland compositor and pops a VISIBLE window on the user's desktop instead of
# rendering into our private Xvfb — i.e. "headless" stops being headless. Unset it.
unset WAYLAND_DISPLAY
export SDL_VIDEODRIVER=x11

cleanup() {
    local pid
    for pid in $(zelda3d_pids "$ZELDA3D_BIN" oot); do kill -9 "$pid" 2>/dev/null; done
    [ -n "${XVFB_PID:-}" ] && kill -9 "$XVFB_PID" 2>/dev/null
    pkill -9 -f "Xvfb :${DISP}\b" 2>/dev/null
}
trap cleanup EXIT INT TERM

# Tear down any stragglers from a previous (crashed) run before starting.
cleanup
pkill -9 -f "Xvfb :${DISP}\b" 2>/dev/null
rm -f "/tmp/.X${DISP}-lock" 2>/dev/null

Xvfb ":${DISP}" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
sleep 1

cd "$SOH_DIR" || exit 1
DISPLAY=":${DISP}" timeout "$TIMEOUT" "$ZELDA3D_BIN" oot
rc=$?
# 124 = timeout reached, 139 = teardown segfault after framedump (both ok).
echo "zelda3d_render: zelda3d oot rc=$rc (124=timeout 139=teardown-segfault, both expected)"
exit 0
