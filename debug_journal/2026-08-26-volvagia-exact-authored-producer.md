# Volvagia exact authored producer — 2026-08-26

The Boss_Fd history divergence had two independent causes hidden behind a stale
runtime link. First, `FUN_0036B96C` integrates with
`s16(global+0x110) * 0.5 = 1.0` per 30 Hz actor tick; measuring over 60 Hz
emulator frames had incorrectly suggested a half-rate scalar. Second, OoT3D
uses interpolated table trig and its own `FUN_003696EC` float atan2 polynomial,
not SoH's coarse N64 trig plus host `std::atan2`.

The harness dynamically loads `Shipwright/build-cmake/soh/libsoh_core.so`.
Rebuilding only `soh_lib` does not update runtime evidence; the stale shared
library reproduced the former drift and briefly made the exact-math port look
ineffective. The BossFd comparator now also prints oracle and authored XYZ
velocity, making that class of stale/shifted snapshot immediately visible.

After rebuilding the actual shared owner under Clang, the forced-profile run
was exact through 270 authored ticks: position, rotation, velocity, speed,
turn, move timer, and all sampled history deltas were zero. A +1000 corruption
of one oracle history X sample yielded `meanPos=50`, `maxPos=1000`, and
`DIVERGED`; restoring it returned exact `MATCH`. Evidence log:
`scratch/logs/bossfd_exact_math_drive.log` (scratch is intentionally untracked).

The verified scope is the forced action-0 body-history producer. General
action/death sequencing and paired rendered-image parity remain open.
