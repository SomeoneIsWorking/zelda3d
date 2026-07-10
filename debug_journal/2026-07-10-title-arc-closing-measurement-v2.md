# Title arc closing measurement v2 — per-enabled-light ambient sum ported (terrain ~1.9x SOLVED) + fresh 10-point sweep (2026-07-10)

Follow-up to `2026-07-10-title-arc-closing-measurement.md` (v1) and
`2026-07-10-title-three-residuals-remeasure.md`. Three deliverables:

1. **Task 1 — the terrain ~1.9x root cause is PORTED**: the per-enabled-light ambient
   accumulation from the disassembled `/CmbVShader.shbin`
   (`<oot3d-decomp>/docs/title_env_lighting.md` §10/§11).
2. **Task 2 — fire-glow re-measured** under the same mechanism hypothesis (verdict below:
   hypothesis NOT applicable to the overlay path; gap stays open).
3. **Task 3 — closing sweep v2**, the arc's second full 10-point measurement, after ALL of:
   lifetime fix (c95948ae), sky R/G skyboxTime fix, fire-glow combiner port, const-color
   palette (+0xB4) fix, overlay aspect/placement fix, and this session's ambient sum.

## 1. The per-enabled-light ambient sum (terrain ~1.9x root cause) — ported

### Ground truth (title_env_lighting.md §7.1 → §10 → §11)

- §7.1: SoH renders `saturate(2·texel·vColor·ambient/255)` formula-EXACT (sub-LSB);
  the oracle runs ~1.89–1.93x ABOVE that formula, channel-uniform.
- §10: `/CmbVShader.shbin` disassembled — the PICA vertex-lit program does NOT apply
  `matAmbient·sceneAmbient` once; it accumulates `matAmbient·LightAmbientColor_i` once
  PER ENABLED light slot (3 slots, each gated by its own `LightDir_i.w` flag; instr
  91/97/104). The diffuse half is a no-op for terrain (matDiffuse=black), so the ambient
  sum survives exactly N times.
- §11: the CPU-side per-material light-setup (`FUN_003fa5d0`) confirms a runtime-gated
  ≤3-slot loop with the scene color fetched ONCE and reused per enabled slot — i.e. every
  enabled slot carries the identical scene ambient. N64 scenes standardly enable 2
  directional lights → ground truth = 2× the ported formula, matching the measured 1.89–1.93x
  (sub-2 residual = the ~5% fog mix, §7.1).

### The port (real sum, no fitted constant)

SoH's `envCtx.lightSettings` tracks ONE scene ambient (the N64 shape), so the real N-term
per-light sum collapses exactly to `ambient × numEnabledLights`. Implemented as that sum,
with N derived from the LIVE light data, not hardcoded:

- `Shipwright/soh/src/zelda3d/zelda3d.c` (`Zelda3D_UpdateLight`): counts enabled slots —
  light1 (key) always authored; light2 (fill) counted enabled iff its direction is
  non-degenerate (`|light2Dir| > 0.5`, the same degeneracy test the normalize already used).
  Passes `numEnabledLights` (1 or 2) into `Zelda3D_GL_SetLightParams` (new trailing param).
- `Shipwright/libultraship/src/fast/zelda3d_gl.cpp`: new `gZelda3dAmbientLightCount`
  (float, default 2), set from the param.
- `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp`: `uAmbient.w` is now the
  enabled-light COUNT for the draw (0 = ambient path off, exactly the old gating), and the
  scene-lit branch multiplies `rgb *= uAmbient.xyz * uAmbient.w`. When SoH's light model
  grows distinct per-slot ambient colors this becomes a genuine loop; today the N identical
  terms are really there in the 3DS uniform buffer, so the multiply IS the sum, not a fudge.
- No UBO layout change (uAmbient stays vec4); all 443 lus_tests pass (incl. the std140
  offset tests).

### Verification — ratio collapsed to ~1.0x

Analytic decomposition at the pixel-aligned az=500/soh=908 pair
(`tools/terrain_pixel_decompose.py`, formula = `sat(2tva)` i.e. the OLD 1-light formula, so
BOTH engines should now sit ~2x above it):

```
pixel      | expected=sat(2tva) | SoH px        | Az px         | SoH/exp  Az/exp
(100,210)  | (20.1, 35.2, 12.5) | ( 40, 69, 25) | ( 45, 79, 39) |  1.96     2.24
(200,220)  | (17.4, 29.8,  8.5) | ( 34, 59, 16) | ( 31, 53, 12) |  1.98     1.78
(320,205)  | (22.2, 36.6, 20.5) | ( 42, 70, 36) | ( 31, 53,  8) |  1.91     1.45
(150,150)  | (16.5, 30.2,  9.1) | ( 33, 60, 18) | ( 25, 47,  2) |  1.99     1.56
(250,150)  | (25.7, 35.7, 21.6) | ( 51, 71, 43) | ( 51, 67, 39) |  1.99     1.88
```

SoH now sits at a clean ~1.96–1.99x of the old formula (the exact ×2 sum); Az's 1.45–2.24
spread around the same band is its known per-pixel filtering noise (§7.1: only region means
are meaningful). Region-mean A/B at the same pair (`title_ab ab 500 --soh 908`, pack off):
every one of the 12 regions now |d| ≤ 4 per channel — e.g. (100,0)-(200,80): Az (38,63,24)
vs SoH (40,65,25) (was Az (38,63,24) vs SoH (18,28,11), R2.1 G2.3 B2.2, in v1). **The
1.9–2.3x terrain residual is CLOSED** — by the decomp-named mechanism, not a constant.

## 2. Fire-glow under the same mechanism — hypothesis FALSIFIED for the overlay path

Checked whether the overlay materials get the same per-enabled-light ambient sum:

- `g_title.cmb` (fire-glow): material 0 has **IsVertexLighting = 0** (byte at mats+0x0C+0x01,
  raw file read). The PICA vertex-lit color block (§10, gated on `IsVertexLighting`) does NOT
  run for it — no ambient sum applies on the 3DS side. (SoH-side it also draws FORCE_UNLIT,
  so no change either way.)
- `title_logo_us.cmb` (wordmark): its 12 materials ARE vertexLighting=1 (matAmb bytes range
  25..255) — but the wordmark actor's own decompiled light env (title_logo_actor.md §6.3)
  carries ambient={1,1,1,1} on ITS dedicated fragment light, and the wordmark is not part of
  the measured glow delta by construction (frame-differencing isolates the glow mesh).

Re-measured the glow additive delta anyway (frame-difference method, `fireglow_ab.py --diff`,
cf 460 pre vs 490/525/570 post, pack off, current build):

```
   cs    az |  Az dR     dG     dB     px | SoH dR     dG     dB     px | R ratio soh/az
  490   804 |   58.2   25.6   35.0  20469 |   18.3    7.4   18.9  13657 |  0.314
  525   874 |   66.1   29.3   34.4  23821 |   24.5   12.4   23.9  20878 |  0.372
  570   964 |   64.4   31.1   34.3  24572 |   34.5   26.3   26.2  22821 |  0.535
```

Verdict: **the per-enabled-light ambient mechanism does not apply to the glow** (material is
not vertex-lit), and the measured gap persists at 0.31–0.54 of oracle. NOTE the ratios read
LOWER than the prior session's 0.398/0.455/0.564 — that measurement ran with the texture
pack ON; this one is pack-off (glow region includes the pack-substituted wordmark texture in
the box). Cross-condition comparison is not apples-to-apples; the stable conclusion is the
40–70% additive-delta gap is real and remains OPEN. Hypothesis "same oracle-side
amplification as terrain" is now FALSIFIED (terrain's amplification is the vertex-lit
ambient sum, which this material provably does not receive). Remaining candidates from
`2026-07-10-title-three-residuals-remeasure.md` §1 stand: texture-content-level
(g_title_efc/g_title_mable_t decode) or the +0x1D0 alpha staging ceiling — next step is the
raycast-decomposition method applied to the glow texels.

## 3. Closing sweep v2 (pack OFF, current build) vs v1 (2026-07-10, pack on)

Same 10 points, same tool (`title_ab.py ab <az> --soh <az+408>`), score = grayscale-SSIM
content score, mean|d| = mean per-channel |Az−SoH| over the 4x3 region grid. Full region
tables: `scratch/title_ab/sweep_v2_log.txt`; SxS PNGs `scratch/title_ab/v2_*_sxs.png`
(machine-local, never committed).

| az | soh | content | score v1 | score v2 | mean\|d\| v1 | mean\|d\| v2 | dominant residual now |
|---|---|---|---|---|---|---|---|
| 100 | 508 | night sky, moon rising | 0.8020 | 0.9070 | 14.2 | 9.7 | night-sky R/G: SoH mid-sky BRIGHTER than Az (pre-existing sign in v1; terrain rows now \|d\|≤8) |
| 200 | 608 | night, rider distant | 0.7848 | 0.8827 | 16.7 | 11.2 | same |
| 360 | 768 | moonlit rider crossing | 0.7298 | 0.8175 | 19.7 | 15.4 | same (sky rows d up to −52 G) |
| 500 | 908 | grass close-up push | 0.6001 | 0.7073 | 22.1 | **1.8** | none ≥5/255 — terrain residual CLOSED |
| 700 | 1108 | logo fade-in | 0.0741 | 0.4335 | 22.4 | 4.4 | one region (100,80): d=(−7,−24,−24) — the known cs-438-segment camera-framing artifact, much reduced |
| 1000 | 1408 | logo display + copyright | 0.1293 | 0.4305 | 26.9 | 9.8 | fire-glow weakness (Az mid-frame R d=+25..+34); overlay regions |
| 1300 | 1708 | logo display, castle wall | 0.1474 | 0.3076 | 29.7 | 23.5 | dawn-sky warmth (SoH sky G/B high, Az warm R high); fire-glow |
| 1522 | 1930 | logo display | −0.2325 | 0.3476 | 86.8 | 25.6 | v1's "SoH exited title" GONE (lifetime fix verified in-sweep); now dawn-sky + glow deltas |
| 1700 | 2108 | logo display | 0.1909 | 0.3906 | 69.5 | 14.9 | v1's attract-gameplay exit GONE; residuals = dawn-sky warmth split (Az warm R vs SoH G/B) + fire-glow |
| 1900 | 2308 | logo display | 0.0589 | 0.4667 | 65.4 | 10.4 | terrain rows near-exact (\|d\|≤8, several ≤3); residual = fire-glow/dawn warm split in the logo band (Az +R, SoH +G/B) |

Headline: every point improved on BOTH metrics. The three post-exit points (1522/1700/1900)
now measure real rendering (v1 measured "SoH left the title"); the terrain-dominated points
(500/700) collapsed to mean|d| 1.8/4.4; overall sweep mean|d| went 37.3 (v1) → 12.7 (v2).

### Residual list after v2

1. ~~Title lifetime (v1 residual 1)~~ — RESOLVED (c95948ae), re-verified by this sweep: SoH
   stays in the title cs at az=1522+ (v1 showed HUD/gameplay; v2 shows the title scene).
2. ~~Terrain/vegetation ~2x darkness (v1 residual 2)~~ — **RESOLVED this session** (per-enabled-
   light ambient sum, §1). az=500 mean|d| 22.1 → 1.8.
3. Night-sky R/G: SoH's mid-sky runs brighter/greener than Az at night points (d −10..−52 on
   R/G in sky regions, stable v1→v2 sign) and the dawn warm-up still diverges at cs 588+
   (v1 residual 3's remnant). OPEN — sky-dome path, not the terrain ambient (unchanged by §1).
4. Fire-glow additive delta 0.31–0.54 of oracle (v1 residual 4) — NARROWED to R 0.86 / G 1.02 /
   coverage 0.92 by the shared-basis overlay-scale fix (the glow was drawn at the wordmark's pixel
   height, ~31% under its authored footprint) — see
   `2026-07-10-title-star-footprint-and-overlay-scale-derivation.md` §3. B 0.60 still OPEN;
   candidates remain texture decode / alpha staging.
5. Camera framing at cs 438 (v1 residual 5) — largely resolved in practice (az=700 score
   0.0741 → 0.4335; one region residual d=(−7,−24,−24)); segment-boundary audit still open.
6. ~~Overlay placement/scale (v1 residual 6)~~ — **RESOLVED** (2026-07-10 follow-up): the whole
   overlay placement/scale is now the decomp perspective-compose derivation (shared basis at depth
   -34 through the live cs fov; all fitted fraction constants deleted). Copyright bbox 0.987/1.000
   of oracle, center within 1 px; az=1000 frame score 0.4305 → 0.6954. See
   `2026-07-10-title-star-footprint-and-overlay-scale-derivation.md` §2.
7. Texpack contamination (v1 residual 7) — measurement caveat closed: v2 ran `ZELDA3D_TEXPACK=off`.
8. ~~Star brightness (v1 residual 8)~~ — **RESOLVED** (2026-07-10 follow-up): SoH's synthetic mip
   chain was blurring the single-level L8 star texture's sparse bright texels (peak crushed, integral
   preserved — exactly the measured signature). Additive point-sprite material class now samples
   max_lod=0; peak ratio 0.744 → 0.970. See
   `2026-07-10-title-star-footprint-and-overlay-scale-derivation.md` §1.

### Conditions

- Build: current `main` + this session's ambient-sum port; harness rebuilt via
  `tools/soh3d_harness.sh` (embeds current SoH source; verified by the [1/7] zelda3d.c…
  [6/7] soh3d_harness relink in the build log).
- `ZELDA3D_TEXPACK=off` for every capture (clean parity, v1 residual 7's caveat).
- Headless throughout (harness Xvfb :99, `SOH3D_HARNESS_HEADLESS=1`).
- 443/443 lus_tests pass post-change.
