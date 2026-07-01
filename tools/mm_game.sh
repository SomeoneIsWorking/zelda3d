#!/usr/bin/env bash
# Single-instance native-MM (2s2h) headless game manager — the MM counterpart to soh3d_game.sh.
# Boots mm.elf under a private Xvfb with the debug-warp gate AND both control FIFOs wired:
#   - shared scripted-input FIFO ($SHIP_SCRIPTED_FIFO, libultraship ScriptedInputFifo) for input
#   - MM per-game REPL FIFO      ($ZELDA3D_MM_REPL, mm/2s2h/Z3DRepl.c) for PlayState queries
# Drive it with tools/mm_control.py. Detaches via setsid+nohup so the instance survives across
# agent tool calls. See docs/MM_NATIVE.md (N3.4 phase 2b).
#
# Subcommands:
#   start [entrance]   kill all, clear FIFOs, launch ONE detached instance, wait for gameplay
#   restart [entrance] build the mm target, then start
#   stop               kill ALL mm.elf (incl. "(deleted)") + this instance's Xvfb + FIFOs
#   status             list running mm.elf pids; non-zero exit if != 1 instance
#   shot <name>        capture the current frame -> scratch/screenshots/<name>.png
#   log [-f]           tail the run log
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MM="${ZELDA3D_MM:-$REPO/Shipwright/build-cmake/mm/mm.elf}"
GAMEDIR="$(dirname "$MM")"
DISP="${ZELDA3D_MM_DISPLAY:-:94}"
LOGDIR="$REPO/scratch/logs/mm_n2"
LOG="$LOGDIR/run_mm.log"
PIDFILE="$REPO/scratch/mm.pid"
SHOTDIR="$REPO/scratch/screenshots"
export SHIP_SCRIPTED_FIFO="${SHIP_SCRIPTED_FIFO:-$LOGDIR/mm_input.fifo}"
export ZELDA3D_MM_REPL="${ZELDA3D_MM_REPL:-$LOGDIR/mm_repl.fifo}"
mkdir -p "$LOGDIR" "$SHOTDIR"

# List pids of every running mm.elf — matches the path with OR without a trailing " (deleted)".
mm_pids() {
    local p t pid
    for p in /proc/*/exe; do
        t="$(readlink "$p" 2>/dev/null)" || continue
        case "$t" in
            "$MM"|"$MM ("*) pid="${p#/proc/}"; echo "${pid%/exe}" ;;
        esac
    done
}

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
    Xvfb "$DISP" -screen 0 1280x960x24 >"$LOGDIR/xvfb_mm.log" 2>&1 &
    sleep 2
    ( cd "$GAMEDIR" && setsid nohup env -u WAYLAND_DISPLAY DISPLAY="$DISP" \
        SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
        ZELDA3D_MM_WARP=1 ${entr:+ZELDA3D_MM_ENTRANCE="$entr"} \
        SHIP_SCRIPTED_FIFO="$SHIP_SCRIPTED_FIFO" ZELDA3D_MM_REPL="$ZELDA3D_MM_REPL" \
        ./mm.elf >"$LOG" 2>&1 & echo $! >"$PIDFILE" )
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
    restart) shift; cmake --build "$REPO/Shipwright/build-cmake" --target mm -j"$(nproc)" && start "${1:-}" ;;
    stop)    stop ;;
    status)
        n=$(mm_pids | wc -l); echo "mm.elf instances: $n"; mm_pids
        [ "$n" -eq 1 ] || exit 1 ;;
    shot)
        DISPLAY="$DISP" import -window root "$SHOTDIR/${2:?name}.png" && echo "shot -> ${2}.png" ;;
    log)    shift; tail "${1:-}" "$LOG" ;;
    *) echo "usage: mm_game.sh {start [entrance]|restart [entrance]|stop|status|shot <name>|log [-f]}"; exit 2 ;;
esac
