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
# currently up. Any order works, including a core twice: the `_exit(0)` at the end of OoT's DeinitOTR
# that once made OoT last-only (claim C057) was deleted, and `oot,oot` has since been run and measured
# here. The default is mm,oot only because that is the pairing the split was built for.
#
#   usage: tools/zelda3d_sequence.sh [mm,oot]   (also: oot,oot / mm,mm -- used for leak differentials)
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
# Seconds to wait for a core that HAS opened its REPL to reach a real scene. Separate from BOOTWAIT
# and configurable for the same reason BOOTWAIT is: this was hardcoded at 60, which is generous for a
# release build and not enough for a sanitizer one, and the gate then reported "NEVER REACHED A SCENE"
# for a core that was merely slow. That is the failure BOOTWAIT's own comment describes, one budget
# further along -- a fixed limit sitting next to a configurable one is a bug waiting for a slow build.
SCENEWAIT="${ZELDA3D_SEQ_SCENE_WAIT:-60}"
# Seconds to let the launcher exit on its own after the last core is quit. Third budget in this file
# to need a knob for the same reason: 30 is fine for a release build and not enough under a sanitizer,
# where AddressSanitizer's at-exit leak scan runs AFTER main returns. Killing during that scan loses
# the leak report entirely and looks identical to "there were no leaks".
EXITWAIT="${ZELDA3D_SEQ_EXIT_WAIT:-30}"
# Overridable so a caller running several sequences can keep each one's log. It is a FIXED path by
# default, and `rm -f "$LOG"` below means the next run destroys the previous one's -- which lost the
# game-side diagnostics for a captured crash (docs/issues/0022): the ASAN report was preserved
# per-sequence but the log holding the probe output that would have explained it was already gone.
LOGDIR="${ZELDA3D_SEQ_LOGDIR:-$REPO/scratch/logs/sequence}"
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

# Reap the launcher if THIS script dies before its own cleanup runs -- interrupted, timed out, or
# killed by whatever started it. Without this the launcher outlives the run, and because every
# sequence uses the SAME REPL FIFO paths, the orphan then answers the NEXT run's commands: a live
# `posinfo` came back with a scene the current core had never been in, which reads as inherited game
# state rather than as two processes on one pipe. Kill BY PID, never by name -- other agents and the
# user run this same binary. See CLAUDE.md, "Never pkill a shared binary name".
cleanup_launcher() {
    kill "$SEQPID" 2>/dev/null
    # Give it a moment to unwind, then insist. A sanitizer build can sit in its leak scan.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        kill -0 "$SEQPID" 2>/dev/null || return 0
        sleep 1
    done
    kill -9 "$SEQPID" 2>/dev/null
}
trap 'cleanup_launcher; exit 130' INT TERM HUP

# Quit each core in turn. A core is ready to be quit once its REPL FIFO exists; sending before that
# writes into nothing. Anything unquit after its budget leaves the process hanging, which the final
# kill covers -- but the log then says which core never came up, instead of the run just timing out.
RAN_FAIL=0
# Counted, not a boolean. RAN_FAIL can only be set INSIDE the per-core loop, so a launcher that
# dies before the loop runs leaves it 0 and the verdict below said "yes -- every core answered
# posinfo" for a run in which ZERO cores started. A count carries its own denominator and cannot
# be vacuously true over an empty set.
RAN_OK=0
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
    for _ in $(seq 1 "$SCENEWAIT"); do
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
        echo "SEQUENCE: core '$id' NEVER REACHED A SCENE within ${SCENEWAIT}s (ZELDA3D_SEQ_SCENE_WAIT)"
        echo "SEQUENCE: -- it opened a REPL (or inherited one) but did"
        echo "          not run a game. last posinfo reply: $(cat "$fifo.out" 2>/dev/null || echo "(none)")"
        RAN_FAIL=1
    else
        echo "SEQUENCE: core '$id' is live: $SCENE"
        RAN_OK=$((RAN_OK + 1))
    fi
    # Optional extra REPL commands, run in each core once it is live. This is how a sequence exercises
    # a code path the boot does not reach on its own -- the first user is `randogen`, because the deep
    # check's own verdict says the randomizer ownership paths go unexercised, and those are paths this
    # arc changed. Replies are ECHOED, not swallowed: a command that silently did nothing would
    # otherwise make the sequence look like it covered more than it did.
    #
    # Per-core override: ZELDA3D_SEQ_CMDS_OOT / ZELDA3D_SEQ_CMDS_MM take precedence over the shared
    # ZELDA3D_SEQ_CMDS. Needed the moment the commands stop being game-agnostic -- `warp` exists in
    # both REPLs but entrance indices are per-game, so one shared list would send OoT's Kokiri Forest
    # to MM and quietly land somewhere else (or nowhere).
    #
    # `sleep:<n>` is a pseudo-command handled here rather than sent: after a warp the reply returns
    # immediately while the load takes seconds, and neither REPL has a game-agnostic settle.
    eval "CMDS=\${ZELDA3D_SEQ_CMDS_$(echo "$id" | tr '[:lower:]' '[:upper:]'):-\${ZELDA3D_SEQ_CMDS:-}}"
    if [ -n "$CMDS" ]; then
        echo "$CMDS" | tr ';' '\n' | while IFS= read -r c; do
            c="$(printf '%s' "$c" | sed 's/^ *//; s/ *$//')"
            [ -n "$c" ] || continue
            case "$c" in
                sleep:*)
                    echo "SEQUENCE: '$id' waiting ${c#sleep:}s (load/settle)"
                    sleep "${c#sleep:}"
                    continue
                    ;;
            esac
            # A dead launcher is not a slow one. Opening a FIFO for writing BLOCKS until a reader
            # opens it, so once the core has crashed every remaining command sits for the full
            # CMD_WAIT -- a tour of six commands turned a crash into a 90-minute stall three times
            # today, and the run looks like a hang rather than the crash it is. Check first and say
            # which it was.
            if ! kill -0 "$SEQPID" 2>/dev/null; then
                echo "SEQUENCE: '$id' cmd '$c' -> SKIPPED, the launcher (pid $SEQPID) is GONE -- it died"
                echo "          earlier in this tour. Check $LOG for a crash and the run's ASAN_OPTIONS log_path for a report."
                break
            fi
            rm -f "$fifo.out"
            if timeout "${ZELDA3D_SEQ_CMD_WAIT:-300}" sh -c 'printf "%s\n" "$2" > "$1"' _ "$fifo" "$c"; then
                # The reply file appears when the command finishes, which for a blocking command
                # (seed generation) is much later than the write returning.
                for _ in $(seq 1 "${ZELDA3D_SEQ_CMD_WAIT:-300}"); do
                    [ -s "$fifo.out" ] && break
                    sleep 1
                done
                echo "SEQUENCE: '$id' cmd '$c' -> $(cat "$fifo.out" 2>/dev/null || echo "(NO REPLY -- it did not answer)")"
            else
                echo "SEQUENCE: '$id' cmd '$c' -> NOT ACCEPTED (no reader on $fifo)"
            fi
        done
    fi
    # Optional dwell: keep the core alive after it has reached a scene, so the run exercises more
    # than the first playable frame. Added because a bug was only reachable deeper into the title
    # demo than this gate had ever run -- `oot` alone loaded 3 player animations where a longer run
    # loads more, and "the gate is clean" meant "the gate stopped early".
    if [ "${ZELDA3D_SEQ_DWELL:-0}" != "0" ]; then
        echo "SEQUENCE: dwelling ${ZELDA3D_SEQ_DWELL}s in '$id' before quitting"
        sleep "$ZELDA3D_SEQ_DWELL"
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

for _ in $(seq 1 "$EXITWAIT"); do sleep 1; kill -0 "$SEQPID" 2>/dev/null || break; done
if kill -0 "$SEQPID" 2>/dev/null; then
    echo "SEQUENCE: launcher still alive ${EXITWAIT}s after the sequence (ZELDA3D_SEQ_EXIT_WAIT) -- killing pid $SEQPID"
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
if [ "$RAN_FAIL" = 0 ] && [ "$RAN_OK" -eq "${#CORES[@]}" ]; then
    echo "   yes -- $RAN_OK of ${#CORES[@]} cores answered posinfo with a real scene (per-core lines above)"
else
    echo "   NO -- only $RAN_OK of ${#CORES[@]} cores ran a game; scroll up for which one."
    RC=1
fi
# GPU handle ownership. SDL3's Vulkan backend QUEUES a released resource instead of destroying it, so
# releasing one handle twice is silent at the call site and aborts much later, wherever the queue
# happens to be flushed -- issue 0009 collected four different innocent abort sites before the check
# that names the offender was written. It lives at the release now, and this is the gate for it.
#
# The missing-line case is failed deliberately: "0 released twice" and "the renderer never tore down"
# print identically if you only grep for the number.
echo "-- GPU handles released more than once (want: 0):"
DUPLINE="$(grep -E "released [0-9]+ handle\(s\) tearing down" "$LOG" | tail -1)"
if [ -z "$DUPLINE" ]; then
    echo "   UNKNOWN -- the renderer never printed its release accounting, so it did not tear down."
    echo "   That is a failure, not a pass: this check cannot distinguish 'no duplicates' from 'never ran'."
    RC=1
elif echo "$DUPLINE" | grep -qE ", 0 of them released more than once"; then
    echo "   ${DUPLINE#*] }"
else
    echo "   FAIL -- ${DUPLINE#*] }"
    grep -E "DOUBLE RELEASE" "$LOG" || echo "   (no DOUBLE RELEASE line found, which should be impossible)"
    RC=1
fi
# How much of the game this run actually exercised. Stated because a clean verdict otherwise reads as
# "the game is fine" when it means "the first playable frame is fine": with no dwell, `oot` loads 3
# player animations and quits, and an out-of-bounds read in the title demo's horse segment went
# unseen for the whole of issue 0018 -- attributed to the launcher three times -- purely because the
# gate never ran that far. A blind spot that is not printed is one nobody remembers.
echo "-- how much was exercised:"
if [ "${ZELDA3D_SEQ_DWELL:-0}" = "0" ]; then
    echo "   each core was quit as soon as it reached a scene (no dwell). This gate says NOTHING about"
    echo "   anything past the first playable frame; use ZELDA3D_SEQ_DWELL=<seconds> to go further."
else
    echo "   each core was held ${ZELDA3D_SEQ_DWELL}s after reaching a scene before being quit."
fi
echo "-- crashes:"
grep -iE "segmentation|SIGSEGV|SIGABRT|dumped core|terminate called|double free" "$LOG" || echo "   (none in the log; check the exit code above)"
exit "$RC"
