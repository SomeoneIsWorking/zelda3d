#!/usr/bin/env bash
# The DEEP re-runnability check: sanitizer build + dwell, across the sequences that matter.
#
# Why this exists as its own script. tools/zelda3d_sequence.sh quits each core the moment it reaches a
# scene, and its own verdict says so -- but that caveat was easy to read past, and it cost a real bug:
# `oot,oot` passed the normal gate for weeks while run 2 was crashing in TitleRider::releaseMount,
# because a release build never got far enough into the title sequence to call it. It was found only
# because a SANITIZER run is slow enough to stay in the title cs (docs/issues/0016).
#
# So the two knobs that found it are the two this script sets: the sanitizer build, and a dwell that
# keeps each core in-game past its first playable frame. Neither is the default gate's job -- this run
# takes tens of minutes -- but "the fast gate is green" is not evidence about anything past frame one,
# and there needs to be somewhere that says what IS covered.
#
#   usage: tools/zelda3d_deep_check.sh [dwell-seconds]        (default 60)
#          ZELDA3D_DEEP_SEQS="oot,oot mm,oot,mm"  to override the sequence list
#
# Requires the sanitizer build at scratch/build-asan (configure with -DZELDA3D_SANITIZE=address).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASAN_BIN="${ZELDA3D_ASAN_BIN:-$REPO/scratch/build-asan/zelda3d/zelda3d}"
DWELL="${1:-60}"
SEQS="${ZELDA3D_DEEP_SEQS:-oot,oot mm,mm mm,oot,mm}"
OUT="$REPO/scratch/logs/deep_check"

if [ ! -x "$ASAN_BIN" ]; then
    # Refuse rather than fall back to the release build: a "deep check" that silently ran without the
    # sanitizer would report a pass that means far less than it looks like.
    echo "DEEP: no sanitizer build at $ASAN_BIN"
    echo "DEEP: configure one with -DZELDA3D_SANITIZE=address -B scratch/build-asan, then rebuild."
    exit 2
fi

rm -rf "$OUT"; mkdir -p "$OUT"

# Pass a kill through to whichever sequence is running, so interrupting a 30-minute deep run does not
# leave a launcher behind on the shared REPL FIFO. It did: two killed runs left three orphans that
# then answered the next run's commands, and the resulting log read as a core inheriting another
# core's scene. zelda3d_sequence.sh traps the signal and reaps its own launcher; this just makes sure
# it receives one. By PID, never by name.
trap 'kill "$SEQ_CHILD" 2>/dev/null; exit 130' INT TERM HUP
SEQ_CHILD=""
# What each core is driven through once it is live. Dwell alone sits wherever the core spawned, which
# the verdict has always said -- and "time in-game" is not coverage: the first tour run found three
# crashes in half an hour, none of which the dwell had ever reached. Scene loads and teardowns are
# where this project's allocation bugs live, so the tour is warps.
#
# Per-game because entrance indices are: OoT 0xEE / 0x209 are two different Kokiri Forest spawns
# (0x209 is the one that exposed the entrance-index bug) and 0x109 is Zora's River; MM 0x5400 is
# Termina Field and 0x6800 Great Bay Coast. `sleep:N` waits for the load -- 30s because a sanitizer
# build on llvmpipe is slow, and a posinfo taken too early reports the OLD scene, which would read as
# a warp that did nothing. Every reply is echoed, so a warp that lands nowhere is visible.
# The leading sleep is not padding. A core answers `posinfo` with a real scene as soon as the BOOT
# warp has loaded one, while that warp's own transition can still be in flight -- and z_play.c:786
# DISCARDS a transition trigger raised while another is pending. Under a sanitizer build that window
# is wide enough to swallow the tour's first warp, which then reads as "the tour ran" while the core
# never left the title. The warp reply says `A WARP WAS ALREADY PENDING` when it happens, so this is
# visible rather than assumed -- grep the echoed replies if a tour looks like it covered nothing.
OOT_TOUR="${ZELDA3D_DEEP_CMDS_OOT:-sleep:30; randogen; warp 0xEE; sleep:30; posinfo; warp 0x209; sleep:30; posinfo; warp 0x109; sleep:30; posinfo}"
MM_TOUR="${ZELDA3D_DEEP_CMDS_MM:-sleep:20; warp 0x5400; sleep:30; posinfo; warp 0x6800; sleep:30; posinfo}"

fail=0
for seq in $SEQS; do
    d="$OUT/$(echo "$seq" | tr ',' '_')"; mkdir -p "$d"
    echo "DEEP: === $seq (dwell ${DWELL}s per core) ==="
    ZELDA3D_SEQ_LOGDIR="$d/seq" \
    ZELDA3D_LAUNCHER_BIN="$ASAN_BIN" \
    ZELDA3D_SEQ_BOOT_WAIT="${ZELDA3D_SEQ_BOOT_WAIT:-900}" \
    ZELDA3D_SEQ_SCENE_WAIT="${ZELDA3D_SEQ_SCENE_WAIT:-400}" \
    ZELDA3D_SEQ_DWELL="$DWELL" \
    ZELDA3D_SEQ_CMDS="${ZELDA3D_DEEP_CMDS:-}" \
    ZELDA3D_SEQ_CMDS_OOT="${ZELDA3D_DEEP_CMDS:-$OOT_TOUR}" \
    ZELDA3D_SEQ_CMDS_MM="${ZELDA3D_DEEP_CMDS:-$MM_TOUR}" \
    ZELDA3D_SEQ_CMD_WAIT="${ZELDA3D_SEQ_CMD_WAIT:-900}" \
    ASAN_OPTIONS="detect_leaks=0:halt_on_error=0:detect_odr_violation=0:log_path=$d/asan" \
        "$REPO/tools/zelda3d_sequence.sh" "$seq" > "$d/seq.out" 2>&1 &
    SEQ_CHILD=$!
    wait "$SEQ_CHILD"
    rc=$?
    SEQ_CHILD=""
    reports=$(ls "$d" 2>/dev/null | grep -c '^asan\.' || true)
    # The game-side log lives with its sequence, not at a shared path the next run deletes. Report
    # its size: an empty or missing run.log means the diagnostics for anything below are ABSENT,
    # not that nothing was diagnosed.
    rl="$d/seq/run.log"
    if [ -s "$rl" ]; then
        echo "DEEP:   run.log $(wc -c < "$rl") bytes at $rl"
        grep -aoE "\[[0-9]{4}\] .{0,160}" "$rl" 2>/dev/null | sort -u | head -20 | sed 's/^/DEEP:   /'
    else
        echo "DEEP:   NO run.log at $rl -- every game-side diagnostic for this sequence is MISSING."
    fi
    grep -E "RETURNED|NEVER REACHED|cmd " "$d/seq.out" | sed 's/^/DEEP:   /'
    echo "DEEP:   sequence exit $rc, ASAN reports: $reports  (log: $d/seq.out)"
    [ "$rc" -ne 0 ] && fail=1
    [ "$reports" -ne 0 ] && { fail=1; head -20 "$d"/asan.* | sed 's/^/DEEP:   /'; }
done

echo
if [ "$fail" -eq 0 ]; then
    echo "=== DEEP VERDICT (exit 0) === every sequence ran clean under the sanitizer with ${DWELL}s dwell."
    echo "    COVERED: each core held ${DWELL}s in-game, so this DOES say something past the first frame."
    echo "    NOT covered: leaks. detect_leaks=0 here -- and turning it on would not help much, because"
    echo "    LSAN only reports UNREACHABLE memory and this project's leaks stay reachable from their"
    echo "    X::Instance globals (validated both ways, docs/info/instruments/035)."
    echo "    Each core is also driven through a warp TOUR (echoed above): two Kokiri Forest spawns and"
    echo "    Zora's River for OoT, Termina Field and Great Bay Coast for MM. Scene load/teardown is where"
    echo "    this project's bugs have been, and dwell alone never left the spawn point."
    echo "    A randomizer seed IS generated per core now (REPL randogen, echoed above with its hash), so"
    echo "    the rando ownership paths are exercised -- but only through a COMPLETED generation, because"
    echo "    the command blocks. For the IN-FLIGHT case (a generation still running as the game ends,"
    echo "    which is a different bug and was a real one) run:"
    echo "        ZELDA3D_DEEP_CMDS=\"randogen async\" tools/zelda3d_deep_check.sh"
    echo "    MM answers randogen with unknown-command, which is correct and not a failure."
    echo "    Dwell sits wherever the core spawns -- it is time in-game, not coverage of the game."
else
    echo "=== DEEP VERDICT (exit 1) === see the lines above."
fi
exit "$fail"
