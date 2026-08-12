# 0022 — `Camera_BGCheckInfo` SIGSEGVs a few seconds after warping into Kokiri Forest spawn 2

status: CLOSED 2026-08-12 — root-caused and fixed; verified by a full clean deep check (see "Verification")
found by: `tools/zelda3d_deep_check.sh` after issue 0021 made the warp tour land where it was aimed
severity: crashed the core and made the deep check RED; fixed 2026-08-12, deep check oot,oot now exits 0

## What happens

Sanitizer build, `oot,oot`, tour = `randogen; warp 0xEE; …; warp 0x209; …`. The warp to `0x209`
(`ENTR_KOKIRI_FOREST_OUTSIDE_DEKU_TREE`, Kokiri Forest spawn 2) succeeds — the previous warp's
`posinfo` confirms Kokiri Forest — and the core dies during the following settle:

    Camera_BGCheckInfo
    func_80045508
    func_80046E20
    Camera_Normal1
    Camera_Update
    Play_Update

`RAX = 0` on the faulting instruction. Signal 11.

## What is and is not known

- **Not the warp itself.** `warp 0x209` completes and the scene loads; the crash is a few seconds
  later, from the camera's per-frame background check.
- **Not reproduced on the release build yet.** A release run of `warp 0xEE → 0x209 → 0x109` with 15 s
  settles survived all three. The sanitizer run sits in the scene far longer (30 s settle + 30 s
  dwell), so "release is fine" is NOT established — it may simply never have run that long there.
  Do not report this as sanitizer-only without running release with the same dwell.
- **No AddressSanitizer report exists for it**, which is a second problem: our own crash handler
  installs a SIGSEGV handler and `_exit`s, so on a sanitizer build it pre-empts ASAN's handler and
  the precise report (what address, what allocation, what shadow state) is lost. The
  async-signal-safe backtrace above is all there is. That is worth fixing before this is chased --
  the report would probably name the cause outright.

## Progress 2026-08-12

**The crash handler no longer pre-empts the sanitizer.** `CrashHandler`'s constructor now skips
SIGILL/SIGABRT/SIGFPE/SIGSEGV when the translation unit was built with AddressSanitizer
(`__SANITIZE_ADDRESS__` / `__has_feature(address_sanitizer)`), so a fault becomes an ASAN report
instead of a symbol-only backtrace and an `_exit`. It says so on stderr at startup, because a
sanitizer run with no crash-handler output would otherwise look like the handler failing. Release
builds are unchanged — this is a compile-time branch on a build that exists to be diagnosed, not a
runtime toggle. **Not yet observed doing its job**: no fault has occurred since, so "ASAN now reports
it" is a design claim, not a measurement, until 0022 next fires.

**It is INTERMITTENT.** Two targeted reproductions failed to trigger it on the sanitizer build:

    sleep:30; warp 0x209; sleep:60; posinfo                                    -> survived
    sleep:30; randogen; warp 0xEE; sleep:30; posinfo; warp 0x209; sleep:45     -> survived

The second is the deep check's own tour up to and past the point it died, on the same build, with the
same ASAN options. So the trigger is not simply "reach Kokiri Forest spawn 2 and wait" — either it
needs something later in the tour (the `warp 0x109` step, or the dwell), or it is timing-dependent.
Anyone chasing this should NOT conclude from a single clean run that it is fixed.

## It fired again, and this time the report survived (2026-08-12)

The next full `tools/zelda3d_deep_check.sh` run reproduced it, and because the crash handler now
stands aside on a sanitizer build, AddressSanitizer produced the report the previous occurrence could
not. That closes the design claim above: the change works, observed rather than argued.

    AddressSanitizer: SEGV on unknown address 0x000000000008 ... READ
    Hint: address points to the zero page.
      #0 Camera_BGCheckInfo   z_camera.c:312:54
      #1 func_80045508        z_camera.c:889:17
      #2 func_80046E20        z_camera.c:1388:15
      #3 Camera_Normal1       z_camera.c:1813:9
      #4 Camera_Update        z_camera.c:7667:9

Copy kept at `scratch/logs/issue0022_report/asan.890427`.

**Address 8 is `to->poly->normal.x` with `to->poly == NULL`** — line 312 is the first of three
`COLPOLY_GET_NORMAL(to->poly->normal.*)` reads.

## The mechanism, as far as the code shows

`Camera_BGCheckInfo` reaches line 312 by one of two routes, and only one of them can leave `poly`
null:

1. `BgCheck_CameraLineTest1` returned true — it set `to->poly` to the poly it hit. Not null.
2. It returned false, and the block above ran: `BgCheck_CameraRaycastFloor2` fills `floorPoly`, and
   `to->poly = floorPoly` at line 307.

Route 2 is guarded. `BgCheck_RaycastFloorImpl` sets `*outPoly = NULL` and returns `BGCHECK_Y_MIN`
(-32000) when it finds no floor, and line 298 then takes `(to->pos.y - floorPolyY) > 5.0f` and
returns 0 before ever dereferencing. So for a NULL to reach line 312, **that comparison has to be
false while `floorPoly` is NULL** — which needs `to->pos.y` to be either ≤ about -31995, or **NaN**.

NaN is the strong candidate, because a NaN makes every `>` comparison false: the guard does not just
fail to fire, it fails *open*. `to->pos` comes from camera math a few frames after a scene load
(`OLib_Vec3fDiffToVecSphGeo` / `Camera_Vec3fVecSphGeoAdd`), and a degenerate input — camera eye and
target at the same point, which is exactly the state a freshly-loaded scene can present for a frame —
is the usual way a normalize produces one.

**Measured 2026-08-12 — and the NaN guess was wrong in the specific.** The instrumented run fired on
its first attempt:

    [0022] Camera_BGCheckInfo: no floor poly and the guard FAILED OPEN (1) --
      to->pos=(inf,-inf,-inf) floorPolyY=-32000.000000  y-isnan=0
      from=(3944.399414,-35.141968,-1119.740845)

Not a NaN — an **infinity**. `to->pos.y` is `-inf`, so `(-inf) - (-32000)` is `-inf`, and
`-inf > 5.0f` is false. The guard fails open for the same *reason* the NaN theory predicted (a
non-finite value makes the comparison false) but the value is different, and `y-isnan=0` says so
outright. Recording that because "NaN" was written down as the candidate and acting on it without
this run would have sent the fix at the wrong arithmetic.

`from` is `camera->at` and is finite and sane (Kokiri Forest). **`to->pos` is `camera->eyeNext`** —
`func_80045508` assigns `eyeChk->pos = camera->eyeNext` (z_camera.c:916) and passes it straight to
`Camera_BGCheckInfo` (:920). So the camera's own state had already gone non-finite before collision
was ever consulted: **`Camera_BGCheckInfo` is the victim, not the cause**, and a null check at line
312 would hide an infinite camera rather than fix it.

Note this is authentic decomp code — N64 would deref the same NULL. So whatever produces the
infinite `camera->eyeNext` is the bug.

## Now hunting the producer

A second probe bracketed the camera mode function inside `Camera_Update` and **printed nothing**,
which is itself the finding rather than a dud. Everything was finite on entry every frame, including
the crashing one — because the corruption and the crash are inside the SAME `Camera_Normal1` call
(`Camera_Vec3fVecSphGeoAdd(eyeNext, …)` at z_camera.c:1837, then `func_80046E20` →
`func_80045508` → `Camera_BGCheckInfo` seven lines below), so the "after" sample is never reached.
**"It arrived infinite from an earlier frame" is ruled out.** Kept the probe: it is cheap and it now
carries that negative result.

A third probe sits between the write and the use, on line 1837's result: if `eyeNext` comes out
non-finite it logs `at`, the whole `eyeAdjustment` (r/pitch/yaw), `camera->dist`, `distMin`,
`distMax`, `atEyeNextGeo.r` and `yawUpdateRateInv`. `at` is finite in the failing run, so the
infinity has to arrive through `eyeAdjustment.r` — which is `camera->dist`, assigned four lines
earlier straight out of `Camera_ClampDist`. That probe names the input.

**Reproduction note:** targeted single-core runs keep surviving (four attempts now); only the full
`tools/zelda3d_deep_check.sh` has reproduced it, twice out of two attempts that got far enough. Drive
it with the deep check, not with a hand-built sequence.

## Still intermittent

Two targeted repros (above) did not trigger it; the full deep check did, twice out of two attempts
that got that far. Whatever the trigger is, it is not "reach Kokiri Forest spawn 2 and wait".


## Root cause — the camera register table is run-scoped state behind a process-scoped flag

`OREG(r)` is not a static array. It expands to `gGameInfo->data[2 * REG_PER_GROUP + r]`, and
`func_800636C0` (z_debug.c) mallocs a fresh `gGameInfo` **and explicitly zeroes every entry of
`data[]`** on each run. The table that fills it lives in `Camera_Init`:

    if (sInitRegs) {                       // z_camera_data.inc: s32 sInitRegs = 1;
        for (i = 0; i < sOREGInitCnt; i++) OREG(i) = sOREGInit[i];
        for (i = 0; i < sCamDataRegsInitCount; i++) R_CAM_DATA(i) = sCamDataRegsInit[i];
        ...
        sInitRegs = false;
    }

`sInitRegs` is a plain static, which on a console is correct — one game per boot, so once-per-process
and once-per-run are the same thing. In this launcher they are not. On the SECOND core run the static
is already `false`, the fill is skipped, and the freshly-zeroed register table stays zero.

That is not a cosmetic default. `OREG(6)` is the seed AND the LERP target for `camera->rUpdateRateInv`,
and both `Camera_ClampDist` and its sibling end with:

    return Camera_LERPCeilF(distTarget, camera->dist, 1.0f / camera->rUpdateRateInv, 0.0f);

With the register at 0, `rUpdateRateInv` LERPs to 0 (`|diff| < minDiff`, so it returns the target
unchanged), the step scale becomes `1.0f / 0.0f` = +inf, and `camera->dist` goes infinite. From there:
`eyeAdjustment.r = camera->dist` -> `Camera_Vec3fVecSphGeoAdd` writes a non-finite `eyeNext` ->
`Camera_BGCheckInfo` raycasts from an infinite point -> `BgCheck_CameraLineTest1` finds no floor ->
the NULL `floorPoly` is dereferenced at z_camera.c:343.

Every measured value falls out of this and none had to be assumed:

    [0022] Camera_Normal1 wrote a NON-FINITE eyeNext (1): eyeNext=(inf,-inf,-inf)
           at=(3944.399414,-35.141968,-1119.740845) eyeAdjustment=(r=-inf pitch=5640 yaw=-3278)
           camera->dist=-inf distMin=136.000000 distMax=204.000000 atEyeNextGeo.r=627.684753
           yawUpdateRateInv=0.000000 scene=85

`at` finite and `atEyeNextGeo.r` finite rule out an inherited-from-an-earlier-frame infinity;
`camera->dist=-inf` names the carrier; and `yawUpdateRateInv=0.000000` is the same disease in a
neighbouring field (it LERPs toward its own now-zero OREG), which is what turned "dist is wrong" into
"the whole register table is zero".

This also explains the intermittency that four targeted single-core repros failed to reproduce: a
single run always initialises the table, so the bug is **structurally impossible on run 1** and only
appears from run 2 onward.

## Fix

`Camera_Init` now gates the fill on a run-epoch latch instead:

    static Zelda3DOnce sCameraRegsInit;
    ...
    if (Zelda3D_Once(&sCameraRegsInit)) { ...fill OREG / R_CAM_DATA, DbCamera_Reset, PREG(88) = -1... }

`sInitRegs` is DELETED rather than left beside the latch — including its `extern` and its
save/restore in `savestates.cpp`, where the field no longer tracks anything (by the time a savestate
can be taken the table is filled, and a re-fill on the next `Camera_Init` is identical).

Verification is a POSITIVE line, not the absence of a crash: the latch logs
`[camera] register table initialised for this run: OREG(6)=<n>` once per run. Before the fix a second
core printed nothing there. "No crash" alone could not distinguish fixed from not-run-that-long.

## Tooling defects this exposed

1. `zelda3d_sequence.sh` wrote its game log to a FIXED path and `rm -f`'d it at startup, so the log
   holding the probe output for a captured crash was destroyed by the next run. The ASAN report was
   preserved per-sequence; the log that explained it was not. Now `ZELDA3D_SEQ_LOGDIR`, set
   per-sequence by the deep check, which also prints the log's size (or says the log is MISSING).
2. The sequence verdict reported "yes -- every core answered posinfo with a real scene" for a run in
   which ZERO cores started: `RAN_FAIL` can only be set inside the per-core loop, so a launcher that
   dies before the loop leaves it 0 and the check is vacuously true over an empty set. It now counts
   cores that reached a scene and compares against the number requested.


## Verification (full deep check, 2026-08-12)

All three sequences, sanitizer build, 60 s dwell plus the warp tour:

| sequence     | exit | ASAN reports | run.log |
| ---          | ---  | ---          | ---     |
| `oot,oot`    | 0    | 0            | 4.3 MB  |
| `mm,mm`      | 0    | 0            | 21 MB   |
| `mm,oot,mm`  | 0    | 0            | 25 MB   |

`DEEP VERDICT (exit 0)`.

The evidence is the POSITIVE line, not the missing crash. `[camera] register table initialised for
this run` appears **2** times in `oot,oot`, **1** in `mm,oot,mm`, **0** in `mm,mm` — i.e. exactly once
per **OoT** core and never for MM, which is correct because MM does not go through OoT's
`Camera_Init`. That the count tracks OoT cores rather than runs is a check on the instrument itself:
a latch that fired once per process, or once per run regardless of game, would have produced
different numbers here.
