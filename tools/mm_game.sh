#!/usr/bin/env bash
# Single-instance native-MM (2s2h) headless game manager — the MM counterpart to zelda3d_game.sh.
# Boots MM under a private Xvfb with the debug-warp gate AND both control FIFOs wired:
#   - shared scripted-input FIFO ($SHIP_SCRIPTED_FIFO, libultraship ScriptedInputFifo) for input
#   - MM per-game REPL FIFO      ($ZELDA3D_MM_REPL, mm/2s2h/Z3DRepl.c) for PlayState queries
# Drive it with tools/mm_control.py. Detaches via setsid+nohup so the instance survives across
# agent tool calls. See docs/MM_NATIVE.md (N3.4 phase 2b).
#
# ONE program binary now runs both games: `zelda3d mm ...` / `zelda3d oot ...` (see
# tools/zelda3d_game.sh for the OoT side). Because both games are the SAME executable, exe-path
# alone no longer identifies an instance -- see tools/zelda3d_proc.sh, which this sources.
#
# Subcommands:
#   start [entrance]   kill all, clear FIFOs, launch ONE detached instance, wait for gameplay
#   restart [entrance] build the zelda3d_app target, then start
#   stop               kill ALL "zelda3d mm" instances (incl. "(deleted)") + this instance's Xvfb +
#                       FIFOs -- never an OoT instance, even one on the same exe
#   status             list running "zelda3d mm" pids; non-zero exit if != 1 instance
#   shot <name>        capture the current frame -> scratch/screenshots/<name>.png
#   log [-f]           tail the run log
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/tools/zelda3d_proc.sh"
# Binary path: the one zelda3d launcher, run as `zelda3d mm`. Override with ZELDA3D_MM to run a
# launcher built into a different dir. GAMEDIR derives the MM core's own asset directory as the
# launcher's build-tree sibling ".../<build>/mm" -- the launcher resolves that same sibling
# internally, so ROM provisioning (which the launcher does not do) targets exactly where the core
# will look.
MM="${ZELDA3D_MM:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
GAMEDIR="$(dirname "$(dirname "$MM")")/mm"
DISP="${ZELDA3D_MM_DISPLAY:-:94}"
LOGDIR="$REPO/scratch/logs/mm_n2"
LOG="$LOGDIR/run_mm.log"
PIDFILE="$REPO/scratch/mm.pid"
SHOTDIR="$REPO/scratch/screenshots"
export SHIP_SCRIPTED_FIFO="${SHIP_SCRIPTED_FIFO:-$LOGDIR/mm_input.fifo}"
export ZELDA3D_MM_REPL="${ZELDA3D_MM_REPL:-$LOGDIR/mm_repl.fifo}"
mkdir -p "$LOGDIR" "$SHOTDIR"

# List pids of every running "zelda3d mm" instance (see tools/zelda3d_proc.sh). Matches the exe
# path with OR without a trailing " (deleted)" (a rebuilt-over binary) AND requires argv[1]=="mm",
# since the SAME binary also runs OoT -- exe-path alone would match a concurrently-running OoT
# instance too, and this manager must never touch that.
mm_pids() { zelda3d_pids "$MM" mm; }

stop() {
    local pids; pids="$(mm_pids)"
    [ -n "$pids" ] && kill $pids 2>/dev/null
    sleep 1
    pids="$(mm_pids)"; [ -n "$pids" ] && kill -9 $pids 2>/dev/null
    # This instance's Xvfb only (matched by the display arg, not a blanket pkill).
    local xp; xp="$(pgrep -f "Xvfb $DISP " 2>/dev/null)"
    [ -n "$xp" ] && kill $xp 2>/dev/null
    rm -f "$SHIP_SCRIPTED_FIFO" "$SHIP_SCRIPTED_FIFO.out" "$ZELDA3D_MM_REPL" "$ZELDA3D_MM_REPL.out" "$PIDFILE"
    echo "stopped"
}

start() {
    local entr="${1:-}"
    stop >/dev/null 2>&1
    # ROM provisioning: mirrors zelda3d_game.sh so ZELDA3D_MM_ROM / ZELDA3D_MM3D_ROM
    # (needed by the mm3d asset provider) come from .env when the caller shell hasn't
    # already exported them.
    [ -f "$REPO/.env" ] && . "$REPO/.env"
    Xvfb "$DISP" -screen 0 1280x960x24 >"$LOGDIR/xvfb_mm.log" 2>&1 &
    sleep 2
    ( cd "$GAMEDIR" && setsid nohup env -u WAYLAND_DISPLAY DISPLAY="$DISP" \
        SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
        ZELDA3D_MM_WARP=1 ${entr:+ZELDA3D_MM_ENTRANCE="$entr"} \
        ${ZELDA3D_MM_ROM:+ZELDA3D_MM_ROM="$ZELDA3D_MM_ROM"} \
        ${ZELDA3D_MM3D_ROM:+ZELDA3D_MM3D_ROM="$ZELDA3D_MM3D_ROM"} \
        SHIP_SCRIPTED_FIFO="$SHIP_SCRIPTED_FIFO" ZELDA3D_MM_REPL="$ZELDA3D_MM_REPL" \
        "$MM" mm >"$LOG" 2>&1 & echo $! >"$PIDFILE" )
    echo "mm pid=$(cat "$PIDFILE") disp=$DISP entrance=${entr:-default}"
    echo "input_fifo=$SHIP_SCRIPTED_FIFO repl_fifo=$ZELDA3D_MM_REPL"
    # Wait for gameplay: poll the REPL posinfo until Link exists.
    for _ in $(seq 1 40); do
        sleep 1
        if python3 "$REPO/tools/mm_control.py" query posinfo 2>/dev/null | grep -q "pos=("; then
            echo "gameplay reached"; return 0
        fi
        if ! kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null; then
            echo "GAME EXITED early (see $LOG)"; return 3
        fi
    done
    echo "WARNING: gameplay not confirmed within 40s (see $LOG)"; return 0
}

case "${1:-}" in
    start)   shift; start "${1:-}" ;;
    # Cap parallelism: this machine has ~15GB RAM and a full-core link of mm/soh OOMs. Override with
    # ZELDA3D_BUILD_JOBS if you know you have headroom.
    restart) shift; cmake --build "$REPO/Shipwright/build-cmake" --target zelda3d_app -j"${ZELDA3D_BUILD_JOBS:-4}" && start "${1:-}" ;;
    stop)    stop ;;
    status)
        n=$(mm_pids | wc -l)
        if [ "$n" -eq 0 ]; then echo "zelda3d mm instances: 0 (searched exe=$MM[ (deleted)] argv[1]=mm)"
        else echo "zelda3d mm instances: $n"; mm_pids; fi
        [ "$n" -eq 1 ] || exit 1 ;;
    shot)
        DISPLAY="$DISP" import -window root "$SHOTDIR/${2:?name}.png" && echo "shot -> ${2}.png" ;;
    log)    shift; tail "${1:-}" "$LOG" ;;
    *) echo "usage: mm_game.sh {start [entrance]|restart [entrance]|stop|status|shot <name>|log [-f]}"; exit 2 ;;
esac
