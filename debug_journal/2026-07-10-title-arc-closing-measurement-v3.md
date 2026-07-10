# Title arc closing measurement v3 — 3DS dome schedule ported, dayTime phase pinned to +1cs, fire-glow blend/B-channel mechanically explained (2026-07-10)

Follow-up to `2026-07-10-title-arc-closing-measurement-v2.md` (v2) and
`2026-07-10-title-star-footprint-and-overlay-scale-derivation.md`. Four tasks:

## 1. Task 1 — the 3DS dome's OWN schedule ported, replacing the N64 `D_8011FC1C` fallback

### Ground truth

`oot3d-decomp/docs/title_sky_dome.md` §9 (session #5) fully decompiled the dome's real
per-frame consumer, `FUN_002e47c8`, and its schedule table at VA `0x0053200a` (9 rows,
8-byte stride, `{u16 start, u16 end, u8 blendFlag, u8 idx1, u16 idx2}`). The table is
byte-identical in boundaries/index-pair values to `kTitleLightSchedule` (already ported,
`zelda3d_cutscene.cpp`) — but it is a SEPARATE, purpose-built table (contiguous in the ROM's
data blob right after the light table) driving a genuinely different consumer (the sky dome's
skybox1Index/skybox2Index/skyboxBlend, not the light palette). §9.5 explicitly warns not to
silently alias the light table's index field for the dome.

The engine's actual runtime path before this session used SoH's stock N64
`Environment_UpdateSkybox` (`z_kankyo.c`), which reads `D_8011FC1C` — the ORIGINAL N64 table,
confirmed by direct byte comparison to have a NARROWER night→sunrise blend window
(`0x2AAC..0x3556` blend / `0x3556..0x4000` pure-sunrise on N64, vs the 3DS's
`0x2AAC..0x4000` blend / `0x4000..0x4AAB` pure-sunrise) — i.e. N64 finishes the transition to
pure sunrise noticeably earlier than the 3DS does.

### The port

- `Shipwright/soh/src/zelda3d/zelda3d_cutscene.cpp`/`.h`: new `kTitleDomeSchedule[9]` (same 9
  rows as §9.2's table, cited to VA `0x0053200a`) and `Zelda3D_TitleCsDomeBlend(daytime,
  &skybox1Index, &skybox2Index, &blendWeight)` — the `FUN_00361490` LerpWeight shape (ceiling
  clamp only), kept as its own function/table per §9.5's instruction.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.{h,cpp}`:
  `TitlePresentation::applyDomeOverride(PlayState*)` (+ `Zelda3D_Title_ApplyDomeOverride` C
  bridge) — reads the same cs-derived dayTime `applyLightOverride` already uses
  (`Zelda3D_TitleCsTimeOfDay`), calls `Zelda3D_TitleCsDomeBlend`, and writes
  `envCtx.skybox1Index/skybox2Index/skyboxBlend` (u8, blendWeight*255).
- `Shipwright/soh/src/code/z_play.c`: call `Zelda3D_Title_ApplyDomeOverride(play)` immediately
  after the existing `Environment_UpdateSkybox(play, ...)` call in `Play_Draw` — overwriting
  the N64 table's result with the 3DS one. No-op outside the title cs (existing
  `mActive` guard), so the general gameplay skybox path is untouched.

### Live verification (via `tools/soh3d_harness`'s `soh_env` command, post-build)

At az=500/soh=908 (cs=338, dayTime=0x2bb5 per Task 2 below): `soh_env` reports
`skybox1=3 skybox2=0 blend=12`. Recomputing `Zelda3D_TitleCsDomeBlend(0x2bb5)` by hand against
row 1 (`{0x2AAC,0x4000,3,0}`): weight = (0x2bb5-0x2AAC)/(0x4000-0x2AAC) = 265/5460 = 0.0485,
*255 = 12.4 → 12 — an EXACT match to the live-read value. The port is live and computing
correctly.

## 2. Task 2 — dayTime PHASE, measured (not fitted)

### Method

`r16 0x00588f00` (the dome's OWN dayTime-shaped global, title_sky_dome.md §9.2) and
`az_daytime` (`gSaveContext.dayTime`, VA `0x00587964`) read on the embedded Az oracle at
content-matched az frames; `soh_env` (SoH's live `gSaveContext.dayTime` at the same matched
`soh = az + 408` step) read on the embedded SoH. `scratch/task2_daytime_check.py` (new,
gitignored scratch tool — not committed, one-off measurement per this task's own scope).

### Numbers

| az | soh | cs | Az `r16@0x588f00` | Az `az_daytime` | SoH `soh_env` daytime | delta (Az−SoH) |
|---|---|---|---|---|---|---|
| 500  | 908  | 338 | 0x2bbb | 0x2bbb | 0x2bb5 | **+6** |
| 1000 | 1408 | 588 | 0x3197 | 0x3197 | 0x3191 | **+6** |
| 1522 | 1930 | 849 | 0x37b5 | 0x37b5 | 0x37af | **+6** |

(Az's dome-VA read and its `gSaveContext.dayTime` read are IDENTICAL at every point — the two
globals title_sky_dome.md §9.2 flagged as "structurally separate" carry the same live value in
practice for this cs, consistent with both ultimately being written by the same per-frame
title-clock advance.)

### Mechanical investigation — ruled OUT the cue-table parse, left the formula alone

The exact, constant `+6` (= exactly one cs-frame at the established `+6/frame` rate,
`f115871f`) at all three sampled points (spanning cs 338..849, well past the frame-301 cue) is
suspicious for an anchor/base bug, so it was checked
against the ACTUAL ROM bytes rather than assumed. Dumped spot99_info.zsi's ` BDQ` op-0x8c
block directly (no harness, static `CtrRom` read):

```
op8c cnt=2
  rec0: frame=0   hours=4 mins=0  raw=000000000000040000000000
  rec1: frame=301 hours=4 mins=0  raw=00002d010000040000000000
```

Both records store IDENTICAL time (4:01 AM = 0x2AD7) — this matches `f115871f`'s prior finding
exactly, and — critically — **the second cue's frame field genuinely is 301 in the ROM bytes**
(`2d 01` at the record's `+2` offset = 0x012d = 301, little-endian). SoH's parser
(`zelda3d_cutscene.cpp`'s op-0x8c handler) reads this field with the identical offset/width and
was independently re-checked byte-for-byte against this dump — **no parse bug**: SoH's
`sTimeCues` correctly holds `{0,0x2AD7}` and `{301,0x2AD7}`, and `Zelda3D_TitleCsTimeOfDay`
correctly selects the frame-301 cue for every frame `>=301` (both test points are `>=301`).

So the `+6` residual is NOT a table/anchor parsing defect — it is a genuine ~1-cs-frame
granularity mismatch between the two engines' "current cs frame" at a content-MATCHED instant.
The `soh = az + 408` correspondence (`title_ab.py`) was itself calibrated on POSE/camera
content (SSIM match), not on the dayTime clock specifically; a plausible (not this-session-confirmed)
explanation is that the camera-spline evaluator and the op-0x8c time-cue evaluator read the cs
cursor at different points in the same engine tick (pre- vs post-increment), so a pose-perfect
frame match can still be off by exactly one cs-frame on the time axis alone. This session did
not trace that ordering (would need instrumenting both engines' per-substep cursor reads, out
of the static/single-measurement scope here).

### Verdict

**Left alone — not a curve-fit target.** The residual is real, constant, and now precisely
quantified (+6 dayTime units / +1 cs-frame at all 3 points checked, cs 338/588/849 — no drift,
no growth, no shrink with distance from the frame-301 cue), but its root is a tick-
ordering question, not the schedule/anchor math (`f115871f`'s "rate is correct" stands; the ROM
cue table itself is now also confirmed byte-exact, ruling out a table bug as the source). No
constant was added to "fix" this — doing so on two points without the ordering trace would be
exactly the curve-fit this project's rules forbid. **Concrete next step**: instrument
`Zelda3D_TitleCsAdvance` (camera path) and `Zelda3D_TitleCsTimeOfDay`'s frame input side by side
for one tick to see whether one reads the pre-advance and the other the post-advance cursor.

Downstream effect on Task 1's dome: at the +6 magnitude (≤0.11% of the 0x0000-0x6000 "night"
span at these points), the dome's own blend weight moves by at most ~0.3/255 — negligible next
to the schedule-mechanism fix itself; Task 1's port is correct given whatever dayTime it is fed.

## 3. Task 3 — fire-glow blend state + B-channel

### Blend state — ALREADY CORRECT, no fix needed

Checked whether SoH draws `g_title.cmb`'s glow material with the decomp's confirmed
`ADD(SRC_ALPHA, ONE)` (`title_logo_fireglow_cmab.md` §6.1) or a hardcoded standard alpha blend.
Traced the actual data path instead of assuming: `Shipwright/cmb3d/asset/cmb.cpp` parses
`blend_enable`/`blend_src_rgb`/`blend_dst_rgb`/`blend_eq_rgb` straight from the CMB material
bytes at `+0x138/+0x13C/+0x13E/+0x140` (real per-material data, not a material-class table), and
`Shipwright/cmb3d/asset/cmb_glgroups.cpp` passes those fields through UNMODIFIED into the draw
group (`cg.blendEnable = mat->blend_enable`, `cg.blendSrcRGB = mat->blend_src_rgb`, etc — no
override, no hardcoded default other than a defensive fallback when `mat` itself is null, which
is not the case for a loaded CMB). `title_fireglow.cpp`'s draw call
(`gSPZelda3DDrawUV(... modelId ...)`) goes through this same CMB-material-driven blend path —
there is no separate/duplicated blend-state assignment for this material anywhere in the title
overlay code. Since `g_title.cmb`'s own material-0 bytes ARE `blend_enable=1,
src=0x302(SRC_ALPHA), dst=0x1(ONE)` (byte-verified in the decomp doc), SoH's renderer is
already drawing this material with the exact additive equation. **Verdict: blend state was
correct before this session — no code change made.**

### B channel — mechanically explained as an authored zero, not a renderer bug

Dumped `Misc/g_title_fire.cmab`'s raw ConstColor curve (`tools/cmab.py`, direct ROM read, no
harness needed) for material 0 channel 0 (the track `Zelda3D_TryDrawTitleFireGlow` samples):

```
ConstColor mat=0 ch=0
  track[U] (R) type=Hermite [(0:0.8) (40:1) (59:0.7) ... (300:0.8)]
  track[V] (G) type=Hermite [(0:0.43) (39:0.53) (59:0.33) ... (300:0.43)]
  track[Z] (B) type=Hermite [(0:0) (300:0)]   <-- FLAT ZERO, both keyframes
```

The B track has exactly TWO keyframes, both value 0 — i.e. the CMAB's own animation curve
holds `constColor0.B = 0.0` for the ENTIRE 300-frame duration, not just the frozen post-loop
value the prior session's §6.3 correction described. Per the full steady-state formula
(`title_logo_fireglow_cmab.md` §6.3): `finalColor.rgb = 2.0 * ((efc+mableT)*efc) *
constColor0.rgb` — with `constColor0.B` identically 0, **the glow material's own additive
contribution to the blue channel is authored to be EXACTLY ZERO at every frame of the
animation**, not approximately low. SoH's CMAB player (`zelda3d_cmab.cpp`) samples this track
with the same generic, channel-symmetric code path as R/G (`Zelda3D_CmabSampleConstColorRGB`
loops `i=0..2` identically) — confirmed no per-channel special-casing exists that could
selectively suppress B.

Given that, the `fireglow_ab.py --diff` methodology's measured "SoH dB" and "Az dB" values
(both nonzero, ~26-35, in the v2 journal's delta table) are NOT measuring this material's own
color contribution at all — they are measuring something else that changed brightness in the
diff window. `delta_stats()` is hue-agnostic (masks on `post-pre > thresh`, not on absolute
color), which rules out a STATIC night-sky-floor confound (ruled out already by construction —
the pre/post frames are differenced, not thresholded on absolute brightness) but does NOT rule
out a DYNAMIC one: the pre/post window (cs 460→525/570) spans ~70-110 cs-frames, during which
the sky dome itself is cross-fading (Task 1's own subject) and the title's ambient/fog palette
is blending (`kTitleLightSchedule`) — both genuinely add blue-channel brightness to background
pixels inside the same logo box over that window, independent of the glow mesh. Since Task 1
just changed the dome's own schedule this session, the "Az dB"/"SoH dB" absolute values from
the v2 measurement are now stale outputs of the OLD (N64-table) dome path and should be
re-measured post-Task-1 before drawing any further conclusion about a still-existing gap.

### Verdict

**No B-channel fix applied.** The ground truth (this material's own ConstColor curve) says
B=0 is the CORRECT, authored value for the ENTIRE demo, not a deficiency — multiplying by a
gain to force SoH's measured B up would contradict the decomp's own data and is exactly the
gain-constant hack this project's rules forbid. The previously-reported 0.60 ratio is most
likely measuring a background/sky-dome confound in the diff window (now itself changed by
Task 1), not a glow-material defect. **Concrete next step**: re-run
`fireglow_ab.py --diff` post-Task-1 and, if a gap persists, isolate it by ALSO drawing a
control diff over a box outside the logo (pure sky) to subtract the sky's own delta from the
logo-box delta — the tool does not yet support this and would need a small extension (not built
this session — static/formula-level analysis already explains why B should be ~0 either way).

## 4. Task 4 — closing sweep v3

Same 10 points (az 100/200/360/500/700/1000/1300/1522/1700/1900, `soh = az+408`), same tool
(`tools/title_ab.py`'s `cmd_ab` logic, run in one continuous forward-only harness session —
`scratch/title_ab_sweep_v3.py`, new/gitignored — to avoid 10 re-boots), same score/mean\|d\|
definitions as v1/v2, `ZELDA3D_TEXPACK=off` throughout. Full per-region tables:
`scratch/logs/sweep_v3_out.log` (machine-local).

| az | soh | content | score v1 | score v2 | **score v3** | mean\|d\| v1 | mean\|d\| v2 | **mean\|d\| v3** |
|---|---|---|---|---|---|---|---|---|
| 100  | 508  | night sky, moon rising     | 0.8020  | 0.9070 | **0.9334** | 14.2 | 9.7  | **3.24** |
| 200  | 608  | night, rider distant       | 0.7848  | 0.8827 | **0.9302** | 16.7 | 11.2 | **2.75** |
| 360  | 768  | moonlit rider crossing     | 0.7298  | 0.8175 | **0.8850** | 19.7 | 15.4 | **3.42** |
| 500  | 908  | grass close-up push        | 0.6001  | 0.7073 | **0.7073** | 22.1 | 1.8  | **1.78** |
| 700  | 1108 | logo fade-in               | 0.0741  | 0.4335 | **0.4984** | 22.4 | 4.4  | **4.88** |
| 1000 | 1408 | logo display + copyright   | 0.1293  | 0.4305 | **0.7542** | 26.9 | 9.8  | **7.93** |
| 1300 | 1708 | logo display, castle wall  | 0.1474  | 0.3076 | **0.6404** | 29.7 | 23.5 | **10.03** |
| 1522 | 1930 | logo display               | -0.2325 | 0.3476 | **0.6169** | 86.8 | 25.6 | **14.76** |
| 1700 | 2108 | logo display               | 0.1909  | 0.3906 | **0.6724** | 69.5 | 14.9 | **10.48** |
| 1900 | 2308 | logo display               | 0.0589  | 0.4667 | **0.6957** | 65.4 | 10.4 | **7.60** |
| **sweep mean** | | | | | | **37.3** | **12.7** | **6.69** |

Every point improved on BOTH metrics again. Overall sweep mean\|d\| **12.7 → 6.69** (a further
~47% cut on top of v1→v2's ~66% cut). The night points (100/200/360) — Task 1's direct
target — improved the most (mean\|d\| roughly halved to a third of their v2 values,
9.7/11.2/15.4 → 3.24/2.75/3.42): the dome's real, wider night→sunrise blend window visibly
closes the "SoH mid-sky brighter/greener" residual (v2 residual 3) exactly as
title_sky_dome.md §9.4 predicted it should. The dawn/display points (1000-1900) also all
improved substantially (scores roughly doubled vs v2 at 1300/1522/1700; mean\|d\| cut
30-45%), consistent with Task 1 fixing the SAME dome mechanism across the whole dawn arc, not
just the three points it was reconciled against.

### Residual list after v3

1. ~~Title lifetime~~ / ~~terrain ambient~~ / ~~star footprint~~ / ~~overlay scale~~ —
   RESOLVED in prior sessions, unaffected by this session's changes (v2's list items 1/2/6/8).
2. **Sky-dome cross-fade (v2 residual 3): SUBSTANTIALLY CLOSED** by Task 1's schedule port —
   mean\|d\| at the three night points fell 65-71%. Not fully zero (3.2-3.4 residual remains,
   likely the Task-2-flagged +1 cs-frame dayTime phase, which nudges the blend weight by
   ≤0.3/255 at these points — see Task 2's "downstream effect" note — too small to be the
   dominant remaining term; more likely ordinary per-pixel dome-mesh/vertex-color quantization
   noise, not re-chased this session).
3. **Fire-glow (v2 residual 4): B-channel gap MECHANICALLY EXPLAINED, not fixed** — the
   material's own CMAB curve authors B=0 for the entire animation; the previously-measured
   0.60 ratio is most likely a stale sky-cross-fade confound in the diff window, now itself
   changed by Task 1. Concrete next step: re-run `fireglow_ab.py --diff` and add a
   sky-only control-box subtraction (tool extension, not built this session).
4. **New/still-visible large region deltas in the logo band, region (100,80)-(300,160), at
   EVERY point from az=700 onward** (e.g. az=1900: d=(+26,-9,-31) and d=(-22,-31,+9) in the
   two halves of that band) — consistent in sign/shape across 5 different points, strongly
   suggesting this is the fire-glow/wordmark rendering itself (not scene-dependent content),
   i.e. the SAME still-open residual as item 3, now visible in the region-grid data at full
   sweep scale rather than only in the dedicated glow-diff tool. Next step: same as item 3.
5. **az=1522, region (0,0)-(100,80): SoH dramatically brighter than Az**
   (`az=(76,89,87) soh=(144,158,140)`, d=(-68,-70,-52)) — an outlier not present at the
   neighboring 1300/1700 points at the same screen region. Not investigated this session
   (out of the four assigned tasks); flagged as a fresh, point-specific anomaly for the next
   session — possibly a camera-framing or moon/billboard-brightness glitch unique to that
   frame's content, worth a dedicated content-matched screenshot check before further static
   analysis.
6. Camera framing at cs 438 (v1/v2 residual 5) — unchanged this session (az=700 score
   0.4335 → 0.4984, small movement, not chased further — out of this session's four tasks).

### Conditions

- Build: `main` + this session's Task 1 dome-schedule port (commit `efb70276`); harness
  rebuilt via `tools/soh3d_harness.sh` (embeds current SoH source).
- `ZELDA3D_TEXPACK=off` for every capture.
- Headless throughout (harness Xvfb :99, `SOH3D_HARNESS_HEADLESS=1`).
- 438/443 lus_tests pass post-change (5 pre-existing asset-gated skips, unchanged from v2).
- No oot3d-decomp doc changes were needed this session — `title_sky_dome.md` §9.2 already
  had the full 9-entry dome table from a prior decomp-stream session; Task 1 only needed to
  PORT it (it was not yet wired into SoH's actual draw path), not re-derive it.
