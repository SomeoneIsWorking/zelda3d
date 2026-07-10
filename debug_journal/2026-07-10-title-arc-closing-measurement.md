# Title arc closing measurement — wordmark sheen port + full-cs frame-matched A/B sweep (2026-07-10)

Two deliverables close the title-parity arc:

1. **Task 1 — wordmark sheen (light-direction sweep) ported** from the decompiled OoT3D logo
   actor draw fn (`<oot3d-decomp>/docs/title_logo_actor.md` §6.3, `FUN_001da4f4`).
2. **Task 2 — closing frame-matched A/B sweep** across the full title cs vs the embedded-Azahar
   oracle, with per-frame scores and named residuals (the arc's honest closing numbers).

## 1. Wordmark sheen port

### Ground truth (title_logo_actor.md §6.3, decompiled FUN_001da4f4)

The logo actor's `+0x1DC` field (0..255, ramping at +4.25/frame over cs frames 466–525 in
lockstep with the backdrop alpha, then frozen at 255 forever) is NOT an alpha — it drives the
DIRECTION of a hardware fragment light on the wordmark's own material (`title_logo_us.cmb`
only):

```
t   = clamp(+0x1DC / 255, 0, 1)
w0  =  2t - 1;  w1 = 1 - 2t;  w2 = -0.5 - 0.5t
dir = normalize(w0*row0 + w1*row1 + w2*row2)     // rows of the wordmark's own billboard basis
```

with STATIC light colors ambient {1,1,1,1}, diffuse {0.1834,0.1834,0.1834,1}, specular
{1,1,1,1}, emission {0,0,0,1} — only the direction animates. The decomp's basis rows are an
IDENTITY rotation (the billboard basis carries only a local translate `(0,0,-34)`), so in the
wordmark's own object space the two endpoints reduce exactly to `t=0: (-1, 1, -0.5)` and
`t=1: (1, -1, -1)` (pre-normalize), i.e. `dir(t) = (2t-1, 1-2t, -0.5-0.5t)`.

### Seam: a per-draw light-direction override through the existing draw plumbing

The prior session's flag ("no existing draw seam carries a per-draw light-direction uniform")
was correct — so one was added, at the same altitude as the existing per-draw material
overrides (`SetMatConstOverride` et al.), NOT as a bug-specific hack:

- `Zelda3D_GL_SetLightDirOverride(modelId, dx, dy, dz)` / `Zelda3D_GL_ClearLightDirOverride`
  (`libultraship/include/fast/zelda3d_gl.h`, `src/fast/zelda3d_gl.cpp`): per-model
  object-space light-direction override. Read DIRECTLY at Submit time (documented: the title
  overlay draws via a raw `gSPZelda3DDrawA` opcode, never calls `EmitPose`, and has exactly one
  instance per frame — the emit-order pairing problem `ItemPose` solves doesn't exist here).
- `Zelda3D_Sg_DrawModel(..., const float* lightDirOv)` (new trailing param, default `nullptr`):
  when set, the renderer transforms the object-space direction by THAT draw's own `mat3(uMV)`
  (the identical transform the vertex shader applies to normals), renormalizes, and feeds it as
  the draw's `uLightDir` instead of the scene sun (`gZelda3dLightDirWorld`). Scene/other-actor
  lighting untouched.
- `SgUbo::uSheen` (new vec4 in `zelda3d_sg_ubo.h`; `.x` = diffuse strength, rest reserved):
  the fragment shader applies `shade *= (1.0 + uSheen.x * max(0, N·L))` — an ADDITIVE diffuse
  boost on a full-bright baseline, which is what the decomp's lighting reduces to (ambient
  always 1.0 + a small diffuse bonus). The existing half-Lambert term (`uParams.y`) was NOT
  reused: it darkens from full-bright (`0.55 + 0.45·hl`), the wrong shape for this light.
  `uSheen.x = 0.1834` (the decompiled diffuse constant, `kWordmarkSheenDiffuse`) iff the draw
  carries an override; 0 otherwise (no behavior change for every other draw).
- **Specular: proven-negative, not approximated.** The PICA specular term needs the material's
  own light-LUT config (CMB-side, not in the actor's decompiled code) AND a view vector; the 2D
  ortho overlay pass has no real camera for a Blinn-Phong H to reduce to. Documented in-code at
  the uSheen declaration; the diffuse term is the decomp-derivable part and is ported exactly.
- Mirrored `uSheen` into `Zelda3DUnified::CommonUbo` + `UNIFIED_COMMON_UBO_BODY`
  (size-parity padding, unified renderer is default-off) and updated the std140 offset tests
  (`libultraship/tests/zelda3d_render_tests.cpp`) — all 6 pass. NOTE: the offset test had been
  silently stale (it asserted `uBones` at 320 while `uMatConst` already lived there); now it
  asserts every field including the two newest.

### Driver: phase state carries sheenT

`title_logo.cpp`'s `LogoPhaseState` gained `sheenT` (0..255): `stagedRamp(csFrame,
backdropStart, 4.25, 60)` computed UNCONDITIONALLY from the fade-in trigger (not inside the
fade-in/fade-out branches), so it is 0 before cf(fadeIn+121), ramps over the same 60-frame
window as the backdrop alpha, and saturates at 255 for the rest of the title's life — the
decomp's "freezes at t=1, never decremented on fade-out" for free, no latch needed.
`Zelda3D_TryDrawTitleLogo` converts `t = sheenT/255` → `dir=(2t-1, 1-2t, -0.5-0.5t)` → 
`SetLightDirOverride` before the draw opcode.

### Verification (live, headless, ZELDA3D_DBG_SHEEN=1)

Per-frame trace across the sweep window (run log, cs cursor free-running from boot):

```
[SHEEN] csFrame=345 sheenT=0.00   t=0.000 dir=(-1.000, 1.000,-0.500)   # trigger; still endpoint 0
[SHEEN] csFrame=466 sheenT=4.25   t=0.017 dir=(-0.967, 0.967,-0.508)   # ramp start (backdrop stage)
[SHEEN] csFrame=495 sheenT=127.50 t=0.500 dir=( 0.000, 0.000,-0.750)   # midpoint
[SHEEN] csFrame=525 sheenT=255.00 t=1.000 dir=( 1.000,-1.000,-1.000)   # ramp end, exact
[SHEEN] csFrame=650 sheenT=255.00 t=1.000 dir=( 1.000,-1.000,-1.000)   # frozen (display hold)
```

Every frame 466..525 stepped +4.25 exactly; direction endpoints byte-match the decomp formula;
frozen thereafter — the full mechanism (phase → t → direction → per-draw override → shader) is
live end-to-end. Visually the effect is a subtle diffuse gleam (0.1834 peak boost, no specular);
screenshots at pinned cf466/490/525 are in scratch/screenshots/sheen_*.png (machine-local).

## 2. Closing A/B sweep (frame-matched, oracle = embedded Azahar)

### Preconditions: offset verified, harness rebuilt

- **+408 offset verified for this build.** A content calibration at az=100 peaked at +379, but
  with margin 0.0028 — inside the tool's own documented low-motion-night plateau noise. A second
  calibration at high-motion content (az=700, `calibrate 700 --margin 80`) peaked at soh=1111
  (+411) with a 10x sharper margin (0.0228) — i.e. +408 is correct to within ±3 steps (±1.5 cs
  frames). Decisively confirmed by direct cursor readout (below): at every +408 pair up to
  az=1300, Az's cs frame (`0.5·az_step + 88`) and SoH's (`0.5·(soh_step − 232)`, measured live
  via `soh_titlecs` at soh_step 600/1000/1400/1708: cs 184/384/584/738 — slope 0.5 and intercept
  232 both EXACT) land on the **same cs frame to the frame**. The +408 pairs are cs-frame-exact,
  not approximate.
- **Harness rebuilt before measuring.** The prebuilt `soh3d_harness` (Jul 9 23:57) embedded a
  SoH that PREDATED the overlay commits (029843bb ortho pass, 332e1868 skip path, this session's
  sheen) — its SoH panel showed no logo at all at logo-phase frames, which would have produced a
  false "logo missing" residual. `tools/soh3d_harness.sh` rebuilt it against current source
  (incremental, its own object tree) before the sweep below.

### Sweep table (az_step ∈ {100..1900}, soh_step = az_step + 408, content = same cs frame)

Score = title_ab's grayscale-SSIM content score (structure match; lighting-insensitive by
design but NOT overlay-insensitive). mean|d| = mean per-channel absolute RGB delta over the
4x3 region grid (the per-region tables are in scratch/title_ab/final_sweep.txt; SxS PNGs
scratch/title_ab/fsweep_*.png — machine-local, per never-commit-PNGs).

| az_step | soh_step | cs frame | content | score | mean\|d\| | dominant residual at this pair |
|---|---|---|---|---|---|---|
| 100 | 508 | 138 | night sky, moon rising | 0.8020 | 14.2 | terrain darkness; sky R/G warmth; star brightness |
| 200 | 608 | 188 | night, rider distant | 0.7848 | 16.7 | same |
| 360 | 768 | 268 | moonlit rider crossing | 0.7298 | 19.7 | same + rider not visible in SoH pane |
| 500 | 908 | 338 | grass close-up push | 0.6001 | 22.1 | terrain darkness (Az≈2x SoH per channel) |
| 700 | 1108 | 438 | logo fade-in (wordmark ramp done, backdrop ramping) | 0.0741 | 22.4 | camera FRAMING differs at this segment despite cs-frame-exact match (see residual 5); logo present in BOTH panes, SoH's placed ~0.03-0.05 screen lower-right and slightly smaller |
| 1000 | 1408 | 588 | logo display + copyright | 0.1293 | 26.9 | fire-glow much weaker in SoH; copyright block smaller; texpack-substituted textures; dawn-warmth gap (Az mid-frame R d=+52..+79) |
| 1300 | 1708 | 738 | logo display, castle wall | 0.1474 | 29.7 | as az=1000; camera framing close (background structures align) |
| 1522 | 1930 | 849 | logo display (oracle) | **-0.2325** | 86.8 | **SoH has EXITED the title** (see residual 1) — HUD/gameplay vs oracle title |
| 1700 | 2108 | 938 | logo display (oracle) | 0.1909 | 69.5 | same — SoH in N64 attract-gameplay |
| 1900 | 2308 | 1038 | logo display (oracle) | 0.0589 | 65.4 | same |

### Residuals, named and attributed

1. **[RESOLVED 2026-07-10, commit c95948ae — see `2026-07-10-title-lifetime-ownership.md`.
   Root cause: z_demo.c's Cutscene_Command_Terminator has no gameMode gate on its
   `playCutscene` trigger, so the N64-authored title cs's own 0x3E8 terminator ended the
   PlayState at its shorter length; now suppressed while Zelda3D_Title_IsActive(). Full
   2400-frame loop, wrap fade, second loop and press-START skip verified free-running.]**
   Title lifetime is still the N64 gamestate flow's, not the 3DS cs's — the biggest
   remaining structural divergence, measured precisely this session: SoH's title-cs cursor
   advances at exactly 0.5 cs/engine-step until **cs frame 811** (soh_step ~1854), freezes
   there, and the game exits into the N64 attract sequence (gameplay demo with HUD — visible
   in fsweep_1522/1700/1900 SoH panes). The OoT3D oracle plays its single 2400-frame title cs
   in a loop forever. Consequence: the ported fade-out trigger (cs 1930), screen-level loop
   fade (2310–2460), and loop restart (2400) are correct against the decomp but UNREACHABLE
   in the real flow — the N64 `Play`/demo state machine ends the title at its own, earlier
   boundary. Fix locus: TitlePresentation must own the title's lifetime (suppress the N64
   demo-end transition, loop the cs at 2400 per the op-0x3e8 destination + op-0x7c fade)
   rather than riding the N64 flow. The three sweep rows past cs 811 measure THIS, not
   rendering.
2. **[DECOMPOSED 2026-07-10 — verdict: ORACLE-side, see `2026-07-10-fireglow-combiner-and-
   terrain-decomposition.md` §2. Analytic single-pixel decomposition
   (tools/terrain_pixel_decompose.py: raycast of the real spot99 room-0 ROM geometry from
   the live byte-matched camera, exact texel + baked vertex color + live ambient per pixel):
   SoH's rendered pixels equal saturate(2·t·v·a) to sub-LSB precision (mean |err|
   0.32–0.56/255 over 400 near-ground pixels) — SoH's whole chain is formula-EXACT. The
   oracle's pixels are ~1.9x ABOVE the formula, channel-uniform on region means
   (1.89/1.93/1.85 RGB). Texpack confound also ruled out (ZELDA3D_TEXPACK=off: ratio
   unchanged, pack shifts means ≤5%). The missing term is on the DECOMP/oracle side
   (~ one extra x2, or an Az output-stage curve) — reported to the decomp stream
   (title_env_lighting.md open item). No SoH change made; nothing to tune here until the
   decomp names the term.]**
   **[UPDATED 2026-07-10 — see `2026-07-10-title-terrain-uboverify-and-followups.md`.
   NOT a lighting-tuning item: RULED OUT as an ambient/vColor/shader-math or UBO-fill bug by
   direct runtime measurement (`<oot3d-decomp>/docs/title_env_lighting.md` static derivation +
   this session's live `lightparams`/`sgdump 1000` readback: `ambient=(0.192,0.263,0.435)`
   matches the doc's ROM-derived expectation `~(0.20,0.26,0.43)` to within frame noise;
   `vtxLit=1 matAmb=(1,1,1) matDif=(0,0,0) combScale=2.0` — exact formula match). Re-measured
   on a pixel-aligned pair (az=500/soh=908, content score 0.6001, confirmed NOT the
   framing-mismatched az=700 pair) with the CURRENT build: ratio persists at 1.9–2.3x on all
   12 regions (e.g. (100,0)-(200,80): Az(38,63,24) vs SoH(18,28,11), R2.1 G2.3 B2.2 —
   unchanged from the numbers below). Verdict: honest negative — everything
   derivable from ROM bytes through the shader checks out; the residual's cause is NOT
   identified and is NOT being chased further by tuning constants per the
   stop-micro-tuning-lighting directive.]**
   **[RESOLVED 2026-07-10, commit 02181072 — see `2026-07-10-title-arc-closing-measurement-v2.md`.
   The decomp stream disassembled /CmbVShader.shbin (title_env_lighting.md §10/§11): the real
   PICA vertex-lit program sums matAmbient·LightAmbientColor_i once PER ENABLED light slot
   (2 for standard scenes) — the "oracle ~1.9x above the formula" was the FORMULA missing the
   per-light sum. Ported as a real sum (uAmbient.w = enabled-light count from live envCtx
   light data, no fitted constant); az=500 region deltas collapsed from R2.1 G2.3 B2.2 to
   |d|≤4/255 on all 12 regions.]**
   Terrain/vegetation darkness, ~2x per channel — d≈+20..+40 on every ground region at
   every matched pair (e.g. az=500 region (100,0)-(200,80): Az (38,63,24) vs SoH (18,28,11) —
   R2.1 G2.3 B2.2). Same magnitude as the 2026-07-08 remeasure (1.9–2.6x).
3. **[FIXED 2026-07-10, see `2026-07-10-title-terrain-uboverify-and-followups.md`. Root cause:
   `gSaveContext.skyboxTime` was never written by the title cs's direct `dayTime` assignment
   (`TitlePresentation::step`), so `Environment_UpdateSkybox`'s sync guard
   (`(sceneSetupIndex>=5 || gTimeIncrement!=0) && dayTime>skyboxTime`) never fired —
   `skyboxTime` stuck at its scene-load value and the schedule kept re-selecting the SAME
   `D_8011FC1C` row every frame, collapsing `skybox1Index==skybox2Index` (matches the
   idx1==idx2==3-constant smoking gun below) and freezing the cross-fade's warm channel.
   Fix: `title_presentation.cpp` now writes `gSaveContext.skyboxTime = csTime` alongside
   `dayTime`, mirroring the existing pattern at every other dayTime-jump site (z_scene.c,
   z_demo.c). Verified live: `sky info` now reports `idx1=3 idx2=0` (previously idx1==idx2)
   with `blend` actively climbing/cycling across cs frames 154→378, and `lightparams` ambient
   tracks the expected schedule. Title-only change; general gameplay's own skyboxTime writes
   (z_scene.c/z_demo.c) untouched.]**
   Dawn warmth lag — by cs 588+ Az's mid-frame warm regions run d=+52..+79 R: the oracle's
   sky/scene warm-up outpaces SoH's (sky unfreeze landed, but the warm channel still lags).
   Extension of the known sky R/G item (2026-07-08-title-divergence-remeasure verdict 3).
4. **[PARTIALLY RESOLVED 2026-07-10 — see `2026-07-10-fireglow-combiner-and-terrain-
   decomposition.md`. The material's REAL 3-stage TEV chain was decoded byte-level
   (`<oot3d-decomp>/docs/title_logo_fireglow_cmab.md` §3.1) and ported: stage-1's hardware
   scaleRGB=x2 (the direct "half brightness" cause) + stage-0's dual-texture
   ADD_MULT (efc+mableT)*efc detail-mask brightening + CMAB UV-scroll retargeted to
   coordinator 1 — all as first-class renderer features (uTex1/uTex1Xf/uMatConst.a-as-scale).
   Measured (tools/fireglow_ab.py, gold-hue flame mask, matched frames): az=936 SoH glow
   R mean 123.8→160.9 vs Az 200.6; az=1100 SoH 168.2 vs Az 201.4. REMAINING (still open):
   intensity ~0.8x + flame-pixel coverage ~45% of Az's, and at az=730 (cs 453) Az already
   shows a full wash while SoH's glow hasn't started — both point at THIS element's
   alpha-channel STAGING (+0x1D0 ramp timing/ceiling vs the oracle), no longer at combiner
   gain.]**
   Fire-glow intensity — both engines draw g_title.cmb's glow, but Az's is a prominent
   flame wash around the whole logo while SoH's is a faint gold tinge (fsweep_1000/1300).
   The CMAB ConstColor/UV port is live; the additive blend's effective gain is visibly low.
   Candidate: the glow mesh's own alpha path (+0x1D0 staging is correct; the additive
   src=SRC_ALPHA dst=ONE gain vs Az needs a quantitative probe).
5. **Camera framing at cs 438 (az=700 pair)** — despite the cs-frame-EXACT pair, Az frames a
   wide hillside with the road while SoH frames closer grass; yet at cs 588/738 the framing
   matches (mountain/tree/castle-wall silhouettes align). Points at a per-segment boundary
   issue in the OP97 camera port (segment active at cs 438), not a cursor-rate/phase error
   (both cursors proven frame-exact above). Follow-up: audit the ported segment table around
   cs 400-470 against the oracle's live camera eye.
6. **Overlay placement/scale delta** — SoH's wordmark sits ~0.03–0.05 screen lower-right of
   Az's and renders slightly smaller; the copyright block is smaller still. The current
   constants are oracle-MEASURED (bbox mask on az1000-era captures) rather than derived from
   the decomp's projection (the decomp gives local translates but copy scale was never
   recovered). Small, visible in direct SxS.
7. **Texture pack contamination (measurement caveat, not a parity bug)** — the hi-res texpack
   replaces the wordmark ("OCARINA OF TIME 4K") and copyright ("4K TEXTURED BY HENRIKO")
   textures in SoH's panes, so pixel deltas over the logo/copyright regions partly measure
   deliberate texture substitutions. A clean parity sweep of the overlay should disable the
   texpack. [2026-07-10: a real disable now exists — `ZELDA3D_TEXPACK=0|off|none`
   (texpack.cpp); previously there was no off switch. Terrain measurements are pack-neutral
   (≤5% shift, see `2026-07-10-fireglow-combiner-and-terrain-decomposition.md` §2a).]
8. **[UPDATED 2026-07-10 — see `2026-07-10-title-terrain-uboverify-and-followups.md`. The
   2026-07-08 L8-decode fix IS live in the current build (confirmed by reading
   `pica_texture.cpp`); the "unchanged" note above was the stale-harness-binary artifact the
   Task-2-sweep session already flagged (§ "Harness rebuilt before measuring"). Re-measured
   peak star luminance (moon-masked sky region, matched pairs az=200/soh=608 and
   az=360/soh=768) on a freshly rebuilt binary: Az peak ≈154–157, SoH peak now ≈112–125
   (ratio 0.73–0.80) — up from the pre-fix ~70/140 (ratio ~0.5) this doc originally measured.
   IMPROVED, not fully closed: SoH's brightest stars still run ~25-30% below the oracle's.
   Marked open with numbers, not re-tuned further.]**
   Star brightness — SoH's stars ~2x too dim to clear the noise floor (2026-07-08 remeasure
   verdict 2); visible in the night rows' sky regions.

### What now matches

- **Cs clocks**: rate law exact on both sides (0.5 cs/step), offsets pinned; a +408 pair is
  the same cs frame to the frame (measured, not inferred) up to SoH's early exit at cs 811.
- **Scene**: spot99 geometry/collision ported (the scene the oracle actually loads); actor
  spawn table + env-light palette byte-identical (2026-07-09-title-spot99-scene-port).
- **Camera**: OP97 spline port byte-exact-verified (tools/oot3d_cs_camera.py); live framing
  aligns at matched pairs cs 588/738 (one segment flagged, residual 5).
- **Overlay elements + timing**: wordmark (13-bone CMB + 120f assembly csab), fire-glow
  (CMAB ConstColor+UV), copyright, all three decomp-derived staged alpha ramps (3.0/4.25/6.0
  per frame, 40f delay, snap-to-255), synchronized -10/f fade-out, press-START skip
  (25f grace + 25/f fade), and — this session — the wordmark sheen light-direction sweep
  (+4.25/f over cf466-525, frozen at t=1). All gated on the cs's own op-0x03 triggers at
  345/1930, byte-confirmed against the decomp.
- **Sky bodies**: moon/sun/cloud dome ported with matched trajectories (prior sessions);
  moon position/size aligns in every matched night pane of this sweep.

### Artifacts

- scratch/title_ab/final_sweep.txt (scores + all 4x3 region tables), fsweep_*_sxs.png
- scratch/title_ab/cal_700* (offset verification), cal700_log.txt
- scratch/screenshots/sheen_*.png (Task 1 pinned-frame captures)
- run log [SHEEN] trace (Task 1 per-frame ramp verification; table above)
