# Title-cs cursor PHASE SYNC — root cause was a 2x RATE bug, not a fixed offset (2026-07-09)

Closes handoff item (f) (`debug_journal/HANDOFF-2026-07-09-title-logo-phase.md` §4) and the
open question left by `2026-07-08-title-daytime-schedule-re.md` ("the cursor phase/rate law
... still needs to be pinned"). Also lands Task 2 (op-0x7c screen-level loop fade consumer).

## Root cause: SoH's ported title-cs cursor ran at TWICE the oracle's real rate

Prior sessions observed the `tools/title_ab.py` content-matched anchors
`(az=200 -> soh=397)`, `(az=360 -> soh=449)` and treated the shrinking gap (Δ=197, then
Δ=89) as evidence of *some* boot-time lag that "collapses nonlinearly" — but never pinned
why. It's not a lag at all: the two engines' cs cursors tick at **different rates**, so any
two-point "offset" measurement necessarily looks like it's drifting.

Measured directly (not SSIM-inferred) via the harness, using ground truth already trusted
in this project (the byte-exact-verified OP97 camera spline, `tools/oot3d_cs_camera.py`):

1. **Az's own cs cursor rate**: read Az's live title-camera eye (fixed VA `0x005BE6D4`, the
   existing `compare firstdiv`/"title-cam" probe) at az_step checkpoints 0..600 (`run <n>`
   calls from `scratch/title_settled.state`), then INVERTED each eye position against the
   ported camera-spline table (`oot3d_cs_camera.py` walking `spot99_info.zsi`'s OP97 block at
   payload offset `0x3980`) to find the exact cs frame that produces it. Residual <0.1 world
   units at every checkpoint (float noise) — this is an exact, not approximate, measurement.
   Result: **az_cs(az_step) = 88 + 0.5 * az_step** (clean fit, R≈1). The `0.5` slope is exact
   across the whole sampled range.
2. **SoH's own cs cursor rate (pre-fix)**: read `Zelda3D_TitleCsFrame()` via the harness's
   `soh_titlecs` command at soh_step checkpoints 0..800. Result (pre-fix):
   **soh_cs(soh_step) = soh_step - 232** for soh_step ≥ 232 (slope exactly 1.0, boot-splash
   offset 232 constant across every checkpoint tested).

So pre-fix, SoH's title-cs cursor advanced **1 cs-frame per engine tick**, while the real
3DS/oracle cutscene advances **0.5 cs-frames per Az retro_run() tick** — SoH was running the
demo at exactly 2x real speed. This is precisely why a two-anchor "offset" table looked like
a shrinking lag: two points on two lines with different slopes always look that way over a
short sampled window; it was never a one-time startup delay.

(Side note, doesn't change the port but worth recording: `oot3d-decomp/docs/
title_gamestate_driver.md` §3's table assumed the whole 2400-frame loop takes 40s "@ 60fps".
Given the confirmed 0.5 cs-frame/tick rate under Az's 60Hz-clocked `retro_run()`, the loop is
actually ~80s of real 3DS time, not 40s. Filed as a correction in that doc, see below.)

## Fix

`Shipwright/soh/src/zelda3d/zelda3d_cutscene.cpp`: `Zelda3D_TitleCsAdvance()` now only
actually increments `sFrame` on every OTHER call (`sTickParity` toggle), halving the
effective advance rate to match the oracle's real cadence. No magic constant was fit to any
anchor — the `0.5` comes directly from the RE'd rate law above, verified independently on
both engines.

## Verification (exact, not SSIM-approximate)

Post-fix, `soh_titlecs` at soh_step checkpoints confirms the new rate exactly:
**soh_cs(soh_step) = 0.5 * (soh_step - 232)** (slope 0.5 confirmed to the frame at every
checkpoint 250..800; boot-splash offset unchanged at 232, as expected — that offset is about
when TitlePresentation first activates, untouched by the advance-rate fix).

Combining both formulas analytically predicts a content match at
**soh_step = az_step + 408** (a single affine relationship — no more shrinking gap). Verified
this is EXACT, not approximate, using the harness's live camera-eye compare
(`compare firstdiv`'s "title-cam" line, |Δeye| in world units) at three fresh az/soh pairs,
each from a clean boot (no cumulative-step bugs):

| az_step | soh_step (az+408) | \|Δeye\| | \|Δdir\| | \|Δup\| |
|---|---|---|---|---|
| 200 | 608  | **0.00** | 0.0002 | 0.0001 |
| 360 | 768  | **0.00** | 0.0001 | 0.0001 |
| 600 | 1008 | **0.00** | 0.0001 | 0.0001 |

Byte-exact camera match at every sampled point — the two cursors are now genuinely in phase,
not just "closer". (The `tools/title_ab.py` SSIM content-search score also rose from the old
baseline 0.23–0.37 to ~0.80–0.82 at these frames, but that metric plateaus in this low-motion
early-night content — the eye-position match above is the authoritative check, not the SSIM
number.)

`tools/title_ab.py` updated: the old two-point eyeballed `ANCHORS` table (which actively
encoded the wrong, rate-confused relationship) is replaced with the single derived constant
`SOH_STEP_INTERCEPT = 408` and a comment explaining the rate-law derivation, so future
`calibrate` runs seed from the correct model instead of re-fitting a stale, now-falsified
piecewise curve.

## Task 2 — op-0x7c screen-level loop fade now consumed

`Zelda3D_TitleCsScreenFade()` (parsed, landed 64ba86f0) exposed the `[2310,2460)` fade
window but nothing drove the engine's fade with it. Added
`Zelda3D::TitlePresentation::applyScreenFade()` (`title_presentation.cpp`/`.h`), called once
per active frame from `update()`. It drives `play->transitionFade.fadeColor` — the engine's
existing full-screen fade overlay, already unconditionally drawn every frame by
`Play_Draw`'s `TransitionFade_Draw` call after every other draw pass (exactly the compositing
order a screen-level fade needs) — with a triangular ramp: alpha 0→255 over cs frames
`[2310, 2400)` (fading TO black as the loop point approaches), then 255→0 over `[2400, 2460)`
(fading back in after the restart). Because `Zelda3D_TitleCsFrame()` wraps at `2400` (never
reports 2400..2459), the post-wrap tail of the window is read back as low post-wrap frame
values and re-mapped onto the window's absolute timeline (`absFrame = csFrame + loopFrame`
when `csFrame < end - loopFrame`). Color assumed black (op-0x7c's raw record doesn't carry an
independent color/curve payload beyond `[start,end)` — matches every other OoT screen fade in
this engine, including `TransitionFade`'s own default). `exit()` clears the alpha so the
shared `transitionFade` struct can't leak a stale fade into real gameplay transitions once
title hands off.

### Verification — live game, quantified

`ZELDA3D_HEADLESS=1 ZELDA3D_WARP= tools/zelda3d_game.sh start`, then `titlecs <n>` + `shot`
at 14 points spanning the window, mean-luminance per frame:

| cs frame | mean L | | cs frame | mean L |
|---|---|---|---|---|
| 2200 | 26.5 | | 2415 | 31.4 |
| 2280 | 25.2 | | 2430 | 59.2 |
| 2310 | 24.7 | | 2450 | 96.1 |
| 2340 | 14.0 | | 2460 | 111.5 |
| 2360 | 6.7  | | 2470 | 111.5 |
| 2380 | **1.4** | | 2500 | 112.0 |
| 2400 | 3.4  | | 2550 | 112.4 |

Brightness dips to near-black (mean luminance 1.4) approaching the loop point and fully
recovers to the post-loop scene's own brightness level (~112) by frame 2460-2470 — the
expected fade-to-black-and-back shape. `scratch/screenshots/fade_2380.png` is fully black;
`fade_2340.png` shows the fade partway through (tunnel scene dimming); `fade_2450.png` shows
full post-loop content restored (night sky + moon). All gitignored, not committed (per
project screenshot policy).

## What this does NOT touch / ruled out

- The dayTime FORMULA (`Zelda3D_TitleCsTimeOfDay`) — untouched, already verified correct
  (`2026-07-08-title-daytime-schedule-re.md`). This fix only changes when a given cs-frame
  NUMBER occurs in wall-clock terms, not what any cs-frame-indexed function returns.
- `title_logo.cpp`'s fade-in/fade-out alpha rate (STOPGAP, still N64-derived) — separate,
  already-flagged gap (§4 open item in the handoff doc), not addressed here.
- No magic constant was fit to any single anchor point — `0.5` and the boot-splash `232` were
  each independently measured from live cursor/camera state on both engines, not chosen to
  make one screenshot line up.

## Files touched

- `Shipwright/soh/src/zelda3d/zelda3d_cutscene.cpp` — `sTickParity` + half-rate
  `Zelda3D_TitleCsAdvance()`.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.{h,cpp}` —
  `applyScreenFade()`, called from `update()`; `exit()` clears the fade alpha.
- `tools/title_ab.py` — `ANCHORS`/`estimate_soh_frame` replaced with the derived
  `SOH_STEP_INTERCEPT = 408` affine seed; `list-anchors` and the splash-zone warning updated
  to match.
- `<oot3d-decomp>/docs/title_gamestate_driver.md` — correction note on the loop's real
  wall-clock duration (80s @ 30fps-effective cs advance under a 60Hz vblank clock, not 40s
  @ 60fps as originally assumed).
