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

## Session 2 — content-search sync RETIRED; integer-cursor sync (design redirect)

User redirect: the content-search calibration (above) is a hack compensating
for two things that needed root-causing instead. Both were root-caused and
the sync mechanism was replaced wholesale.

### Falsification 1: "retro_run advances a variable slice" does NOT mean
### content nondeterminism

`scratch/az_determinism_check.py` ran the identical schedule in two fresh
harness processes from `scratch/title_settled.state` and compared, at
checkpoints 100/500/1000/2000/4500 retro_runs: the title-state vblank
counter, `CoreTiming().GetGlobalTicks()`, and the sha256 of the az
framebuffer:

```
run  frames  cs/vbl        ticks                    result
   100       185/185       3353908754/3353908754    SAME
   500       585/585       5146363077/5146363077    SAME
  1000      1085/1085      7386931077/7386931077    SAME
  2000      2085/2085     11868067109/11868067109   SAME
  4500      4585/4585     23070907077/23070907077   SAME
framebuffer sha256: 38ff366fe5923488 == 38ff366fe5923488
CONTENT DETERMINISTIC
```

Even the GLOBAL TICK COUNT is bit-identical across processes. The old
`az_ticks` caveat ("variable slice depending on host wall-clock
scheduling") described tick-boundary variance WITHIN a frame in some other
context; `retro_run()` itself loops `RunLoop()` until exactly one frame is
submitted (`EmuWindow_LibRetro::HasSubmittedFrame`, one per vblank), and
core scheduling is cycle-based — no wall-clock input to emulated content.
Session 1's loop_period_check "black frame at +4800" observation was a
wrong-loop-period artifact (the settled state sits BEFORE the cs loop
start, see below), not jitter. **1 retro_run == 1 deterministic emulated
frame.**

### Falsification 2 (recorded 2026-07-04, re-surfaced): 0x0054CC3C is the
### VBLANK counter, not the cs cursor

Session 1's content search existed because "the oracle has no readable cs
cursor". Wrong on two counts: (a) VA 0x0054CC3C is a deterministic +1/frame
vblank counter (usable as the LOCKED rate-model clock via the RE'd law
az_cs advances 0.5/frame — 2026-07-09-title-cs-phase-sync.md), and (b) the
oracle's ABSOLUTE cs frame is recoverable exactly by inverting its live
title-camera eye (RE'd basis @ 0x005BE6D4) against the byte-exact ported
OP97 spline (`Zelda3D_TitleCsCamera`) — the same inversion that derived the
2026-07-09 rate law (residual <0.1 world units).

New empirical finding while wiring this: after loading
`title_settled.state`, the camera basis VA holds an identity placeholder
(eye=(0,0,1), dir=(1,0,0)) for the first ~85 frames — the settled state
sits BEFORE the first camera-spline segment activates. The arm sequence
therefore advances the oracle +85 frames (discovered dynamically: run until
the basis publishes, cap 600) to the "anchor", where eye-inversion returned
**cs frame 1 with residual 0.0007 world units** (runner-up frame 7 at
0.439 — unambiguous).

### New mechanism (tools/soh3d_harness/title_sync.h/.cpp + main.cpp)

- **Arm**: load settled state → advance to camera-spline coverage (+85
  frames, discovered not hardcoded; `g_armAdvanceRuns`/`g_armAnchorEye`
  recorded) → HOLD.
- **Anchor**: lazily (once SoH's cs data is parsed) invert the held eye →
  `azLockCs` (=1 for the current settled state).
- **Lock**: first frame SoH's own cursor (`Zelda3D_TitleCsFrame()`) reaches
  `azLockCs` — integer equality, no search.
- **LOCKED**: per frame, model the oracle's cs as
  `azLockCs + (vbl - vblAtLock)/2` (vblank counter read from guest memory)
  and run a 0/1/2-step integer governor keeping `sohCs - modelAzCs == 0`.
  The governor absorbs the ±1 tick-parity offset (~1 correction per ~35
  frames observed); `maxAbsDelta` never exceeded 1 over a full loop.
- **Wrap**: when SoH's cursor drops ≥1500, reload settled state + replay
  exactly `g_armAdvanceRuns` frames (determinism-verified, eye re-checked
  against the recorded anchor) → re-HOLD → same integer lock. No content
  search anywhere. csFrames in [0, azLockCs) at each wrap are by-design
  unsynced (oracle holds at the anchor); with azLockCs=1 that window is
  2 engine frames.

`ContentScoreNative`/`DownsampleGray*`, `CalibrateAndLock`, the periodic
checkpoint recalibration, and `kSyncSohFrame`/`kCalibrateMargin`/
`kWrapDropThreshold(old use)` were all REMOVED from main.cpp.
content_score survives only as the Python VERIFICATION metric.

Session 1's residual worth keeping: the content search demonstrably
MISLOCKED (first lock chose az_step=399, score 0.79, on a near-black
frame; the cursor says the true offset was ~12) — pixel similarity on
low-signal frames is not a sync mechanism.

### Texture-pack poisoning (harness-only neutralization)

The embedded SoH half was picking up the user's Henriko 4K pack ("4K"
wordmark + different copyright), corrupting every compare. Mechanism: the
pack is NOT CVar-driven — `Shipwright/cmb3d/asset/texpack.cpp:findPackRoot()`
resolves `ZELDA3D_TEXPACK` env → cwd `textures/` → `textures/` next to
`ZELDA3D_OOT3D_ROM`. It already honors `ZELDA3D_TEXPACK=off|0|none` as an
explicit disable (added 2026-07-10 for the terrain-darkness A/B). Fix:
`SohBootInternal()` (tools/soh3d_harness/main.cpp) now
`setenv("ZELDA3D_TEXPACK", "off", 1)` before booting the embedded SoH —
harness-process-only; the user's config and `tools/zelda3d_game.sh` are
untouched. Verified visually: SoH half shows the vanilla "3D" wordmark and
"© 1998-2011 Nintendo / Codeveloped by GREZZO" (intsync2_03 SxS).

### Verification — tools/title_sbs_verify.py (durable, committed)

Drives the default arm→lock path, samples K=8 instants across one full
loop, writes SxS PNGs + content_score table (verification metric only).
`harness_ctl.py` gained `close()` (quit → SIGTERM → SIGKILL on the tracked
pid only — no pkill patterns). Run `intsync2` (fresh process, headless):

| target_cs | actual_cs | score  | governor delta | note |
|-----------|-----------|--------|----------------|------|
| 150       | 150       | 0.9482 | 0 | |
| 464       | 464       | 0.6912 | 0 | LOW — same camera segment both halves; SoH wordmark dimmer (fade alpha) |
| 779       | 779       | 0.8729 | 0 | |
| 1093      | 1093      | 0.6393 | 0 | LOW — same segment (rider at right edge on BOTH); SoH wordmark glow weaker + terrain saturation |
| 1407      | 1407      | 0.8765 | 0 | |
| 1721      | 1721      | 0.9160 | 0 | late-loop |
| 2036      | 2036      | 0.9863 | 0 | late-loop |
| 2350      | 2350      | 0.9959 | 0 | late-loop |

Every instant landed on the exact requested cs frame (actual==target),
governor delta 0 at every sample, maxAbsDelta=1 across the whole loop, 131
±1-parity corrections over 4700 frames. Late-loop (cs>1600) — where the
retired content-search sync decayed to 0.25 with visibly different camera
segments — now scores 0.92-1.00. The two sub-0.7 rows were individually
inspected (`scratch/title_ab/intsync2_01_cs464_sxs.png`, `_03_cs1093_`):
both show the SAME camera segment/framing on both halves; the score dips
are real content divergences, NOT sync drift.

### Candidate REAL content divergences (seen in the clean compare, NOT fixed — out of scope)

- Wordmark fade/alpha timing: SoH logo noticeably dimmer at cs~464 while
  the oracle's is fully opaque.
- Wordmark golden glow (fireglow) weaker on SoH at cs~1093.
- Terrain color saturation: oracle grass is warmer/more saturated in
  daylight segments (known lighting-divergence family).
- Slight wordmark placement/scale offset (SoH logo sits ~2-3% lower/right
  at cs464).
