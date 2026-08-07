#!/usr/bin/env bash
# #16 first-person early-load crash repro loop. Boots Zelda3D on the REAL GPU directly into a scene
# with ZELDA3D_FP_REPRO=1 (engine synthesizes C-up edges at the first controllable frame so the
# cold-load texture race overlaps first-person engagement), waits a few seconds, and scans the log
# for the SPECIFIC #16 crash signature (guMtxF2L <- Matrix_ToMtx) or our instrumentation markers
# (SOH3D MTX-NULL / SOH3D ARENA-CORRUPT). The teardown audio use-after-free from killing the
# process is NOT a match and is ignored. Repeats up to N times since the crash is a race.
#
#   tools/zelda3d_fp_repro_loop.sh [entrance] [attempts] [seconds_per_boot]
# Defaults: entrance 389 (Hyrule Field wooded exit), 20 attempts, 12s each.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$REPO/tools/zelda3d_proc.sh"
ZELDA3D_BIN="${ZELDA3D_SOH:-$REPO/Shipwright/build-cmake/zelda3d/zelda3d}"
ENTR="${1:-389}"
ATTEMPTS="${2:-20}"
SECS="${3:-12}"
mkdir -p "$REPO/scratch/logs/fp_repro"
RUN="$REPO/scratch/logs/fp_repro/run_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RUN"
echo "fp_repro_loop: entrance=$ENTR attempts=$ATTEMPTS secs=$SECS -> $RUN"

# The #16 crash: signal in guMtxF2L called from Matrix_ToMtx (sky/sun-moon billboard draw). Our
# guards print SOH3D MTX-NULL / SOH3D ARENA-CORRUPT one frame earlier. Match any of these.
SIG='SOH3D MTX-NULL|SOH3D ARENA-CORRUPT|guMtxF2L|Matrix_ToMtx'

for i in $(seq 1 "$ATTEMPTS"); do
    LOG="$RUN/attempt_$(printf '%02d' "$i").log"
    for pid in $(zelda3d_pids "$ZELDA3D_BIN" oot); do kill -9 "$pid" 2>/dev/null; done
    sleep 1
    ZELDA3D_FP_REPRO=1 timeout "$SECS" "$REPO/tools/zelda3d_gpu_launch.sh" "$ENTR" >"$LOG" 2>&1
    rc=$?
    for pid in $(zelda3d_pids "$ZELDA3D_BIN" oot); do kill -9 "$pid" 2>/dev/null; done
    sky=$(grep -c 'SKYBUG' "$LOG"); sky=${sky:-0}
    eng=$(grep -c 'FP_REPRO: first-person ENGAGED' "$LOG"); eng=${eng:-0}
    started=$(grep -c 'FP_REPRO: start injecting' "$LOG"); started=${started:-0}
    if grep -Eq "$SIG" "$LOG"; then
        echo "attempt $i: *** #16 CRASH SIGNATURE FOUND *** (skybug=$sky fpStart=$started fpEngaged=$eng rc=$rc) -> $LOG"
        echo "$LOG" > "$RUN/HIT"
        grep -nE "$SIG|Signal:|RDI:|RSI:|Scene:" "$LOG" | head -30
        exit 0
    fi
    echo "attempt $i: no #16 crash (skybug=$sky fpStart=$started fpEngaged=$eng rc=$rc)"
done
echo "fp_repro_loop: no #16 crash in $ATTEMPTS attempts. Logs in $RUN"
exit 1
