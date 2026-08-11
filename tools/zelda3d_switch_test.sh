#!/usr/bin/env bash
# Gate for the in-process game switch: choose Majora's Mask in the launcher's chooser and prove the
# game changed WITHOUT the process changing.
#
# Why this exists next to tools/zelda3d_sequence.sh rather than inside it. The sequence gate drives
# `zelda3d --run-sequence oot,mm`, which loads two cores back to back because the LAUNCHER was told
# to. That measures whether two cores can coexist; it cannot see the chooser at all, and a
# regression that made the Majora's Mask row exec a process again would leave it perfectly green.
# This drives the row a user actually clicks.
#
# The four things it asserts, and why each alone is not enough:
#   PID UNCHANGED   -- an exec produces a nearly identical log: same "starting Majora's Mask" line,
#                      same MM boot, same everything except the process. The pid is the only
#                      observable that separates a handover from a process swap, so it is checked
#                      directly against /proc rather than inferred from log text.
#   MM REACHES A SCENE -- "the MM core attached" is not "MM runs". A second core can install its
#                      session and then die on its first frame, or sit forever without a PlayState.
#                      So the gate polls MM's own REPL for `posinfo` until it reports a real scene
#                      id. It must NOT accept any non-empty reply: MM answers
#                      `posinfo scene=-1 (no PlayState)` while booting, and the first version of
#                      this gate passed green while printing exactly that.
#   IT COMES BACK   -- returning to the chooser runs the OoT core a SECOND time in this process,
#                      which is the half that was impossible until 2026-08-11 (docs/issues/0016).
#                      Alive is not enough: the core could re-run straight into the game, so the
#                      pass condition is the chooser DOCUMENT being visible.
#   THE ROW WORKS   -- assertion 3 drives the MECHANISM (REPL `switchgame`). A row that is mis-spelt,
#                      in the wrong pane, or missing from the shipped document would leave that
#                      green while the user has no way to reach it, so the ESC menu's "Return to
#                      Launcher" row is activated BY NAME and then checked for landing at the
#                      chooser.
#
# Every failure says which assertion failed and what was observed. A run that could not get as far
# as the chooser reports THAT, rather than a bare non-zero that reads like "the switch is broken".
#
# Usage: tools/zelda3d_switch_test.sh          (exit 0 = the switch works)
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAUNCHER="${ZELDA3D_LAUNCHER_BIN:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
DISP="${ZELDA3D_SWITCH_DISPLAY:-:97}"
LOGDIR="$REPO/scratch/logs/switch"
LOG="$LOGDIR/run.log"
export ZELDA3D_REPL="${ZELDA3D_SWITCH_OOT_REPL:-$LOGDIR/oot_repl.fifo}"
export ZELDA3D_MM_REPL="${ZELDA3D_SWITCH_MM_REPL:-$LOGDIR/mm_repl.fifo}"

mkdir -p "$LOGDIR"
rm -f "$LOG" "$ZELDA3D_REPL" "$ZELDA3D_REPL.out" "$ZELDA3D_MM_REPL" "$ZELDA3D_MM_REPL.out"

[ -x "$LAUNCHER" ] || { echo "SWITCH: no launcher at $LAUNCHER -- build target zelda3d_app" >&2; exit 2; }
[ -f "$REPO/.env" ] && . "$REPO/.env"

# Private Xvfb on OUR display. Reusing whatever is already running is how a previous gate ended up
# reporting a renderer fault that was really another agent's server on a shared display.
if ! DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1; then
    setsid Xvfb "$DISP" -screen 0 1280x960x24 >"$LOGDIR/xvfb.log" 2>&1 &
    for _ in $(seq 1 20); do sleep 0.5; DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 && break; done
fi
DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 || { echo "SWITCH: Xvfb failed on $DISP" >&2; exit 3; }

# cd into the OoT core's asset dir, as run.sh does: the engine resolves archives relative to the
# current directory for the FIRST game, and the launcher re-chdirs itself for the next one.
# ZELDA3D_LAUNCHER=1 because the chooser IS the thing under test (the other tools disable it).
cd "$REPO/Shipwright/build-cmake/soh" || exit 3
env -u WAYLAND_DISPLAY DISPLAY="$DISP" XAUTHORITY=/dev/null \
    SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    SOH3D=1 ZELDA3D_LAUNCHER=1 ZELDA3D_MM_WARP=1 \
    "$LAUNCHER" >"$LOG" 2>&1 &
PID=$!
echo "SWITCH: launcher pid=$PID disp=$DISP log=$LOG"

cleanup() { kill -9 "$PID" 2>/dev/null; }
trap cleanup EXIT

# The chooser is up once OoT's REPL FIFO exists.
for _ in $(seq 1 90); do sleep 1; [ -p "$ZELDA3D_REPL" ] && break; done
if [ ! -p "$ZELDA3D_REPL" ]; then
    echo "SWITCH: FAILED BEFORE THE TEST BEGAN -- the OoT core never opened its REPL, so the chooser"
    echo "        was never reached and NOTHING about switching was measured. Last log lines:"
    tail -5 "$LOG" | sed 's/^/          /'
    exit 1
fi
sleep 3   # let the launcher gamestate settle so the choice is not consumed mid-init

echo "SWITCH: choosing Majora's Mask (the row a user clicks)"
printf 'launcher pick mm\n' > "$ZELDA3D_REPL"

# MM is up once ITS REPL exists. This is also the switch's first observable: OoT's core has to have
# returned for the launcher to have loaded MM at all.
for _ in $(seq 1 120); do sleep 1; [ -p "$ZELDA3D_MM_REPL" ] && break; done

FAIL=0

# --- assertion 1: same process ------------------------------------------------------------------
EXE="$(readlink "/proc/$PID/exe" 2>/dev/null)"
if [ -z "$EXE" ]; then
    echo "SWITCH: FAIL (pid) -- pid $PID is GONE. The chooser ended the process instead of handing"
    echo "        back to the launcher; that is the exec behaviour this feature replaced."
    FAIL=1
else
    echo "SWITCH: ok (pid)  -- pid $PID still alive, exe=$EXE"
fi

# --- assertion 2: MM is actually playing ---------------------------------------------------------
if [ ! -p "$ZELDA3D_MM_REPL" ]; then
    echo "SWITCH: FAIL (mm) -- MM's REPL never appeared, so MM did not start. Launcher lines:"
    grep -E "LAUNCHER" "$LOG" | tail -5 | sed 's/^/          /'
    FAIL=1
else
    # posinfo, polled until MM has a real scene. What must NOT be accepted is any non-empty reply:
    # MM answers `posinfo scene=-1 (no PlayState)` while it is still booting, and treating that as
    # success is how this gate passed green the first time it ran while reporting, in its own output,
    # that there was no PlayState. So the pass condition is a scene id that is not -1, and the
    # failure message quotes the LAST reply actually seen rather than saying "no answer".
    POS=""
    for _ in $(seq 1 60); do
        rm -f "$ZELDA3D_MM_REPL.out"
        printf 'posinfo\n' > "$ZELDA3D_MM_REPL"
        sleep 1
        POS="$(cat "$ZELDA3D_MM_REPL.out" 2>/dev/null)"
        case "$POS" in
            *"scene=-1"*|"") continue ;;
            *"scene="*) break ;;
        esac
    done
    case "$POS" in
        *"scene="*) case "$POS" in *"scene=-1"*) POS="" ;; esac ;;
        *) POS="" ;;
    esac
    if [ -z "$POS" ]; then
        echo "SWITCH: FAIL (mm) -- MM's REPL is up but it never reached a scene within 60s."
        echo "        last posinfo reply: $(cat "$ZELDA3D_MM_REPL.out" 2>/dev/null || echo "(none)")"
        echo "        The SWITCH itself may still be fine -- this says MM did not get to gameplay."
        FAIL=1
    else
        echo "SWITCH: ok (mm)   -- MM is in a live scene: $POS"
    fi
fi

# --- assertion 3: the way BACK --------------------------------------------------------------------
# The round trip is the half that was impossible until a core became re-runnable: returning to the
# chooser means running the OoT core a SECOND time in this process. It is asserted here rather than
# assumed from `oot,oot` passing, because that sequence is driven by --run-sequence and never sees
# the chooser -- the same reason assertion 2 exists next to the sequence gate.
#
# Skipped, loudly, if MM never came up: "the way back works" measured from a game that never started
# is the kind of green that means nothing.
if [ ! -p "$ZELDA3D_MM_REPL" ]; then
    echo "SWITCH: SKIPPED (return) -- MM never started, so the round trip could not be attempted."
    echo "        This is NOT a pass: nothing about returning to the chooser was measured."
    FAIL=1
else
    echo "SWITCH: asking MM to return to the chooser (switchgame oot)"
    rm -f "$ZELDA3D_REPL" "$ZELDA3D_REPL.out"
    printf 'switchgame oot\n' > "$ZELDA3D_MM_REPL"

    # OoT's REPL reappearing is the observable that the OoT core ran again.
    for _ in $(seq 1 120); do sleep 1; [ -p "$ZELDA3D_REPL" ] && break; done

    if [ -z "$(readlink "/proc/$PID/exe" 2>/dev/null)" ]; then
        echo "SWITCH: FAIL (return) -- pid $PID died on the way back. Returning to the chooser is a"
        echo "        SECOND run of the OoT core; if that crashes, the ESC menu's row cannot exist."
        tail -8 "$LOG" | sed 's/^/          /'
        FAIL=1
    elif [ ! -p "$ZELDA3D_REPL" ]; then
        echo "SWITCH: FAIL (return) -- the OoT core never opened its REPL again within 120s, so it did"
        echo "        not get back to a frame loop. Launcher lines:"
        grep -E "LAUNCHER" "$LOG" | tail -5 | sed 's/^/          /'
        FAIL=1
    else
        # Being alive is not being AT THE CHOOSER -- the core could have booted into the game
        # instead. `launcher` reports the document's own visibility, which is the thing a user sees.
        VIS=""
        for _ in $(seq 1 30); do
            rm -f "$ZELDA3D_REPL.out"
            printf 'launcher\n' > "$ZELDA3D_REPL"
            sleep 1
            VIS="$(cat "$ZELDA3D_REPL.out" 2>/dev/null)"
            case "$VIS" in *"visible=1"*) break ;; esac
        done
        case "$VIS" in
            *"visible=1"*)
                echo "SWITCH: ok (return) -- back in one process, chooser on screen: $VIS" ;;
            *)
                echo "SWITCH: FAIL (return) -- the OoT core re-ran and is answering, but the chooser is not"
                echo "        visible. last launcher reply: ${VIS:-(none)}"
                FAIL=1 ;;
        esac
    fi
fi

# --- assertion 4: the ROW a user clicks ------------------------------------------------------------
# Assertion 3 drove the mechanism (REPL `switchgame`). This drives the ESC menu's "Return to
# Launcher" row itself, because a working mechanism behind a row that is mis-spelt, in the wrong
# pane, or not in the shipped document is still a feature the user cannot reach.
#
# `menurow <label>` activates by NAME and refuses if focus does not land on the row it matched, so a
# pass here cannot be a click that hit something else.
if [ ! -p "$ZELDA3D_REPL" ]; then
    echo "SWITCH: SKIPPED (row) -- no OoT REPL, so the menu row was never driven. NOT a pass."
    FAIL=1
else
    echo "SWITCH: starting Ocarina of Time to drive its ESC menu row"
    printf 'launcher pick oot\n' > "$ZELDA3D_REPL"
    SCENE=""
    for _ in $(seq 1 90); do
        rm -f "$ZELDA3D_REPL.out"
        printf 'posinfo\n' > "$ZELDA3D_REPL" 2>/dev/null
        sleep 1
        SCENE="$(cat "$ZELDA3D_REPL.out" 2>/dev/null)"
        case "$SCENE" in *"scene=-1"*|"") continue ;; *"scene="*) break ;; esac
    done
    case "$SCENE" in
        *"scene="*) case "$SCENE" in *"scene=-1"*) SCENE="" ;; esac ;;
        *) SCENE="" ;;
    esac
    if [ -z "$SCENE" ]; then
        echo "SWITCH: FAIL (row) -- OoT never reached a scene, so its ESC menu could not be opened."
        echo "        last posinfo reply: $(cat "$ZELDA3D_REPL.out" 2>/dev/null || echo "(none)")"
        FAIL=1
    else
        printf 'menu toggle\n' > "$ZELDA3D_REPL"; sleep 2
        rm -f "$ZELDA3D_REPL.out"
        printf 'menurow Return to Launcher\n' > "$ZELDA3D_REPL"
        # Read the reply FAST. Activating this row ends the run, and the run that replaces it writes
        # its own "SOH3D REPL ready" greeting into the same .out file -- so a leisurely read finds the
        # greeting where the reply was and reports the row as dead when it in fact worked. That is
        # exactly what happened the first time this assertion ran. Sampling in a tight loop keeps the
        # reply, and the greeting is treated as what it actually is: proof the run restarted.
        ROW=""
        for _ in $(seq 1 40); do
            sleep 0.2
            ROW="$(cat "$ZELDA3D_REPL.out" 2>/dev/null)"
            [ -n "$ROW" ] && break
        done
        case "$ROW" in
            *"activated"* | *"REPL ready"*)
                echo "SWITCH: ok (row)   -- $ROW"
                # And it must actually land back at the chooser, not merely be clickable.
                rm -f "$ZELDA3D_REPL"
                for _ in $(seq 1 120); do sleep 1; [ -p "$ZELDA3D_REPL" ] && break; done
                VIS2=""
                for _ in $(seq 1 30); do
                    rm -f "$ZELDA3D_REPL.out"
                    printf 'launcher\n' > "$ZELDA3D_REPL" 2>/dev/null
                    sleep 1
                    VIS2="$(cat "$ZELDA3D_REPL.out" 2>/dev/null)"
                    case "$VIS2" in *"visible=1"*) break ;; esac
                done
                case "$VIS2" in
                    *"visible=1"*) echo "SWITCH: ok (row)   -- the row landed back at the chooser: $VIS2" ;;
                    *) echo "SWITCH: FAIL (row) -- the row activated but the chooser never came back."
                       echo "        last launcher reply: ${VIS2:-(none)}"
                       FAIL=1 ;;
                esac ;;
            *)
                echo "SWITCH: FAIL (row) -- the ESC menu's Return to Launcher row could not be activated."
                echo "        menurow said: ${ROW:-(no reply)}"
                echo "        (menurow names the row it matched, or says how many it scanned, so this"
                echo "         line distinguishes a missing row from a menu that was never open.)"
                FAIL=1 ;;
        esac
    fi
fi

echo
echo "=== SWITCH VERDICT (exit $FAIL) ==="
grep -E "LAUNCHER: (handing back|.* core returned|switching|starting)" "$LOG" | sed 's/^/  /'
grep -E "different game is attaching" "$LOG" | sed 's/^/  /'
[ "$FAIL" = 0 ] && echo "  -> the chooser switched games INSIDE one process, the second game played, and it came BACK"

# Quit whichever core is up at the end -- after the round trip that is OoT, not MM.
for f in "$ZELDA3D_REPL" "$ZELDA3D_MM_REPL"; do
    [ -p "$f" ] && timeout 10 sh -c 'printf "quit\n" > "$1"' _ "$f" 2>/dev/null
done
sleep 4
exit "$FAIL"
