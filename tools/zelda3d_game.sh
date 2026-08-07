#!/usr/bin/env bash
# Single-instance Zelda3D (OoT side) game manager. Fixes the recurring failure where hand-rolled
# launch+kill left MULTIPLE instances fighting over one REPL FIFO (the agent's `kill` loop matched
# a name pattern and SILENTLY missed rebuilt binaries that readlink reports as "... (deleted)").
#
# ONE program binary now runs both games: `zelda3d oot ...` / `zelda3d mm ...` (see
# tools/mm_game.sh for the MM side). Because both games are the SAME executable, exe-path alone no
# longer identifies an instance -- see tools/zelda3d_proc.sh, which this sources.
#
# Subcommands:
#   start [entrance] [time]  kill all, clear FIFO, launch ONE detached instance, wait for ready
#   restart [entrance] [time] build the zelda3d_app target, then start
#   stop                     kill ALL "zelda3d oot" instances (incl. "(deleted)"), zenity crash
#                             dialogs, FIFO -- never an MM instance, even one on the same exe
#   status                   list running "zelda3d oot" pids; non-zero exit if != 1 instance
#   log [-f]                 tail the run log
# Detaches via setsid+nohup so the instance survives across agent tool calls WITHOUT relying on a
# background-runner (which orphaned instances the agent then lost track of). One pid -> scratch.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/tools/zelda3d_proc.sh"
# Binary path: the one zelda3d launcher, run as `zelda3d oot`. Override with ZELDA3D_SOH to run a
# launcher built into a different dir (e.g. a parallel agent's dedicated build dir). SOH_DIR derives
# the OoT core's own asset directory as the launcher's build-tree sibling ".../<build>/soh" -- the
# launcher resolves that same sibling internally, so ROM provisioning (which the launcher does not
# do) targets exactly where the core will look.
SOH="${ZELDA3D_SOH:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
SOH_DIR="$(dirname "$(dirname "$SOH")")/soh"
# Parallel instances: ZELDA3D_INSTANCE=<N> (a positive integer) runs a SECOND (3rd, ...) game
# concurrently with the default one, on its own Xvfb display (:9(9-N)), control FIFO, pidfile and
# log, so two agents can drive/screenshot the game at once. Empty (default) = the original single
# "main" instance with byte-identical paths/behaviour. All instances share the one built binary
# (assets resolve next to it) — so a default-instance `stop` (kill every `zelda3d oot`) would also reap a
# parallel one; stop() therefore SPARES any pid registered in a non-default pidfile (zelda3d.<N>.pid).
INST="${ZELDA3D_INSTANCE:-}"
if [ -n "$INST" ]; then SUF=".$INST"; DEFDISP=":$((99 - INST))"; else SUF=""; DEFDISP=":99"; fi
LOG="$REPO/scratch/logs/run$SUF.log"
FIFO="${ZELDA3D_REPL:-$REPO/scratch/zelda3d$SUF.ctl}"
PIDFILE="$REPO/scratch/zelda3d$SUF.pid"

# List pids of every running "zelda3d oot" instance (see tools/zelda3d_proc.sh). Matches the exe
# path with OR without a trailing " (deleted)" (a rebuilt-over binary) AND requires argv[1]=="oot",
# since the SAME binary also runs MM -- exe-path alone would match a concurrently-running MM
# instance too, and this manager must never touch that.
soh_pids() { zelda3d_pids "$SOH" oot; }

# Pids registered by parallel (non-default) instances — the default `stop` must NOT reap these.
# (zelda3d.<N>.pid; the default pidfile zelda3d.pid has no digit after the dot, so it's excluded.)
parallel_pids() { cat "$REPO"/scratch/zelda3d.[0-9]*.pid 2>/dev/null; }

stop() {
    local pids pid spare alive
    pids="$(soh_pids)"
    if [ -n "$INST" ]; then
        # Instance stop: kill ONLY this instance's pid (from its pidfile) — leave others alone.
        pid="$(cat "$PIDFILE" 2>/dev/null)"
        [ -n "$pid" ] && kill -9 "$pid" 2>/dev/null
    else
        # Default stop: kill every `zelda3d oot` EXCEPT pids a parallel instance registered. The game-id
        # match is what keeps a concurrently-running `zelda3d mm` (same binary) out of this loop.
        spare=" $(parallel_pids | tr '\n' ' ') "
        for pid in $pids; do
            case "$spare" in *" $pid "*) continue ;; esac
            kill -9 "$pid" 2>/dev/null
        done
        pkill -9 zenity 2>/dev/null   # a SoH crash pops a blocking zenity dialog; clear it too
    fi
    rm -f "$FIFO" "$FIFO.out" "$PIDFILE" 2>/dev/null
    # wait for the targeted instance(s) to actually die (up to ~3s)
    for _ in $(seq 1 30); do
        if [ -n "$INST" ]; then
            kill -0 "$pid" 2>/dev/null || break
        else
            spare=" $(parallel_pids | tr '\n' ' ') "; alive=
            for pid in $(soh_pids); do case "$spare" in *" $pid "*) continue ;; esac; alive=1; done
            [ -z "$alive" ] && break
        fi
        sleep 0.1
    done
    return 0
}

status() {
    local pids n; pids="$(soh_pids)"; n=$(printf '%s\n' "$pids" | grep -c . )
    if [ "$n" -eq 0 ]; then echo "zelda3d oot: none running (searched exe=$SOH[ (deleted)] argv[1]=oot)"; return 1; fi
    echo "zelda3d oot running ($n):"; printf '  pid %s\n' $pids
    [ "$n" -gt 1 ] && { echo "WARN: more than one instance!" >&2; return 2; }
    return 0
}

# Headless mode (the DEFAULT): run on a private Xvfb X server so NO window appears on the
# user's desktop. Required on Wayland — SDL ignores X11 DISPLAY there and would open a visible
# Wayland window — so we force SDL's X11 backend onto the Xvfb display and drop WAYLAND_DISPLAY.
# Rendering still uses the real GPU; screenshots (REPL `shot`) read the in-process framebuffer.
# This manager is agent tooling, and agents must never open a window on the user's desktop
# (project hard rule) — so headless is opt-OUT here: set ZELDA3D_HEADLESS=0 for a headed run
# (the user's own headed path is ./run.sh, which is unaffected).
setup_headless() {
    # The OoT/MM launcher is the DEFAULT entry point for a person, but this manager exists to boot
    # straight into gameplay for harnesses, sweeps and screenshots -- a launcher sitting there
    # waiting for a keypress would hang every one of them. So agent tooling opts out by default.
    # Set ZELDA3D_LAUNCHER=1 explicitly to bring it up under this manager (that is how it is
    # captured headlessly). ./run.sh, the user's own path, is untouched and gets the launcher.
    export ZELDA3D_LAUNCHER="${ZELDA3D_LAUNCHER:-0}"
    [ "${ZELDA3D_HEADLESS:-1}" = "1" ] || return 0
    local disp="${ZELDA3D_HEADLESS_DISPLAY:-$DEFDISP}"
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
    local entr="${1:-${ZELDA3D_ENTRANCE:-238}}" time="${2:-${ZELDA3D_TIME:-0x6000}}"
    stop || { echo "stop failed; aborting start" >&2; return 1; }
    setup_headless || return 1
    # ROM provisioning: env -> gitignored .env -> any *.3ds / *.z64 dropped in the repo dir.
    . "$REPO/tools/rom_provision.sh"
    zelda3d_provision_roms "$REPO" "$SOH_DIR"
    : "${ZELDA3D_OOT3D_ROM:?no OoT3D .3ds found — set ZELDA3D_OOT3D_ROM, add ./.env, or drop a *.3ds in $REPO}"
    local xauth; xauth="${XAUTHORITY:-$(ls -t /run/user/"$(id -u)"/xauth_* 2>/dev/null | head -1)}"
    mkdir -p "$REPO/scratch/logs"
    echo "starting: entrance=$entr time=$time -> $LOG"
    # setsid+nohup fully detaches: survives this shell returning, no background-runner needed.
    # ZELDA3D_WARP defaults ON (auto-warp to an in-game entrance, skipping the boot title demo).
    # Set ZELDA3D_WARP= (empty) to boot the real Opening->title demo instead (needed to repro the
    # title/cutscene-camera bugs #4/#14, which only exist on that flow). entrance/time are unused then.
    # SOH3D / ZELDA3D_AUTO / ZELDA3D_N64ANIM are ON BY DEFAULT in the binary now (the unified renderer is
    # the default, no flags) — only pass them through when explicitly overridden (e.g. SOH3D=0 for an
    # N64 A/B reference). ZELDA3D_WARP stays a launcher convenience for the entrance-warp test harness.
    setsid env ${SOH3D:+SOH3D="$SOH3D"} ZELDA3D_WARP="${ZELDA3D_WARP-1}" ${ZELDA3D_AUTO:+ZELDA3D_AUTO="$ZELDA3D_AUTO"} ${ZELDA3D_N64ANIM:+ZELDA3D_N64ANIM="$ZELDA3D_N64ANIM"} \
        ZELDA3D_VULKAN="${ZELDA3D_VULKAN:-}" ZELDA3D_VK_VALIDATION="${ZELDA3D_VK_VALIDATION:-}" \
        ZELDA3D_SDL3GPU="${ZELDA3D_SDL3GPU:-}" ZELDA3D_SDL3GPU_DEBUG="${ZELDA3D_SDL3GPU_DEBUG:-}" \
        ZELDA3D_RMLUI_OPEN="${ZELDA3D_RMLUI_OPEN:-}" \
        ZELDA3D_ENTRANCE="$entr" ZELDA3D_TIME="$time" ZELDA3D_REPL="$FIFO" \
        DISPLAY="${DISPLAY:-:0}" XAUTHORITY="$xauth" \
        stdbuf -oL -eL "$SOH" oot >"$LOG" 2>&1 < /dev/null &
    local pid=$!; echo "$pid" > "$PIDFILE"
    # wait for REPL ready (the .out file appears) — up to ~40s. Liveness is checked on THIS
    # instance's own pid so a parallel instance's failure isn't masked by the others still running.
    for _ in $(seq 1 80); do
        [ -e "$FIFO.out" ] && { echo "ready (pid $pid${INST:+ inst $INST on $DISPLAY})"; return 0; }
        kill -0 "$pid" 2>/dev/null || { echo "FAILED to boot — see $LOG" >&2; tail -5 "$LOG" >&2; return 1; }
        sleep 0.5
    done
    echo "WARN: booted but REPL not ready after 40s (pid $pid)" >&2; return 1
}

case "${1:-}" in
    start)   shift; start "$@" ;;
    restart) shift; cmake --build "$REPO/Shipwright/build-cmake" --target zelda3d_app -j"$(nproc)" || exit 1; start "$@" ;;
    stop)    stop ;;
    status)  status ;;
    log)     shift; tail "${1:--n}" "${2:-40}" "$LOG" 2>/dev/null || tail -40 "$LOG" ;;
    *) echo "usage: $0 {start|restart|stop|status|log} [entrance] [time]"; exit 2 ;;
esac
