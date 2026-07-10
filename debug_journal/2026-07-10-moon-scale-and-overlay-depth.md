# Title moon fixed scale (oracle-anchored) + overlay-pass depth scope (shield occlusion)

Two independent mechanisms from the same session; each verified element-masked against the
Azahar oracle at content-matched frames (tools/title_ab.py, ZELDA3D_TEXPACK off — the tool
never enables it).

## Item A — title moon: fixed scale, derived honestly (NOT guessed)

Ground truth (`<oot3d-decomp>/docs/title_moon.md`): OoT3D's title moon scale is a FIXED
per-draw vertex-shader uniform (disc diagonal 640, halos 1280, exact 2:1) with **no dayTime
dependence**. SoH's `scale = -15*color + 25` in `Zelda3D_TryDrawSunMoon` (zelda3d.c) was an
N64 carry-over with no decomp anchor; the prior session
(`debug_journal/2026-07-10-title-moon-reimplementation.md`) validated the fixed-scale
MECHANISM (az=1522 moon delta collapsed to (−1,−4,−2)) but its guessed constant 10.0
regressed az=200 and was reverted, leaving §5's derivation plan.

### Derivation (option 2 of the plan: oracle-matched live readback)

Added a one-shot readback to the embedded harness — `SohState_MoonDebug`
(tools/soh3d_harness/soh_state.cpp) reads the live `gPlayState->envCtx.sunPos.y` and
recomputes the moon formula verbatim; harness command `soh_moon` (main.cpp) prints it.
At the pre-fix-matching calibration frame (az=200 / soh=608, the frame where the OLD formula
already matched the oracle, d≈(−2,−7,+12) this session):

```
ok soh_moon sunPosY=-1195.9265 color=0.3986 scale=19.0204 discScale=9.6053
```

So the value the old formula produced at the oracle-matched frame is **scale = 19.0204** —
that is the oracle-anchored fixed constant (`kMoonTitleFixedScale = 19.0204f`), not a fit.
The elevation formula is replaced by this constant on the title path
(`Zelda3D_Title_IsActive()`); the non-title gameplay path keeps the N64 formula (the decomp
sessions traced title-specific code only). 3-layer ctxb composite, `kMoonDiscScale=0.505`,
`kMoonDiscAlpha=205` STOPGAP, `kMoonHaloScale=2.0` all unchanged.

### Verification — element tables, moon region (300,0)-(400,80), ZELDA3D_TEXPACK=off

NOTE on the texpack: the hi-res pack AUTO-ACTIVATES from `<ROM dir>/textures/` when
`ZELDA3D_TEXPACK` is unset (texpack.cpp findPackRoot) — measurement runs must explicitly
`export ZELDA3D_TEXPACK=off`. This session's first capture round ran pack-ON by accident;
the pack-off rerun below is the compliant record (the pack-on A/B told the same story, and
notably the pack-off post-fix values reproduce the HISTORICAL baselines exactly, confirming
the prior sessions' tables were pack-off).

az=200 / soh=608 (calibration frame — must not regress):

| build | az mean RGB | soh mean RGB | d (az−soh) |
|---|---|---|---|
| pre-fix baseline (prior sessions' match) | (143,140,132) | (144,145,120) | (−1,−5,+12) |
| post-fix (texoff_200) | (143,140,132) | (144,145,120) | **(−1,−5,+12)** |

Identical to the pre-fix match — expected, since the constant IS the old formula's output at
this exact frame (that's the derivation).

az=1522 / soh=1930 (the target residual — v4 journal attributed d=(−65,−67,−51) here to the
moon; the validated mechanism produced (−1,−4,−2)):

| build | moon region d (az−soh) |
|---|---|
| pre-fix (v4 journal attribution) | (−65,−67,−51) |
| validated-mechanism reference (reverted 10.0 build) | (−1,−4,−2) |
| post-fix (texoff_1522) | **(−1,−4,−2)** |

Both frames pass exactly. Captures: `scratch/title_ab/texoff_{200,1522}*` (gitignored);
pack-on round in `scratch/title_ab/postmoon_*`.

The `soh_moon` harness command + `SohState_MoonDebug` are left in place (commented TEMPORARY)
— they cost nothing and make the next moon calibration a one-command readback; the in-game
debug print route was never needed.

## Item B — overlay pass: own depth scope (shield/sword occlusion)

Attribution (`debug_journal/2026-07-10-shield-sword-attribution.md` §5):
`title_logo_us.cmb`'s own geometry puts shield+sword BEHIND the "ZELDA" letters (model Z
−6.3..−9.7 vs letters −5.0..−5.6); the oracle depth-tests, so the shield is mostly occluded.
SoH's `Zelda3D_Overlay2D_Begin` disabled the Z-buffer for the pass, so raw CMB submit order
(letters, THEN shield) let the shield paint over the letters.

### Mechanism — TWO pieces were needed, and the second was the actual blocker

**Piece 1 (per the task brief): overlay depth scope.** New no-operand dlist opcode
`G_ZELDA3D_CLEARDEPTH` (0x4b, gbi.h `gSPZelda3DClearDepth`), emitted once by
`Zelda3D_Overlay2D_Begin`. The interpreter handler
(`gfx_zelda3d_cleardepth_handler_custom`) flushes Fast3D's pending batch and bridges via
`Zelda3D_ClearOverlayDepth` (zelda3d_gl.cpp shim) into
`Fast::Zelda3DRenderer::ClearOverlayDepth` (zelda3d_sdl3gpu.cpp): a fullscreen depth-only
draw appended as a regular in-pass op — depth compare ALWAYS + depth write ON, fragment
writes `gl_FragDepth = 1.0` (far), `color_write_mask = 0` so the already-composited 3D
scene's color is untouched. No render-pass split, no per-mesh sorting, no material changes.
Safe unconditionally: by Overlay2D_Begin the 3D scene is already fully in the COLOR buffer.
CMB depth-write flags stay as authored (letters/shield/sword mats have depth_write=true;
the banner-text blend mats false — verified by direct parse of title_logo_us.cmb +0x135).

**Piece 2 (found this session — the attribution journal's §5 framing was incomplete): the
overlay's depth SENSE was inverted.** The SG renderer's model pipelines ALWAYS depth-test
(getPipeline: LESS_OR_EQUAL) — the overlay pass's `gSPClearGeometryMode(G_ZBUFFER)` only
affects Fast3D triangles, not Zelda3D model draws. So pre-fix the shield was not merely
"last-drawn wins": it legitimately PASSED the depth test in front of the letters, because
`Zelda3D_Overlay2D_PlaceModel`'s fixed 180° X rotation (the Y-up→Y-down flip) also negates
model Z, mapping modeled-BEHIND (shield z −6.3..−9.7) to NEARER clip depth than the letters
(−5.0..−5.6). Proof: the depth-clear-alone build measured UNCHANGED (npix 2485 vs pre-fix
2402 — red test below). Fix: pass the overlay ortho's near/far REVERSED —
`guOrtho(0, refW, refH, 0, +1000, −1000, 1)` — which re-flips clip z (m22 = −2/(far−near) =
+1/1000, so clip z = −z_model/1000: letters 0.005 nearer than shield 0.0097) and restores
the authored depth order exactly as the oracle's own projection sees it. One line in
Zelda3D_Overlay2D_Begin; every overlay consumer inherits it.

### Verification — az=1000 / soh=1408

Shield-blue mask metric (`scratch/title_ab/shield_bbox.py` — saturated B-dominant pixels in
the central logo area, same blue-mask approach as the attribution session):

| build | bbox | npix |
|---|---|---|
| oracle (az frame, any build) | 148x88 (incl. some sky noise) | 1619 |
| SoH pre-fix (skeptic_1000.soh) | 112x109 | 2402 |
| SoH depth-clear ONLY (red test, postoverlay_1000) | 126x109 | 2485 — no change, proves the inversion |
| SoH depth-clear + ortho z flip, texpack off (texoff_1000) | **100x80** | **1310** |

Post-fix bbox 100x80 vs the attribution session's calibrated-crop oracle target ~85x77
(their tighter metric); by the shared mask metric SoH went from +48% over oracle to −19%
under (slivers visible through letter gaps, matching the oracle's look). Visual
(`scratch/title_ab/texoff_1000_sxs.png`): letters now occlude the shield, sword tip
(bottom-left) + hilt (top-right) show through exactly like the oracle; banner text, fire
glow, copyright all intact; no z-fighting. Whole-frame content score 0.7106 (pre) → 0.8044
(pack-on post) / 0.8318 (pack-off post) — no regression anywhere in the 4x3 grid; remaining
deltas are the KNOWN separate residuals (shield-face oversaturation + dark-square artifact,
flagged open in the attribution journal, untouched by this task).

## Files

- `Shipwright/soh/src/zelda3d/zelda3d.c` — kMoonTitleFixedScale (title path), formula kept
  for gameplay.
- `tools/soh3d_harness/soh_state.cpp`, `tools/soh3d_harness/main.cpp` — `soh_moon` readback.
- `Shipwright/libultraship/include/libultraship/libultra/gbi.h`,
  `Shipwright/libultraship/include/fast/lus_gbi.h` — G_ZELDA3D_CLEARDEPTH (0x4b).
- `Shipwright/libultraship/src/fast/interpreter.cpp` — opcode handler.
- `Shipwright/libultraship/src/fast/zelda3d_gl.cpp` — Zelda3D_ClearOverlayDepth shim.
- `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp`,
  `Shipwright/libultraship/include/fast/{zelda3d_sdl3gpu.h,backends/zelda3d_sdl3gpu.h}` —
  ClearOverlayDepth pipeline (fullscreen depth reset, color mask 0).
- `Shipwright/soh/src/zelda3d/zelda3d_overlay2d.cpp` — emits gSPZelda3DClearDepth in Begin.
