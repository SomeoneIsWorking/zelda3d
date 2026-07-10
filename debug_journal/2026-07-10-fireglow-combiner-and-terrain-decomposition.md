# 2026-07-10 — fire-glow real combiner port + terrain-2x analytic decomposition

Two closures from the title arc's open residuals (closing-measurement doc residuals 4 and 2).

## 1. Fire-glow: real 3-stage TEV combiner ported (residual 4)

Ground truth: `<oot3d-decomp>/docs/title_logo_fireglow_cmab.md` §3.1/§3.2 — g_title.cmb
material 0 is:

```
stage0.rgb = (efc + mableT) * efc          # ADD_MULT dual-texture: g_title_mable_t (64x64,
                                           # binding 1, coordinator-1 scale(3,2)/trans(0,0.9433))
                                           # ADDS into the flame gradient before the self-multiply
stage1.rgb = 2.0 * (stage0 * constColor0)  # MODULATE at hardware scaleRGB=x2; constColor0 is the
                                           # register the CMAB gold-flicker curve animates
stage2     = passthrough
```

SoH's port computed only `stage1 at x1` on `efc` alone: a quantified 2x gain gap plus a
missing brightening term. Both are now first-class renderer features, not fireglow-specific
hacks:

- **Parser** (`cmb3d/asset/cmb.cpp/.h`): texture binding 1 (`tex1_idx`, wrap), coordinator-1
  UV transform (`scale1_s/t`, `trans1_s/t`), the CONST-stage's own hardware RGB scale
  (`comb_const_scale_rgb`), and the dual-tex stage-0 detection
  (`comb0_dual_addmult` = ADD_MULT(TEX0,TEX1,TEX0) with a live binding 1).
- **Renderer** (`zelda3d_sdl3gpu.cpp` + backend): third fragment sampler `uTex1`
  (set=2,binding=2; dummy-backed when unused), new UBO field `uTex1Xf` (coordinator-1
  scale/trans; dual-tex enable rides `uSheen.y`), vertex-shader `vUv1 = scale*(uv - trans)`
  per noclip calcTexMtx (DccMaya, rot=0), fragment stage-0
  `t.rgb = clamp(t0+t1)*t0`, and the CONST modulate now multiplies by
  `uMatConst.rgb * uMatConst.a` where **`.a` carries the stage's hardware scale**
  (0=off, 1/2/4=apply x scale). Per-draw CMAB UV-scroll routes to coordinator 1 for
  dual-tex groups (the CMAB Translation track is channelIndex 1 = mableT's coordinator),
  coordinator 0 for the single-tex scroll consumers (sky cloud band) — fix 3 of the doc.
- **Behavior** (`title_fireglow.cpp`): unchanged mechanically — the CMAB color still rides
  the flat draw tint (mathematically identical: baked constColor0 is white, verified), the
  V-track still rides the uvV arg. Comments rewritten to the confirmed ground truth
  (mableT is NOT unused; ura.ctxb hypothesis superseded per title_ura_ctxb_identified.md §3).

### BUGFIX uncovered en route: constant-color palette read one slot late

`cmb.cpp` read the 6-slot PICA constant palette at material `+0xB8`; the real base is
**`+0xB4`** (noclip readMatsChunk + direct byte dump of g_title.cmb/fine_star.cmb). Every
baked palette was shifted down one slot:

- g_title.cmb constColor[0] (255,255,255,255) read as black,
- fine_star.cmb constColor[0] (255,255,127) — a real star tint — read as black, which is
  the misread the shader's "constBlack skip" heuristic (task #16) was built around.

Fixed to +0xB4. ROM-wide sweep: 686 const-sourcing materials, 149 flip from
"skip (black)" to "apply the real baked color" — the faithful direction (biri_biri,
bv effects, ddg_fire, kekkai, etc. gain their authored tints). Runtime override consumers
(EnHy/EnDog/EnMu) are unaffected (they overwrite the slot; selector +0x24 parse untouched).
Spot-check: Hyrule field night scene renders normally
(scratch/screenshots/fireglow_regression_check.png); all lus_tests pass, including a NEW
real-asset close-test `CmbCombinerParse.TitleGlowDualTexAddMultAndConstScale` locking the
palette base, the x2, the dual-tex detection and the coordinator-1 transform.

### A/B vs oracle (tools/fireglow_ab.py — new tool, one harness session, matched frames soh=az+408)

Gold-hue flame mask (R>60, 0.3R<G<0.9R, B<R/2) in the logo box x[110,300] y[40,190]:

| pair (az/soh) | cs | Az mean RGB (px) | SoH BEFORE (px) | SoH AFTER (px) |
|---|---|---|---|---|
| 730/1138 | 453 | (90.8, 31.5, 25.5) (3982) | (143.9, 69.8, 59.6) (169) | (143.9, 69.8, 59.6) (169) |
| 936/1344 | 556 | (200.6, 147.4, 41.5) (4283) | (123.8, 90.5, 24.0) (1906) | (160.9, 89.8, 11.7) (1977) |
| 1100/1508 | 638 | (201.4, 142.6, 48.6) (4107) | — | (168.2, 94.7, 17.6) (1822) |

- At 936/1100 the glow's R mean rose ~30% toward the oracle (124→161/168 vs Az 201) — the
  x2 + dual-tex are live (B also drops toward the CMAB's B=0 as the real modulate engages).
- **Residual (still open, honest):** (i) intensity still ~0.8x of Az's mean and coverage
  ~45% of Az's flame-pixel count — Az's wash is larger and saturates more; candidate: the
  glow's own alpha channel (+0x1D0 staging) reaching 255 later/lower than the oracle, since
  additive src=SRC_ALPHA scales the whole term. (ii) at az=730 (cs 453, ~108 cs-frames after
  the logo trigger) Az already shows a full flame wash while SoH's glow hasn't STARTED
  (identical 169 stray px before/after = wordmark edge pixels, zero glow contribution) — the
  fade-in staging of THIS element starts too late vs the oracle. Both are timing/alpha-channel
  items in title_logo_actor.md §5.2/§6.2 territory, not combiner-gain items; the combiner
  math itself now matches the doc.

## 2. Terrain 2x: analytic single-pixel decomposition (residual 2) — VERDICT: oracle side

Setup: pixel-aligned pair az=500/soh=908 (established by the prior session). New tool
**`tools/terrain_pixel_decompose.py`**: raycasts spot99 room-0's actual ROM geometry
(Möller–Trumbore over all 29 meshes' triangles, positions/UVs/vertex-colors read straight
off the CMB) from the LIVE camera pose (harness `compare camera`: eye/at/up byte-matched
between engines, |Δeye|<0.01; fov 45.40°), recovers the exact texel (bilinear, ROM decode),
baked vertex color, and live ambient (`compare lighting`: ambient=(43,63,116)/255,
identical on both engines) behind chosen screen pixels, computes the decomp formula
`expected = saturate(2·texel·vColor·ambient)`, and compares with BOTH rendered panes.

### (a) Texpack confound — RULED OUT

Added an explicit disable (`ZELDA3D_TEXPACK=0|off|none` in `texpack.cpp` — there was no off
switch). Re-measured the az=500/soh=908 grid with the pack off: ratio unchanged
(e.g. region (0,0,100,80): Az(36,60,23) vs SoH(19,31,12) — 1.9x; pack-on was SoH(18,28,12)).
The 2143-texture pack shifts terrain means by ≤ ~5%; not the factor.

### (b) Single-pixel decomposition — SoH is formula-EXACT; the oracle is ~1.9x ABOVE it

Example pixels (dist < 100 world units — inside fogNear=996, fog contribution 0):

| pixel | texel RGB | vColor | ambient | expected sat(2tva) | SoH px | Az px |
|---|---|---|---|---|---|---|
| (100,210) | (128.0,150.5,30.5) | (0.465,0.473,0.450) | (0.169,0.247,0.455) | (20.1,35.2,12.5) | (20,35,12) | (45,79,39) |
| (200,220) | (110.8,127.8,20.8) | (0.465,0.473,0.450) | " | (17.4,29.8,8.5) | (17,29,8) | (31,53,12) |
| (320,205) | (141.2,157.2,50.2) | (0.465,0.471,0.449) | " | (22.2,36.6,20.5) | (21,35,18) | (31,53,8) |

Statistical version, 400 random near-ground pixels (t<900):

- mean expected (19.9, 32.0, 13.1) vs **SoH (19.9, 32.0, 13.1)** — per-channel mean |err|
  0.32/0.40/0.56 out of 255. **SoH implements saturate(2·t·v·a) to sub-LSB precision.**
  (This simultaneously validates the raycast/UV/texel/vertex-color/ambient chain end to end.)
- **Az (37.6, 61.8, 24.2) = 1.89x / 1.93x / 1.85x the formula** — channel-uniform ~1.9x on
  region means. Per-pixel the Az/exp ratio is noisy (std ~0.3; log-log slope ≈ 0.1-0.26,
  i.e. Az's per-pixel response is much flatter than the formula's — 3DS-native filtering +
  upscale smoothing; the REGIONAL means are the meaningful statistic). Neither a pure
  constant multiplier per pixel nor a single pure gamma curve fits the per-pixel cloud;
  the region-mean factor is ~1.9 with slight sub-2 depression consistent with ~5% fog mix
  toward the dark fog color (8,6,32).

**The identified factor: the ORACLE applies close to one extra x2 the decomp's formula does
not contain.** Not SoH's output path (SoH == formula exactly), not texpack, not ambient
(byte-matched live), not vertex colors or texel decode (validated above). Leading
candidates for the decomp stream, with these numbers: a second x2 stage scale in the real
combiner chain for these scene materials (i.e. effective sat(4·t·v·a)), a doubled
vertex-color or light-sum term in the PICA vertex-lit path, or an output-stage transfer
curve in Azahar's own present path (the per-pixel flattening hints some nonlinearity is
ALSO present). Reported back to the decomp stream (note appended to
`<oot3d-decomp>/docs/title_env_lighting.md`); per the stop-micro-tuning directive, NO
constant was fitted in SoH.

## Files

- `Shipwright/cmb3d/asset/cmb.{h,cpp}` — binding-1/coordinator-1 parse, const-stage scale,
  dual-tex detect, palette base +0xB8→+0xB4 fix.
- `Shipwright/cmb3d/asset/cmb_glgroups.cpp`, `libultraship/include/fast/zelda3d_gl.h` — plumb.
- `Shipwright/libultraship/{include/fast/zelda3d_sg_ubo.h, include/fast/unified_ubo.h,
  include/fast/backends/*.h, src/fast/zelda3d_sdl3gpu.cpp, src/fast/backends/gfx_sdl3gpu.cpp,
  src/fast/backends/unified_shader.cpp}` — uTex1 sampler, uTex1Xf, const-scale semantics,
  3-sampler model draws.
- `Shipwright/cmb3d/asset/texpack.cpp` — ZELDA3D_TEXPACK=0|off|none disable.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_fireglow.cpp` — comments to ground truth.
- `Shipwright/libultraship/tests/{zelda3d_render_tests.cpp, cmb_combiner_parse_tests.cpp}` —
  UBO offsets updated; new g_title close-test.
- `tools/fireglow_ab.py`, `tools/terrain_pixel_decompose.py` — new measurement tools.
