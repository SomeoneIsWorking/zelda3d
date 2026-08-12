#!/usr/bin/env bash
# Self-test for the boot-failure path: a core that cannot boot must RETURN to the launcher, not
# exit() the process.
#
# Why this exists as its own gate rather than a line in zelda3d_sequence.sh: the failure only happens
# when an asset is missing or corrupt, which no gate can arrange without moving the user's ROMs and
# archives around. So the cores carry a ZELDA3D_BOOTFAIL_TEST=1 hook that throws the same
# Zelda3D::CoreBootError the real asset checks throw, and this drives it. Without it the
# return-instead-of-exit contract ships untested, which is how it quietly reverts to exit() the next
# time someone edits the boot chain.
#
# It asserts BOTH directions, because "the process ended" cannot on its own distinguish a core that
# returned from a core that called exit() -- that is the whole bug being guarded against:
#   1. with the hook: the launcher prints "core returned 1 -- control is back in the launcher".
#      That line is only reachable AFTER run() returns.
#   2. without it: the same core boots and reaches a real scene (covered by zelda3d_sequence.sh,
#      which this script runs for the same core so a broken build cannot pass by failing everywhere).
#
#   usage: tools/zelda3d_bootfail_test.sh [oot|mm|both]     (default: both)
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAUNCHER="${ZELDA3D_LAUNCHER_BIN:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
LOGDIR="$REPO/scratch/logs/bootfail"
WHICH="${1:-both}"
mkdir -p "$LOGDIR"

[ -x "$LAUNCHER" ] || { echo "BOOTFAIL: no launcher at $LAUNCHER -- build first"; exit 2; }

games=()
case "$WHICH" in
    both) games=(oot mm) ;;
    oot|mm) games=("$WHICH") ;;
    *) echo "BOOTFAIL: unknown game \"$WHICH\" (want oot, mm or both)"; exit 2 ;;
esac

fail=0
for g in "${games[@]}"; do
    log="$LOGDIR/$g.log"
    ZELDA3D_HEADLESS=1 ZELDA3D_BOOTFAIL_TEST=1 SDL_AUDIODRIVER=dummy \
        timeout 180 "$LAUNCHER" "$g" > "$log" 2>&1
    rc=$?
    echo "BOOTFAIL: $g exited $rc (log: $log)"

    if [ "$rc" -eq 124 ]; then
        echo "  FAIL: timed out -- the core neither booted nor returned"
        fail=1
        continue
    fi
    # The exit code alone proves nothing here: a core that called exit(1) and a core that returned 1
    # produce the identical code. Only the launcher's own line can tell them apart.
    if grep -q "$g core returned .* -- control is back in the launcher" "$log"; then
        echo "  ok: control came back to the launcher (returned, did not exit the process)"
    else
        echo "  FAIL: the launcher never reported regaining control -- the core exit()ed the process"
        fail=1
    fi
    if grep -qE "cannot boot: ZELDA3D_BOOTFAIL_TEST=1" "$log"; then
        echo "  ok: the failure was reported with its reason"
    else
        echo "  FAIL: no 'cannot boot' line -- the hook did not fire, so this run tested NOTHING"
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "=== BOOTFAIL VERDICT (exit 0) === every core returned control instead of exiting."
    echo "    NOT covered: this exercises the RETURN, not the real asset checks that throw -- and not"
    echo "    the chooser, which lives inside the OoT core, so an OoT boot failure still ends the"
    echo "    process because there is nothing left to return to."
else
    echo "=== BOOTFAIL VERDICT (exit 1) === see the FAIL lines above."
fi
exit "$fail"
