---
id: C084
kind: claim
status: holds
created: 2026-08-12
tags: camera,run-scoped,issue-0022
depends: Shipwright/soh/src/code/z_camera.c
reconfirmed: 2026-08-12
verified_at: 2026-08-12 17:14:10
---

## Claim

The intermittent oot,oot Camera_BGCheckInfo SIGSEGV is caused by the camera register table (OREG/R_CAM_DATA) being run-scoped state gated behind a process-scoped static, sInitRegs.

## Evidence

OREG(r) expands to gGameInfo->data[...], and func_800636C0 (z_debug.c) mallocs a fresh gGameInfo and explicitly zeroes all of data[] every run, while Camera_Init's fill was gated on 's32 sInitRegs = 1' which goes false after run 1 and is never restored. So run 2+ ran with an all-zero register table. OREG(6) is the seed and LERP target for camera->rUpdateRateInv, and Camera_ClampDist ends in 'Camera_LERPCeilF(distTarget, camera->dist, 1.0f / camera->rUpdateRateInv, 0.0f)' -- a zero register makes the step scale +inf and camera->dist infinite, eyeNext non-finite, the BG raycast return no floor poly, and z_camera.c:343 dereference NULL. Measured probe: 'eyeNext=(inf,-inf,-inf) at=(3944.4,-35.1,-1119.7) eyeAdjustment=(r=-inf ...) camera->dist=-inf distMin=136 distMax=204 atEyeNextGeo.r=627.68 yawUpdateRateInv=0.000000' -- at and atEyeNextGeo.r finite (rules out an inherited infinity), dist infinite (names the carrier), and yawUpdateRateInv=0 is the same disease in a neighbouring field. Fixed by gating on a Zelda3DOnce run-epoch latch; sInitRegs deleted including its savestate save/restore. VERIFIED: tools/zelda3d_deep_check.sh oot,oot now exits 0 with 0 ASAN reports, and the latch's positive line '[camera] register table initialised for this run: OREG(6)=..' appears TWICE (once per run) where run 2 previously printed nothing.

## What would falsify it

The verification is the POSITIVE per-run log line, not the absence of a crash -- absence alone cannot distinguish 'fixed' from 'the run did not get that far', which is exactly how four earlier targeted repros produced false negatives. This claim fails if that line ever appears fewer times than the number of cores in a sequence, or if OREG(6) is logged as 0.

## Re-confirmed 2026-08-12

FULL deep check 2026-08-12 (all three sequences, sanitizer build, 60s dwell + warp tour): oot,oot / mm,mm / mm,oot,mm each exit 0 with 0 ASAN reports; DEEP VERDICT exit 0. The positive latch line '[camera] register table initialised for this run' appears exactly once per OOT core and never for MM -- 2 in oot,oot, 1 in mm,oot,mm, 0 in mm,mm. That per-sequence count is itself a check on the instrument: it tracks the number of OoT cores rather than the number of runs, which is what it should measure since MM does not use OoT's Camera_Init.
