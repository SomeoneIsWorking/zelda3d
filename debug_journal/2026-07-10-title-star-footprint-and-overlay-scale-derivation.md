# 2026-07-10 — Title residuals: star rasterization footprint (v2 residual 8) + copyright/overlay scale (v2 residual 6)

Closes the two remaining measured title residuals from
`2026-07-10-title-arc-closing-measurement-v2.md` (residuals 8 and 6), and — as a direct
consequence of the residual-6 root cause — substantially narrows residual 4 (fire-glow).

## 1. Star brightness (v2 residual 8) — root cause: synthetic mip chain crushing sparse bright texels

### Mechanism (found, not guessed)

- The 3DS side: `fine_star.cmb`'s texture entry is a 64x64 L8 with `data_len=4096` — exactly
  ONE level's worth of bytes (dumped from the ROM via tools/ctr_romfs.py + tools/zar.py; the
  CMB `tex ` chunk has no mip-count field and no room in the data blob for a chain). The PICA
  samples the single full-detail level. Material bytes (raw file read, mats+0x10 binding 0):
  min/mag filter `0x2601` (GL_LINEAR), wrap `0x812F` (CLAMP_TO_EDGE), blend
  `src=0x302 SRC_ALPHA / dst=0x1 ONE` (additive), depth_write=0.
- The SoH side: `Zelda3DRenderer::uploadTexture` generates a synthetic FULL mip chain for
  every CMB texture, and `getSampler` used `max_lod=1000` + trilinear for every model draw.
  For minified terrain that chain is a deliberate antialiasing enhancement (#134 moiré). But
  the star dome is a sparse-bright-texel ADDITIVE texture: at the dome's rendered LOD, the
  mip average blends each isolated bright star texel into its near-black neighbors — crushing
  the PEAK while roughly preserving the local AVERAGE. That is precisely the measured
  signature: peak 0.73–0.80x of oracle, integrated ~1.03x
  (`2026-07-10-title-three-residuals-remeasure.md` §4).

### Fix (mechanism-level, no brightness constant)

`getSampler(wrapS, wrapT, noMip)` — new flag forces `max_lod=0` (sample only the real,
single native level, matching the 3DS's actual single-level texture). Set for draw groups in
the additive point-sprite class: `blendEnable && src==SRC_ALPHA(0x302) && dst==ONE(0x1)`
(`isAdditiveBlendGroup`, zelda3d_sdl3gpu.cpp) — the star dome's own material bytes, applied
by CLASS not by model name so sparkle-type VFX materials get the same correct treatment.

### Verification (matched pair az=200 / soh=608, tools/title_star_luminance.py, y=[80,120] band)

|            | peak ratio SoH/Az | integrated ratio |
|------------|-------------------|------------------|
| before fix | 0.744 (0.73–0.80 across sessions) | 1.027 |
| after fix (pack off) | **0.970** | 1.345 |
| after fix (pack on)  | 1.082 | 1.031 |

Peak is restored to ~1.0x — the footprint mechanism is closed. NOTE the pack-off integrated
1.345 / n_bright 1797-vs-812 overshoot is NOT a star regression: SoH's night-sky FLOOR in the
band is 75 vs Az 57 (the still-open v2 residual 3, night-sky R/G brightness — a sky-dome
gradient issue), and the excess-above-floor integral inflates when the underlying sky
gradient is brighter/steeper. The star-specific metric (peak at matched frames) is at parity.

## 2. Copyright scale (v2 residual 6) — root cause chain: fitted constants stacked on a fitted constant

### What the measurement showed

`tools/title_copyright_bbox.py` (new, committed) at the matched display-phase pair az=1000 /
soh=1408, pack off: copyright ink bbox SoH 136x32 px vs Az 149x30 — width 0.913x, height
1.067x, center off by (-11.5, -13.5) px. Non-uniform w/h error = not a placement nudge but a
scale-derivation defect.

### Root cause (decomp §6.1/§6.4 vs the port)

The 3DS logo actor composes ALL THREE overlay elements (wordmark, g_title backdrop/glow,
copy_nintendo) with ONE shared camera-facing basis at local translate (0,0,-34) /
(0,0,-33.99) / (0,-11,-34), rendered through the scene's LIVE perspective projection
(title_logo_actor.md §6.4, placement literal 0x001da8a4 = -34.0f). The exact ortho-pass
equivalent is:

    pxPerLocalUnit = (refH/2) / (tan(liveFovY/2) * 34)
    element origin = screen center (+ the copyright's own -11 basis-Y units = DOWN on screen)
    element size   = pxPerLocalUnit * its OWN geometry

The port instead had THREE independent fitted constants: kHeightFrac=0.55 (wordmark, from an
az1000 color-mask bbox), kCopyrightHeightFrac=0.117 (its own mask bbox), and the fire-glow
squeezed g_title to the WORDMARK's pixel height. Every one disagrees with the derivation:
the oracle's live compose at this frame's fov (~44.6°) gives pxPerUnit ≈ 8.44, i.e. wordmark
161 px (mask-measured Az ink: 161), copyright quad 43.4 px — while the port drew at 6.91
px/unit (wordmark) and an unrelated 5.46 (copyright) and 4.76 (glow).

Implemented in `title_logo.cpp` (`kOverlayComposeDepth`, `Zelda3D_TitleOverlayPxPerUnit(play)`
reading `play->view.fovy` — set per-frame from the ported OP97 cs spline, verified 0.00 vs
Az) and shared by all three elements (wordmark + copyright call sites, `title_fireglow.cpp`).
The fitted constants kCenterXFrac/kCenterYFrac/kHeightFrac/kCopyrightHeightFrac and the
`Zelda3D_TitleWordmarkPlacementFracs` accessor are DELETED — zero fitted placement/scale
constants remain in the title overlay.

### Live-verified compose (ZELDA3D_DBG_OVERLAY_MP=1, new env-gated dump in interpreter.cpp)

At the capture frame: ortho P diag (0.004990, -0.008331) = the exact 400x240 box; all three
elements' MV scale 8.4373 = the fov-derived pxPerUnit; copyright MV translate
(200.00, 212.81) = center + 11*8.4373. The GPU receives exactly the derived compose.

### Verification (az=1000 / soh=1408, pack off, restricted-mask bbox)

|            | bbox w | bbox h | center |
|------------|--------|--------|--------|
| Az oracle  | 149    | 30     | (206.5, 210.0) |
| SoH before | 136 (0.913x) | 32 (1.067x) | (195.0, 196.5) — off (-11.5,-13.5) px |
| SoH after  | **147 (0.987x)** | **30 (1.000x)** | **(206.5, 209.0)** — off (0,-1) px |

Line-spacing cross-check (threshold-robust luminance-centroid feature): SoH 17.7 px vs Az
17.9 px (0.989). Overall frame content score at this pair: 0.4305 (v2) → **0.6954**.

### Trap logged: texture-pack contamination of geometry measurements

One intermediate A/B ran with the hi-res pack ON and produced a phantom "SoH draws 0.72x of
commanded" paradox (correct matrices on the GPU, wrong measured ink) — the pack's REPLACEMENT
copyright texture ("©1998-2011 NINTENDO / 4K TEXTURES BY HENRIKO") has a different ink
layout, so ROM-texel-fraction analysis doesn't apply to it. Every geometry-from-ink
measurement MUST run `ZELDA3D_TEXPACK=off` (v2 residual 7's rule, now proven to bite
geometry too, not just color).

## 3. Fire-glow (v2 residual 4) — substantially narrowed by the shared-basis fix

g_title.cmb is 27.71 local units tall (authored to wash OVER the 19.14-unit wordmark). The
old code drew it at the wordmark's own pixel height (4.76 px/unit vs the wordmark's 6.91) —
shrinking the glow footprint ~31% vs the shared-basis compose. With the derivation fix the
glow draws at its authored relative size. Additive-delta re-measure
(`fireglow_ab.py --diff`, pack off, cs=530/az=884):

|            | R ratio soh/az | G | B | coverage px |
|------------|----------------|---|---|-------------|
| before (cs 525) | 0.372 | ~0.42 | ~0.66 | 20878/23821 = 0.88 |
| after  (cs 530) | **0.860** | **1.02** | 0.60 | 22115/24103 = **0.92** |

Still open (B channel 0.60, R 0.86 ≠ 1.0) but the dominant footprint term is gone; the
remaining candidates stay texture-decode/alpha-staging per the prior session's narrowing.

## v2 residual status updates

- Residual 8 (star brightness): **RESOLVED** — mip-blur footprint mechanism, peak 0.744 → 0.970.
- Residual 6 (overlay/copyright scale): **RESOLVED** — full decomp perspective-compose
  derivation replaced all fitted placement constants; copyright bbox 0.987/1.000 of oracle,
  center within 1 px.
- Residual 4 (fire-glow): narrowed 0.37 → 0.86 (R), coverage 0.88 → 0.92; still open.
- Residual 3 (night-sky R/G): unchanged/open — now also identified as the confound behind the
  star band's integrated-luminance overshoot (§1).

## Conditions

- 438/443 lus_tests pass (5 pre-existing asset-gated skips), zero failures.
- All measurements: harness headless (Xvfb :99, SOH3D_HARNESS_HEADLESS=1),
  `ZELDA3D_TEXPACK=off`, matched pairs via tools/title_ab.py (soh = az + 408).
- New tools: `tools/title_copyright_bbox.py`; new env diagnostics: `ZELDA3D_DBG_OVERLAY_MP`.
- Captures in scratch/title_ab/ (machine-local, never committed).
