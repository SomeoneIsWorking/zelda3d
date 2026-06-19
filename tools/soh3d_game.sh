#!/usr/bin/env bash
# Single-instance SoH3D game manager. Fixes the recurring failure where hand-rolled launch+kill
# left MULTIPLE soh.elf instances fighting over one REPL FIFO (the agent's `kill` loop matched
# "*soh.elf)" and SILENTLY missed rebuilt binaries that readlink reports as "soh.elf (deleted)").
#
# Subcommands:
#   start [entrance] [time]  kill all, clear FIFO, launch ONE detached instance, wait for ready
#   restart [entrance] [time] build the soh target, then start
#   stop                     kill ALL soh.elf (incl. "(deleted)"), zenity crash dialogs, FIFO
#   status                   list running soh.elf pids; non-zero exit if != 1 instance
#   log [-f]                 tail the run log
# Detaches via setsid+nohup so the instance survives across agent tool calls WITHOUT relying on a
# background-runner (which orphaned instances the agent then lost track of). One pid -> scratch.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOH="$REPO/Shipwright/build-cmake/soh/soh.elf"
LOG="$REPO/scratch/logs/run.log"
FIFO="${SOH3D_REPL:-$REPO/scratch/soh3d.ctl}"
PIDFILE="$REPO/scratch/soh3d.pid"

# List pids of every running soh.elf — matches the path with OR without a trailing " (deleted)"
# (a rebuilt-over binary), which is exactly the case the old kill loop missed.
soh_pids() {
    local p t pid
    for p in /proc/*/exe; do
        t="$(readlink "$p" 2>/dev/null)" || continue
        case "$t" in
            "$SOH"|"$SOH ("*) pid="${p#/proc/}"; echo "${pid%/exe}" ;;
        esac
    done
}

stop() {
    local pids; pids="$(soh_pids)"
    if [ -n "$pids" ]; then echo "$pids" | xargs -r kill -9 2>/dev/null; fi
    pkill -9 zenity 2>/dev/null   # a SoH crash pops a blocking zenity dialog; clear it too
    rm -f "$FIFO" "$FIFO.out" "$PIDFILE" 2>/dev/null
    # wait for them to actually die (up to ~3s)
    for _ in $(seq 1 30); do [ -z "$(soh_pids)" ] && break; sleep 0.1; done
    local left; left="$(soh_pids)"
    [ -n "$left" ] && { echo "WARN: still alive: $left" >&2; return 1; }
    return 0
}

status() {
    local pids n; pids="$(soh_pids)"; n=$(printf '%s\n' "$pids" | grep -c . )
    if [ "$n" -eq 0 ]; then echo "soh.elf: none running"; return 1; fi
    echo "soh.elf running ($n):"; printf '  pid %s\n' $pids
    [ "$n" -gt 1 ] && { echo "WARN: more than one instance!" >&2; return 2; }
    return 0
}

# Headless mode (SOH3D_HEADLESS=1): run on a private Xvfb X server so NO window appears on the
# user's desktop. Required on Wayland — SDL ignores X11 DISPLAY there and would open a visible
# Wayland window — so we force SDL's X11 backend onto the Xvfb display and drop WAYLAND_DISPLAY.
# Rendering still uses the real GPU; screenshots (REPL `shot`) read the in-process framebuffer.
setup_headless() {
    [ "${SOH3D_HEADLESS:-0}" = "1" ] || return 0
    local disp="${SOH3D_HEADLESS_DISPLAY:-:99}"
    if ! DISPLAY="$disp" xdpyinfo >/dev/null 2>&1; then
        echo "headless: starting Xvfb on $disp" >&2
        setsid Xvfb "$disp" -screen 0 1920x1080x24 >"$REPO/scratch/logs/xvfb.log" 2>&1 &
        local up=
        for _ in $(seq 1 20); do DISPLAY="$disp" xdpyinfo >/dev/null 2>&1 && { up=1; break; }; sleep 0.5; done
        [ -n "$up" ] || { echo "headless: Xvfb failed on $disp (see scratch/logs/xvfb.log)" >&2; return 1; }
    fi
    export DISPLAY="$disp" XAUTHORITY=/dev/null SDL_VIDEODRIVER=x11
    # Headless = no audio: route SDL audio to the dummy backend so a background headless
    # run never plays sound on the user's speakers (headed runs keep real audio).
    export SDL_AUDIODRIVER=dummy
    unset WAYLAND_DISPLAY
    echo "headless: on $disp (Xvfb, SDL x11, audio=dummy)" >&2
}

start() {
    local entr="${1:-${SOH3D_ENTRANCE:-238}}" time="${2:-${SOH3D_TIME:-0x6000}}"
    stop || { echo "stop failed; aborting start" >&2; return 1; }
    setup_headless || return 1
    # ROM provisioning: env -> gitignored .env -> any *.3ds / *.z64 dropped in the repo dir.
    . "$REPO/tools/rom_provision.sh"
    soh3d_provision_roms "$REPO" "$(dirname "$SOH")"
    : "${SOH3D_3DS_ROM:?no OoT3D .3ds found — set SOH3D_3DS_ROM, add ./.env, or drop a *.3ds in $REPO}"
    local xauth; xauth="${XAUTHORITY:-$(ls -t /run/user/"$(id -u)"/xauth_* 2>/dev/null | head -1)}"
    mkdir -p "$REPO/scratch/logs"
    echo "starting: entrance=$entr time=$time -> $LOG"
    # setsid+nohup fully detaches: survives this shell returning, no background-runner needed.
    setsid env SOH3D=1 SOH3D_WARP=1 SOH3D_AUTO="${SOH3D_AUTO:-1}" SOH3D_N64ANIM="${SOH3D_N64ANIM:-1}" \
        SOH3D_VULKAN="${SOH3D_VULKAN:-}" SOH3D_VK_VALIDATION="${SOH3D_VK_VALIDATION:-}" \
        SOH3D_RMLUI_OPEN="${SOH3D_RMLUI_OPEN:-}" \
        SOH3D_ENTRANCE="$entr" SOH3D_TIME="$time" SOH3D_REPL="$FIFO" \
        DISPLAY="${DISPLAY:-:0}" XAUTHORITY="$xauth" \
        stdbuf -oL -eL "$SOH" >"$LOG" 2>&1 < /dev/null &
    local pid=$!; echo "$pid" > "$PIDFILE"
    # wait for REPL ready (the .out file appears) — up to ~40s
    for _ in $(seq 1 80); do
        [ -e "$FIFO.out" ] && { echo "ready (pid $(soh_pids))"; return 0; }
        [ -z "$(soh_pids)" ] && { echo "FAILED to boot — see $LOG" >&2; tail -5 "$LOG" >&2; return 1; }
        sleep 0.5
    done
    echo "WARN: booted but REPL not ready after 40s (pid $(soh_pids))" >&2; return 1
}

case "${1:-}" in
    start)   shift; start "$@" ;;
    restart) shift; cmake --build "$REPO/Shipwright/build-cmake" --target soh -j"$(nproc)" || exit 1; start "$@" ;;
    stop)    stop ;;
    status)  status ;;
    log)     shift; tail "${1:--n}" "${2:-40}" "$LOG" 2>/dev/null || tail -40 "$LOG" ;;
    *) echo "usage: $0 {start|restart|stop|status|log} [entrance] [time]"; exit 2 ;;
esac
