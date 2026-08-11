#!/usr/bin/env bash
# Run two game cores BACK TO BACK in one launcher process and report what the second one inherited.
#
# This is the acceptance test for docs/MM_NATIVE.md N3 -- the per-game/engine split of Ship::Context.
# It was assembled by hand twice before this existed, and both times the interesting part was not the
# launch but the FIFOs: `--run-sequence` starts a core and then waits, because a core only returns
# when its frame loop ends, and the only headless way to end it is the per-game REPL's `quit`. A
# sequence run without those wired looks like a hang and gets killed, which is exactly how the first
# attempt in this session was lost.
#
# So each core gets its REPL FIFO passed in, and this script sends `quit` to whichever core is
# currently up. Order matters: OoT's DeinitOTR ends in a deliberate `_exit(0)` (claim C057), so OoT
# can only ever be LAST -- put it first and the process dies before the second core starts.
#
#   usage: tools/zelda3d_sequence.sh [mm,oot]
#
# Output: scratch/logs/sequence/run.log, plus a verdict on stdout. The verdict reads the log for the
# classes libultraship distinguishes -- ENGINE state shared with the previous game (correct, that is
# what one libultraship.so is for), PER-GAME state inherited (the bug the split exists to remove),
# and SPLIT-PENDING state inherited because a subsystem is genuinely not divided yet (unfinished, not
# a regression) -- a category that is now EMPTY, Audio and Console having been its last two members.
# It prints the denominators either way: "no per-game inheritance" is only
# meaningful next to "and here is what the second core did install".
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SEQ="${1:-mm,oot}"
LAUNCHER="${ZELDA3D_LAUNCHER_BIN:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
DISP="${ZELDA3D_SEQ_DISPLAY:-:98}"
# Seconds to wait for a core to open its REPL, i.e. to reach its frame loop. 120 is plenty for a
# release build and nowhere near enough for a sanitizer one: an ASAN build on llvmpipe spent >120s
# between "SDL3 GPU backend initialized" and its first frame, and the gate reported that as "it did
# not reach its frame loop" -- a boot budget being read as a broken core.
BOOTWAIT="${ZELDA3D_SEQ_BOOT_WAIT:-120}"
LOGDIR="$REPO/scratch/logs/sequence"
LOG="$LOGDIR/run.log"
# Each core reads its own REPL path from its own env var; both are wired so either can be quit.
export ZELDA3D_REPL="${ZELDA3D_SEQ_OOT_REPL:-$LOGDIR/oot_repl.fifo}"
export ZELDA3D_MM_REPL="${ZELDA3D_SEQ_MM_REPL:-$LOGDIR/mm_repl.fifo}"
mkdir -p "$LOGDIR"
rm -f "$LOG" "$ZELDA3D_REPL" "$ZELDA3D_REPL.out" "$ZELDA3D_MM_REPL" "$ZELDA3D_MM_REPL.out"

[ -x "$LAUNCHER" ] || { echo "SEQUENCE: no launcher at $LAUNCHER -- build target zelda3d_app" >&2; exit 2; }
[ -f "$REPO/.env" ] && . "$REPO/.env"

# Private Xvfb. Started unconditionally on OUR display rather than reusing whatever is running:
# the first attempt in this session checked `pgrep -x Xvfb`, found another agent's server on :99,
# skipped its own, and the run then failed with "SDL window is null at Init()" -- a missing X server
# reported as a renderer fault.
if ! DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1; then
    setsid Xvfb "$DISP" -screen 0 1280x960x24 >"$LOGDIR/xvfb.log" 2>&1 &
    for _ in $(seq 1 20); do sleep 0.5; DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 && break; done
fi
DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 || { echo "SEQUENCE: Xvfb failed on $DISP" >&2; exit 3; }

env -u WAYLAND_DISPLAY DISPLAY="$DISP" XAUTHORITY=/dev/null \
    SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    ZELDA3D_LAUNCHER=0 ZELDA3D_MM_WARP=1 \
    "$LAUNCHER" --run-sequence "$SEQ" >"$LOG" 2>&1 &
SEQPID=$!
echo "SEQUENCE: launcher pid=$SEQPID seq=$SEQ disp=$DISP log=$LOG"

# Quit each core in turn. A core is ready to be quit once its REPL FIFO exists; sending before that
# writes into nothing. Anything unquit after its budget leaves the process hanging, which the final
# kill covers -- but the log then says which core never came up, instead of the run just timing out.
RAN_FAIL=0
IFS=',' read -r -a CORES <<<"$SEQ"
for id in "${CORES[@]}"; do
    fifo="$ZELDA3D_REPL"; [ "$id" = "mm" ] && fifo="$ZELDA3D_MM_REPL"
    ready=0
    for _ in $(seq 1 "$BOOTWAIT"); do
        sleep 1
        [ -p "$fifo" ] && { ready=1; break; }
        kill -0 "$SEQPID" 2>/dev/null || break
    done
    if [ "$ready" != "1" ]; then
        echo "SEQUENCE: core '$id' never opened its REPL ($fifo) -- it did not reach its frame loop"
        break
    fi
    # A FIFO is not a running game, and on a same-game sequence (oot,oot) it is not even evidence
    # that THIS core opened one -- the path is shared, so the previous core's FIFO satisfies the wait
    # above instantly. That is how this gate certified `oot,oot` while the second core was unwinding
    # on its first frame from an inherited exit request (docs/issues/0016 instance 9): both cores
    # "returned 0" and the run had never happened.
    #
    # So each core must ANSWER, with a real scene. `posinfo scene=-1 (no PlayState)` is the reply
    # while booting and must not be accepted -- the same trap the switch gate documents.
    SCENE=""
    for _ in $(seq 1 60); do
        rm -f "$fifo.out"
        timeout 5 sh -c 'printf "posinfo\n" > "$1"' _ "$fifo" 2>/dev/null || break
        sleep 1
        SCENE="$(cat "$fifo.out" 2>/dev/null)"
        case "$SCENE" in "" | *"scene=-1"*) continue ;; *"scene="*) break ;; esac
    done
    case "$SCENE" in
        *"scene="*) case "$SCENE" in *"scene=-1"*) SCENE="" ;; esac ;;
        *) SCENE="" ;;
    esac
    if [ -z "$SCENE" ]; then
        echo "SEQUENCE: core '$id' NEVER REACHED A SCENE -- it opened a REPL (or inherited one) but did"
        echo "          not run a game. last posinfo reply: $(cat "$fifo.out" 2>/dev/null || echo "(none)")"
        RAN_FAIL=1
    else
        echo "SEQUENCE: core '$id' is live: $SCENE"
    fi
    echo "SEQUENCE: asking '$id' to quit"
    # Bounded, because opening a FIFO for writing BLOCKS until a reader opens it -- and on a
    # same-game sequence (oot,oot) both cores share one $ZELDA3D_REPL path, so the file can still be
    # there with nobody reading it. That hung a whole `oot,oot` run at its second quit: both cores
    # had actually run to completion in the log, and the script sat in the write until its outer
    # timeout, printing no verdict at all. A hang that produces no verdict reads as a failed run.
    if ! timeout 15 sh -c 'printf "quit\n" > "$1"' _ "$fifo" 2>/dev/null; then
        echo "SEQUENCE: '$id' did not accept 'quit' within 15s (no reader on $fifo) -- moving on"
    fi
    for _ in $(seq 1 30); do sleep 1; [ -p "$fifo" ] || break; kill -0 "$SEQPID" 2>/dev/null || break; done
done

for _ in $(seq 1 30); do sleep 1; kill -0 "$SEQPID" 2>/dev/null || break; done
if kill -0 "$SEQPID" 2>/dev/null; then
    echo "SEQUENCE: launcher still alive after the sequence -- killing pid $SEQPID"
    kill -9 "$SEQPID" 2>/dev/null
fi
wait "$SEQPID" 2>/dev/null; RC=$?

echo
echo "=== SEQUENCE VERDICT (exit $RC) ==="
echo "-- cores that ran and returned:"
grep -E "starting \(core|RETURNED|FAILED to load|SKIPPED" "$LOG" || echo "   (none -- no core reached its run())"
echo "-- second core ATTACHED as a different game?"
grep -E "different game is attaching|Ending game session" "$LOG" || echo "   (none -- no second game attached; the split was never exercised)"
echo "-- PER-GAME state it installed for ITSELF (want: all four):"
grep -E "installed a FRESH" "$LOG" || echo "   (none)"
# The UNFINISHED (Audio/Console) message also says "INHERITED the previous game", so it is excluded
# here and reported in its own category below -- otherwise known unfinished work would read as a
# regression every run, and a category that always fires is one nobody reads.
echo "-- PER-GAME state it INHERITED (want: none; each line is the bug):"
grep -E "INHERITED the previous game" "$LOG" | grep -v "UNFINISHED" || echo "   (none)"
echo "-- ENGINE state shared with the previous game (expected, this is the design):"
grep -E "SHARED with the previous game" "$LOG" || echo "   (none)"
echo "-- UNFINISHED: subsystems inherited because they are not split yet (should be none):"
grep -E "Not a bug in the split, but UNFINISHED" "$LOG" || echo "   (none reported)"
echo "-- did each core actually RUN a game (not just return)?:"
if [ "$RAN_FAIL" = 0 ]; then
    echo "   yes -- every core answered posinfo with a real scene (see the per-core lines above)"
else
    echo "   NO -- at least one core returned without running a game; scroll up for which one."
    RC=1
fi
echo "-- crashes:"
grep -iE "segmentation|SIGSEGV|SIGABRT|dumped core|terminate called|double free" "$LOG" || echo "   (none in the log; check the exit code above)"
exit "$RC"
