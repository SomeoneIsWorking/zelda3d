# 2026-07-14 — Embedded-harness title-cs frame sync (default, TitleSyncController)

## Task

Make the embedded-Azahar SBS harness (`tools/soh3d_harness`) content-locked at
the title screen by DEFAULT: the oracle (Azahar/OoT3D) holds at the settled
title save-state while SoH3D boots completely cold through the title cs, then
the two engines track the SAME title-cs instant frame-for-frame, including
across the title demo's ~2400-frame loop restart.

## State-load mechanism reused

`LoadStateFileInternal()` / `Core::System::GetInstance().LoadStateBuffer()` —
the exact same call `HandleLoadState` (the `loadstate` REPL command) already
used, factored out so the new auto-arm path can reuse it without setting
`g_manual_state_touch` (that flag means "something ELSE manually touched
state"). Likewise `SohBootInternal()` factors `HandleSohBoot`'s body. No new
load-state protocol was invented.

## Loop-period empirical finding — 1:1 raw-frame stepping does NOT hold

The task brief assumed a simple affine law (`soh_step ~= az_step + 408`, from
`tools/title_ab.py`'s `SOH_STEP_INTERCEPT`) might extend to "step both
engines 1:1 forever, loop period matches for free." This was tested directly
and falsified:

`scratch/loop_period_check.py` ran the oracle ALONE (no SoH involved) via
plain `run <N>` REPL calls, in ONE continuous harness process, from
`title_settled.state`:

- az=200 -> moonlit sky + rider silhouette scene (see
  `scratch/title_ab/loopchk_200.az.png`)
- az=200+4800=5000 -> **solid black frame** (`scratch/title_ab/loopchk_5000.az.png`,
  a 369-byte PNG)

If "N `run` calls" mapped onto a fixed number of internal cs-ticks, these two
points (one full assumed loop period apart, both derived from the SAME
0.5 cs-tick/frame rate law documented for both engines) should show identical
content. They do not. This confirms, empirically, the caveat already
documented on the existing `az_run_until` REPL command: *"each retro_run
advances a variable slice depending on host wall-clock scheduling"* —
real host-scheduling jitter accumulates into visible content drift over a
full ~4800-raw-frame span, even within one continuous process. `title_ab.py`
itself only ever trusted its affine law as a **search seed** for a fine
content-search over a short window (<2000 frames) — never as an exact law
over a full loop. **Conclusion: NO, the two engines' loops do not stay
aligned under naive 1:1 counting — a resync path is required and was
implemented.**

## Sync architecture — `tools/soh3d_harness/title_sync.h` / `.cpp`

`TitleSyncController` (states `UNARMED -> HOLD -> LOCKED`, or `DISABLED` for
legacy passthrough):

- **Arm** (`ArmTitleSync()` in `main.cpp`, called once from the harness
  REPL's `step` command's first invocation in a fresh process): if
  `scratch/title_settled.state` is missing, auto-generates it by shelling out
  to `tools/title_settle.py`; loads it into the oracle + renders exactly one
  frame (`ReloadOracleToBaseline()` — a bare `LoadStateBuffer()` never
  triggers a render, so without this the oracle pane would show a stale/blank
  frame during HOLD); boots SoH3D cold (`SohBootInternal()`). If EITHER step
  fails, `step` refuses to run at all with a clear stderr diagnostic — it
  never silently falls back to a cold-booted oracle.
- **HOLD**: the oracle sits at this one rendered frame (no further
  `retro_run()`) while SoH3D's raw engine-frame counter (`sohFrameCount_`,
  one tick per `RunFrame()` call since `soh_boot`) climbs from 0. `408`
  (`tools/title_ab.py`'s `SOH_STEP_INTERCEPT`) is reused only as a **floor**
  — "wait long enough that SoH's title cs is genuinely live" — never as an
  assumed-exact offset (see the falsified-law finding above).
- **HOLD -> LOCKED**: once `sohFrameCount_` crosses the floor,
  `CalibrateAndLock()` runs a **native content search** — a C++ port of
  `tools/title_ab.py`'s `content_score`/`load_gray_small` (48x28 grayscale
  downsample, ITU-R 601 luma, zero-mean + unit-norm each side independently,
  dot product) operating directly on the in-memory framebuffers (no PNG
  round-trip) — sweeping the oracle forward `[0, 400]` az-steps from the
  loaded baseline and scoring each candidate against SoH's CURRENT (held
  fixed) frame. Since stepping is forward-only, the search overshoots to
  the margin; the oracle is then reloaded to baseline and replayed EXACTLY
  the best-scoring step count so it lands precisely there (no residual
  overshoot) before switching to LOCKED.
- **LOCKED**: one `retro_run()` per SoH `RunFrame()`, 1:1, while also
  watching SoH's authoritative `Zelda3D_TitleCsFrame()` (0..2399,
  decomp-derived, the same counter `title_presentation.cpp` wraps against)
  for a loop wrap (a drop of >=1500 between consecutive iterations). On
  wrap: reload+re-render the oracle to baseline and re-run
  `CalibrateAndLock()` — **the resync path required by the empirical
  finding above.**
- Legacy/no-op path: if a manual `loadstate`/`soh_boot` already ran before
  the first `step` call (e.g. a script driving its OWN scene), title-sync
  auto-arm is skipped entirely (`DISABLED`) and `step` behaves exactly like
  the old unconditional lockstep passthrough. `title_ab.py`,
  `oracle_cache.py`, `title_daytime_scan.py` etc. never call the harness's
  `step` command at all (confirmed by grep) — they use `run`/`soh_step`
  with their own explicit `loadstate`+`soh_boot`, so they are completely
  unaffected by this change; no env-var opt-out was needed or added.

## Verification (real headless runs, `SOH3D_HARNESS_HEADLESS=1`)

Driver: `scratch/titlesync_verify2.py` (throwaway, not committed), spawns
the harness with **no** explicit `loadstate`/`soh_boot` — exactly what a
fresh `tools/soh3d_harness.sh` process looks like — and drives it purely via
`step <N>` + the new `titlesync` diagnostic REPL command.

**2a — launch composition.** Immediately after the first `step 1` (which
auto-arms): `titlesync` reports `state=HOLD sohFrame=1`. Snapshot
(`scratch/title_ab/v2_00_arm.{az,soh}.png`) shows the oracle already holding
real (if dim — `title_settled.state` happens to land on a near-black
instant of the cs, mean pixel value 0.17/255, not a bug) rendered content,
while SoH's side is still fully black (frame 1 of cold boot, nothing drawn
yet) — confirms oracle-holds/SoH-boots-cold ordering.

**HOLD -> LOCKED**: reached at `sohFrame=421` (soh_step 421 > floor 408),
calibration #1 found best az_step=399, score=0.7898
(`scratch/logs/titlesync_verify2_harness.log`).

**2b — 5 matched instants across the loop** (`csFrame` 561 -> 1961, i.e.
spanning most of the 0..2399 range):

| csFrame | content_score |
|---------|---------------|
| 561     | 0.7631 |
| 911     | 0.7385 |
| 1261    | 0.7381 |
| 1611    | 0.8843 |
| 1961    | 0.7288 |

All in/above the project's established "verified good match" band (prior
sessions' `title_ab.py` verified pairs ranged ~0.43-0.75, see
`debug_journal/2026-07-10-title-arc-closing-measurement-v4.md`) — content-
correct lockstep, not just "both animating."

**2c — post-wrap resync.** At `csFrame=2361 -> 61` (drop 2300 >= threshold),
wrap detected, oracle reloaded, calibration #2 ran, `azFrame` reset to 0
(best match found immediately at the reloaded baseline). Settling 300 more
frames post-wrap: `content_score=0.8335`
(`scratch/title_ab/v2_postwrap_sxs.png` — moon, hill silhouette, and the
title-demo rider all match position closely). **Confirms the resync path
fires correctly and re-locks content across the loop restart.**

## Files

- `tools/soh3d_harness/title_sync.h` / `title_sync.cpp` (new) — controller.
- `tools/soh3d_harness/main.cpp` — `ArmTitleSync`, `ReloadOracleToBaseline`,
  `CalibrateAndLock`, `ContentScoreNative`/`DownsampleGrayAz`/`DownsampleGraySoh`,
  refactored `LoadStateFileInternal`/`SohBootInternal`, rewritten `HandleStep`,
  new `titlesync` REPL diagnostic command.
- `tools/soh3d_harness/harness.cmake` — added `title_sync.cpp` to the
  `soh3d_harness` target.
- `tools/soh3d_harness.sh` — header comment documents the new default.
- `tools/title_settle.py` — added to the repo (previously untracked,
  left over from a prior session); `ArmTitleSync()` now actually invokes it
  as the auto-generation path when `scratch/title_settled.state` is missing.

## Deviations from the task brief

- The brief's phrasing ("once SoH3D's title-cs frame counter reaches frame
  408") was interpreted as SoH's raw engine-tick count (matching
  `tools/title_ab.py`'s own `soh_step` convention), not
  `Zelda3D_TitleCsFrame()` itself (which advances at half that rate per
  `title_logo.cpp`'s "advances once every TWO real engine updates" comment)
  — using the literal `Zelda3D_TitleCsFrame()==408` reading would floor at
  the wrong real-time point entirely.
- The brief allowed either "no resync needed" or "implement resync" pending
  measurement. Measurement showed resync IS needed, so a native
  content-search resync (not a raw-frame-count one) was implemented — this
  is a bigger change than a pure 1:1-forever design would have been, but the
  data left no honest alternative.
- No `SOH3D_HARNESS_COLDBOOT_ORACLE` opt-out was added — no consumer of
  the harness's `step` command exists yet that would conflict with the new
  default (verified by grepping every `tools/*.py` for `step` sent to the
  harness's own REPL vs. the separate SoH3D-game REPL, `zelda3d_repl.py`).
