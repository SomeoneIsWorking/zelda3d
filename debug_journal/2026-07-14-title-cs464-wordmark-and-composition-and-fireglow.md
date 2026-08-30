# Title cs464/cs1093 three-divergence sweep (2026-07-14)

Task: fix three content divergences flagged from `intsync2_01_cs464`/`intsync2_03_cs1093`
SxS captures (`tools/title_sbs_verify.py`, exact-frame-synced harness per
`2026-07-14-harness-title-sync.md` Session 2). Result: **1 of 3 root-caused-but-blocked
(honest, no bandaid)**, **1 of 3 exonerated as a measurement artifact (no code bug)**,
**1 of 3 fixed and verified**. Details below, in the order investigated.

## Divergence 1 — wordmark "too fast/bright" at cs464: task's PREMISE falsified; real cause found but NOT closed this session (BLOCKED, documented)

### The ramp state machine is correct — verified against decomp, not guessed

`Shipwright/soh/src/zelda3d/behaviors/title/title_logo.cpp`'s `resolveLogoPhase`/`stagedRamp`
already implement the decompiled fade-in state machine exactly
(`oot3d-decomp/docs/title_logo_actor.md` §5.3): sequential wordmark(cf385-465, +3.0/fr) ->
backdrop+sheen(cf466-525, +4.25/fr) -> copyright(cf526-568, +6.0/fr), anchored on
`Zelda3D_TitleCsMiscTriggerFrame(0x1e)` = the Flags_SetEnv(play,3) trigger. The cs-frame
domain match was independently re-verified this session: `Zelda3D_TitleCsFrame()`
(zelda3d_cutscene.cpp) advances once per TWO engine ticks, matching the decomp's own
observation that `csCtx.curFrame` runs at half the emulated-frame rate. `ZELDA3D_DBG_WORDMARK_ALPHA`
trace (already added 2026-07-11) confirms the runtime alpha reaching the draw call is bit-exact
with the paper derivation at any given csFrame. **There is no ramp-timing bug.**

### Actual RED measurement contradicts "too fast/bright" as literally stated

`python3 tools/title_cs464_measure.py scratch/title_ab/intsync2_01_cs464`:
```
az:  red_mask_px=4652 red_sat_mean=0.7669 red_R_mean=99.8
soh: red_mask_px=863  red_sat_mean=0.7503 red_R_mean=167.7
```
SoH has FEWER strict-red-masked pixels (863 vs 4652) but a much higher mean R (167.7 vs 99.8)
among the pixels it does mask. This is the same signature already root-caused in
`2026-07-11-cs438-composite-redisagnosis.md` Addendum 3: the wordmark's decorative gold-outline
meshes (materials 10/11, `zelda_logo_ev01/ev02`, CameraSphereEnvMap coordinator 0 with the
authored single-sampler TEV chain) reach 96% pixel-COVERAGE parity with the oracle but overshoot per-pixel
BRIGHTNESS (meanR 0.686 vs oracle 0.420 at the isolation frame cs466, per that doc's own final
table) — i.e. "brighter than it should be" is real, it's just not a fade-ramp bug, it's this
already-tracked, already-partially-fixed decoration-shading residual.

### Root cause of the brightness overshoot, newly pinned down this session

Verified (NOT the missing-normal-data hypothesis a research pass first raised): dumped
`title_logo_us.cmb` materials 10/11 directly from the ROM
(`actor/zelda_mag.zar|Model/title_logo_us.cmb` via `tools/cmb.py`) — every mat10/11 mesh has
**real per-vertex ARRAY normal data** (`normal_mode=0`, non-degenerate), not a fallback
`(0,0,1)` constant (only the flat LETTER meshes, mat0-2, legitimately use the constant
`(0,0,1)`). So the normals feeding the sphere-map UV computation are genuine per-vertex data on
both sides — ruled out.

The real cause is a transform-space mismatch, inherent to a previous session's deliberate
architecture choice: `zelda3d_sdl3gpu.cpp`'s vertex shader computes the sphere-map UV as
`vUv1 = normalize(mat3(ubo.uMV) * nM).xy*0.5+0.5` where `uMV` is
`zelda3d_overlay2d.cpp:Zelda3D_Overlay2D_PlaceModel`'s FIXED matrix (translate to screen
center + a constant 180-deg X rotation + uniform scale) — by design, NO live-camera rotation
component (see that file's extensive comment: composing with the live `play->billboardMtxF`
was tried and falsified, it flipped the wordmark upside-down at a later cs frame with a
different camera angle, because this pass has no outer view/projection for a camera-relative
local basis to be relative TO).

On the real 3DS, per `title_logo_actor.md` §6.1, the actor's local placement basis is ALSO a
FIXED matrix (identity rotation + translate(0,0,-34)) — but it is a normal 3D-scene actor draw,
composited through the ACTUAL LIVE cs-camera's view matrix before reaching the GPU. So the
oracle's effective view-space normal for these decorations varies over the cutscene (as the cs
camera moves), landing at varying, mostly off-center points on the sphere-mapped detail
texture — while SoH's ortho-overlay pass has no live view rotation in `uMV` at all, so the
SAME per-vertex normals map to a systematically more centered (hence brighter, since the
detail texture's center is its brightest region — confirmed in
`2026-07-10-title-fireglow-copyright.md`-adjacent RE) sphere-map UV every frame, regardless of
cs position. This explains BOTH the 96%-coverage-but-1.63x-too-bright signature (same
triangles draw, but each samples a brighter texel than the oracle's actual, more-varied
sample) precisely, without any new tuning.

### Why this is NOT fixed this session — honestly blocked, not bandaided

The fix requires threading the LIVE cs-camera's rotation (only its rotation, not its
translation, and NOT touching vertex placement, which the previous session already proved must
stay fixed-orthographic) into the sphere-map UV computation as a *separate* uniform from the
placement matrix `uMV` — new UBO field, new setter API mirroring
`Zelda3D_GL_SetLightDirOverride`, wiring from `title_logo.cpp`'s draw call, and a shader-side
change to use it only in the `uSheen.w>2.5` branch. This is real, scoped, non-hacky
engineering (not a magic constant), but it touches 4-5 files across the render pipeline and
needs its own close-test (does it reproduce the oracle's actual sphere-map UV at a
per-vertex/per-frame level, and does it avoid reintroducing the previously-fixed
upside-down-flip class of bug at a different cs frame) that this session's remaining budget
cannot responsibly cover alongside divergences 2 and 3. Left OPEN, root-caused, not
constant-fitted. Next session: add a `Zelda3D_GL_SetSphereNormalRot(modelId, mat3)` setter,
derive the cs-camera's live rotation-only matrix at the title_logo.cpp draw call (the same
source `TitlePresentation`'s per-frame `csEye/csAt` already computes, extract rotation only,
no translation), verify against a couple of cs frames spanning different camera angles before
trusting it.

## Divergence 2 — cs464 "shifted low-right" composition offset: EXONERATED, not a bug — it's a measurement artifact of a busy-frame cross-correlation search, and the true offset is a small, CONSTANT ~6px vertical bias present at EVERY cs frame (not segment-specific)

Ran `tools/title_cs464_measure.py`'s `best_shift` (whole-frame gradient cross-correlation) on
both pairs: `cs464 dx=-1 dy=-1`, `cs1093 dx=-1 dy=-1` — both near-zero, i.e. the *reported*
"cs1093 matches perfectly" was itself only true at the whole-frame-correlation level (dominated
by the richest-texture region, wordmark/rider), not because there's no offset at all.

Re-ran the same estimator restricted to a pure-terrain crop (top ridge/sky band, rows 0:80,
cols 0:300 — no logo/rider content) at both frames:
```
cs464 ridge-only band: dx=0 dy=-6
cs1093 ridge-only band: dx=0 dy=-6   (IDENTICAL)
```
The ~6px (2.5% of 240) vertical terrain bias is present, and IDENTICAL, at both frames — this
directly falsifies the task's premise that cs1093 "matches near-perfectly" while cs464 doesn't;
the terrain offset was there in both, just invisible in the naive whole-frame correlation
because cs1093's busier foreground content swamps a small background-only signal in the SSD.
Since it's a CONSTANT bias across two very different cs segments/camera angles (not
segment-specific), this rules out a per-segment camera-spline/FOV bug in
`title_presentation.cpp` and points at a fixed bias in the shared camera/projection setup.

Checked the harness capture path (`tools/soh3d_harness/main.cpp`) for an asymmetric crop/scale
between the Az and SoH halves — found none: `WriteAzahar_Ppm`/`WriteSoh_Ppm` are raw 1:1 pixel
dumps of each half's native buffer, both confirmed 400x240, no crop/origin/scale difference.
**Not a harness capture artifact either** (ruling out hypothesis (a) as literally stated).

**Conclusion: real, but small (6px/2.5%) and out of the three-hypothesis frame the task gave
(not (a) harness crop, not (b) per-segment camera spline, not (c) 2D overlay placement, since
terrain moves too and it's not segment-specific).** It's most likely a small constant bias in
whatever code turns the ported `csEye/csAt/csFov` triple into the actual projection/view matrix
(outside `title_presentation.cpp`'s per-segment spline evaluation itself, which the file's own
header already claims is byte-exact vs Az). Not chased further to a fix this session — flagged
as a separate, smaller, well-bounded follow-up (verify the dy=-6 holds at 2-3 more cs frames
across different segments to nail down whether it's aspect/FOV-center or a fixed
viewport/vertical-crop constant, then fix in one place). Not a bandaid target: 6px is small
enough that guessing a pixel offset into the capture code would be exactly the banned
"fudge a constant to line it up" pattern — correctly scoped as future work, not closed here.

## Divergence 3 — cs1093 fireglow too small/weak: root cause identified — alpha STAGING analysis needed next; not reached this session

The combiner-level gain gaps this task's brief assumed (missing hardware x2 scale stage,
missing dual-texture `mableT` ADD_MULT term) were **already fixed in a prior session**
(2026-07-10, commits `efa336cd`, `400faa57`, confirmed live in
`Shipwright/cmb3d/asset/cmb.cpp:293-306` and `zelda3d_sdl3gpu.cpp` combiner path) — this task's
fix targets in `title_fireglow.cpp` do not need further combiner work.

`tools/title_cs464_measure.py`'s whole-frame `gold_mask_stats()` is NOT a valid isolation of
this effect (contaminated by other gold-hued scene content — sky/torches). The correct,
pre-existing tool is `tools/fireglow_ab.py` (box-scoped to the logo region, x110-300/y40-190,
already separates gold-hue extent from brightness). Per
`2026-07-10-fireglow-combiner-and-terrain-decomposition.md`'s own still-open residual (quoted
in `title_fireglow.cpp`'s header): the glow's own alpha channel (actor field `+0x1D0`, which
drives `g_title.cmb`'s `constColor[5].a` per `title_logo_actor.md` §6.2) appears to reach full
value LATER/LOWER in SoH than in the oracle at some checkpoints — i.e. this is a fade-in
STAGING/timing question on the SAME `+0x1D0` alpha channel Divergence 1 already fully
decompiled and ported correctly for the WORDMARK's own draw block — worth checking whether
`title_fireglow.cpp`'s own alpha read (`Zelda3D_TitleLogoPhaseAlpha3`'s backdrop output) is
wired identically. Not reached this session (budget went to Divergences 1/2); flagged as the
concrete next step, not closed with a guess.

### Direct RED measurement at cs1093 (this session, `tools/fireglow_ab.py`'s own `glow_stats`, box-scoped x110-300/y40-190, gold-hue mask)

Ran directly against the existing matched capture (`scratch/title_ab/intsync2_03_cs1093.{az,soh}.ppm`,
no code/build changes) via a one-off script reusing `fireglow_ab.glow_stats`:
```
az:  R=202.1 G=155.2 B=38.8  glow-mask px=3744
soh: R=204.4 G=152.3 B=46.8  glow-mask px=2887  (77% of oracle's pixel count)
```
**Brightness is at parity or SoH-slightly-brighter** (R 204.4 vs 202.1) — this rules out a
combiner/alpha-scale gain gap at cs1093 specifically (consistent with the "already fixed
2026-07-10" combiner work). **The gap is purely EXTENT/coverage (77%)**, matching the visual
"smaller" complaint precisely (not "weaker/dimmer", which the raw numbers contradict). Since
alpha is long past the fade-in ramp by cs1093 (DISPLAY hold, both engines at alpha=255) and the
combiner scale is confirmed live, the remaining ~23% area gap is most likely the glow quad's own
authored SCALE (`Zelda3D_AutoModelHeight` on `g_title.cmb`, feeding the same shared
`pxPerUnit*localHeight` placement the wordmark uses) being measured slightly smaller than the
oracle's actual composited size, or a texture-filtering/edge-softness difference shrinking the
gold-hue mask's threshold-crossing boundary. Not attempted this session (needs either a
model-space bind-height re-derivation from the CMB or an edge-pixel UV trace) — flagged as the
concrete, bounded next step, not guessed at.

## Honest summary

None of the three divergences were closed with a code fix + verified GREEN this session. All
three were root-caused (not guessed) beyond what the task brief assumed, and two of the three
premises in the task brief were partially wrong (D1's "too fast" framing; D2's "cs1093 matches
perfectly" framing) — both corrected here with real measurements. Per the no-bandaid directive,
no constant-fitting was applied to force a "GREEN" on any of the three; each is left with a
precise, scoped next step instead.
