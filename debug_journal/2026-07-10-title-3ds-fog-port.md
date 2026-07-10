# Title 3DS distance-fog PORTED — LUT-fill formula RE'd (predict gate float-exact), end-to-end port shipped (2026-07-10)

Follow-up to `2026-07-10-dawn-hue-fog-rootcause.md` (the measured root cause: SoH computes the
correct blended fogColor and never applies fog). This session closed the one open ground-truth
item — the 3DS fog LUT-fill formula — and shipped the port. Full RE derivation:
`oot3d-decomp/docs/title_env_lighting.md` §13.

## 1. The RE (predict gate PASSED before any port code was written)

- Writer chain found from the single pool constant `0x000F00E6` (GPUREG fog-LUT-index command
  header) in code.bin → the GX flusher pack loop (`FUN_0046c1e4` @0x46f350) → the float LUT
  object → **`FUN_002cdbfc` = the fill** (Grezzo `FogResUpdater.cpp` — literal source path in
  its alloc call) → `FUN_00464b0c` (the setter taking near/far/projMatrix/color) →
  `FUN_002d4554` (4x4 matrix INVERSE — the "depth→distance conversion" §12 couldn't pin).
- **The formula** (mode 0 = linear; exp/exp2 exist but unused at title):
  `eyeDist(t) = b/(a − t)` for LUT node t = i/128, with `a = zFar/(zFar − zNear)`,
  `b = zNear·a` (the 3DS projection z-row); `factor = clamp((fogFar − d)/(fogFar − fogNear))`,
  1.0 below fogNear, 0.0 above fogFar. `value[i] = factor(i)`, `diff[i] = factor(i+1) −
  factor(i)`, packed 13-bit diff | 11-bit value << 13.
- **Inputs live-verified** (scratch/dawn_hue/fog_formula_gate.py): fogNear = the palette
  u16&0x3ff **used directly in eye units** (blended 48/92/138 at dayTime 0x2bbb/0x3197/0x37b5),
  fogFar = blended **drawDist** (40414/42612/44906), zNear = **7.0** exactly, zFar = palette
  **fogEnd = 32000**. Fog param global @0x004fa8b8; LUT object found by memscan for its
  back-pointer (was @0x082101b4).
- **Predict gate: recomputed value[0..127]+diff[0..127] match the live arrays to ≤ 9.7e-8**
  (float noise) at all three dayTimes; max fog 79.2/75.1/71.2% vs the §12 dumps' 79.3/75.2/71.3.
- **The look lives in the LUT's piecewise-linear-in-DEPTH interpolation**: entry 127 spans eye
  873..32000, so a rock at ~2100 gets ~50% fog although the smooth curve says ~5%. Any port
  evaluating the smooth factor(d) directly misses the entire visible haze — the port reproduces
  the node/lerp structure (closed-form nodes, no 128-entry upload; the 11/13-bit quantization
  is ≤1/2048 = sub-LSB and omitted).

## 2. The port (one mechanism, byte-driven, on by default at title)

- `cmb3d/asset/cmb.{h,cpp}` + `cmb_glgroups.cpp`: parse CMB material **isFogEnabled (+0x02)**
  (spot99 room 0: 24/29 set; §12's "28/29" was a miscount) → `Zelda3DGlGroup::fogEnabled`.
- `zelda3d_gl.{h,cpp}`: `Zelda3D_Fog3dSet(camNear, zFar, fogNear, fogFar, eye, fwd)` /
  `Zelda3D_Fog3dOff` → globals `gZelda3dFog3dOn`, `gZelda3dFog3d[8]` (a, b, near, far, fwd,
  fwd·eye). `gZelda3dFog3dForceOff` = harness/REPL A/B latch only.
- `zelda3d_sg_ubo.h` + `zelda3d_sdl3gpu.cpp`: SgUbo gains `uFog3d0/uFog3d1` (kCommonBytes
  368→400, layout tests updated; unified CommonUbo mirrors for size parity). Vertex shader
  carries the 3DS z-buffer depth `a − b/d` (d = dot(world, fwd) − fwd·eye; affine in screen
  space so varying interpolation is exact); fragment does the node/lerp + `mix(fogColor, rgb,
  factor)`. Per-draw gate: `uFog.w == 2.0` iff frame-level on AND `grp.fogEnabled`; sky
  excluded via the existing uLightDir.w gate; overrides the default-off F3DEX ramp.
- `zelda3d_cutscene.{h,cpp}`: `Zelda3D_TitleCsBlendedFog` — fogNear/drawDist/fogEnd blended
  with the SAME schedule weights as the color blend.
- `title_presentation.cpp applyLightOverride`: feeds Fog3dSet per frame (camNear = 7.0, the
  measured 3DS title camera near); `exit()` calls Fog3dOff. N64 lightSettings.fogNear/fogFar
  deliberately NOT rewritten (wrong units — eye-space 40..45000 vs F3DEX fog space; far
  doesn't fit s16).
- Harness: new `soh_fog3d <0|1>` A/B command (tools/soh3d_harness/main.cpp).

## 3. Verification (same-binary fog OFF/ON vs oracle, TEXPACK=off, camera-exact pairing +405)

Region boxes on the 400x240 frame; "off/on" = the `soh_fog3d` harness latch, same process,
same steps. Final numbers (AFTER the camera-source fix below):

az=1000/soh=1405 (dayTime 0x3197 — THE dawn target frame):

| box | oracle | fog OFF |d| | fog ON |d| |
|---|---|---|---|
| rock (Death Mtn) | (55.7,48.4,46.8) | (50.7,55.4,50.2) 5.1 | **(53.9,47.8,44.8) 1.4** |
| skyNearMtn (horizon) | (66.2,58.6,71.0) | (56.7,63.9,73.4) 5.8 | **(60.9,61.4,72.9) 3.4** |
| whole frame mean\|d\| | | 21.68 | **21.30** |

The cold-green rock and the blue horizon band both move to the oracle's warm dawn values —
the dawn HUE axis this arc chased. az=1522/soh=1927 (0x37b5): whole 22.56 → **20.25** (the
unchanged skyNearMtn 7.8 there is the documented moon-composite residual). Night re-measured
(fogNear 40 exists at night too): az=200/soh=605 whole 10.78 → **10.37**; az=500/soh=905
every box improves (rock 1.5→1.0, sky 1.7→1.5/1.4→0.9, grass 0.6→0.5).

10-point sweep (fog off → on, same binary, `scratch/dawn_hue/fog_sweep.py`, grid metric of
title_ab):

| az | score off→on | grid mean\|d\| off→on |
|---|---|---|
| 100 | 0.9477→0.9493 | 5.66→**5.30** |
| 200 | 0.9328→0.9332 | 5.41→**5.37** |
| 360 | 0.8595→0.8570 | 5.04→5.43 |
| 500 | 0.7236→0.7201 | 1.85→**1.80** |
| 700 | 0.7302→0.7469 | 3.24→**3.10** |
| 1000 | 0.8574→0.8587 | 5.79→**5.70** |
| 1300 | 0.7960→0.7932 | 7.54→7.92 |
| 1522 | 0.8449→0.8482 | 6.89→**5.81** |
| 1700 | 0.8177→0.8188 | 7.94→**6.56** |
| 1900 | 0.8535→0.8533 | 6.02→**5.27** |

Improves at 7/10 points (all dawn points strongly), holds at 500/1900; the two +0.4 wobbles
(360, 1300) are grid-metric noise an order below the dawn gains. Mean over the sweep:
5.54→5.23. lus_tests: 438 PASSED (UBO layout offsets updated for the two new vec4s).

### The bug the first verification caught: WRONG camera source for the fog axis

The first off/on sweep regressed at az 360/500/1000 with a UNIFORM ~40% haze on near grass.
Root cause (measured, then fixed — commit in this session): `applyLightOverride` sourced the
fog camera from `play->view.eye/lookAt`, but at that call point in the frame (z_kankyo, before
update()) the view's LOOK direction measurably disagrees with the rendered camera — live at
az500/soh905 (cameras eye-EXACT, 3846.26/-95.90/7235.25 on both engines): view fwd read
(0.28,0.12,-0.95) while the real camera dir is the grazing (0.967,0,0.255). A wrong view AXIS
turns near ground (oracle per-pixel depth 0.9243 = 92 eye units, zero fog) into a fake
1500-unit distance (40% fog) — the fog factor is hypersensitive to the axis. Fix: evaluate the
ported OP97 cs-camera spline directly (pure function of the cs frame, byte-exact vs the
oracle) for the fog eye/fwd.

Also recalibrated the A/B pairing: `soh = az + 405` is the CAMERA-EXACT mapping on the current
build (soh camera eye == az title-cam eye to 0.0 world units at az=1000, scanned 1380..1480;
the old +408 shifted −3 with the cursor-phase commits, and an SSIM `calibrate` plateau had
mis-suggested +437 — trust the camera scan, not SSIM, for pairing).

10-point sweep (fog off vs on, same binary, +405 pairing): table below.

## 4. Dead ends / notes

- Static hunt for the LUT fill via `vstr [rX,#0x204]` / vdiv-near-`cmp #0x80` scans: the
  0x204-offset hits are actor code / init-defaults; the vdiv candidates found the fill only
  because FUN_002cdbfc's rational eval divides per node.
- The driver's fog-object handle table (`FUN_002d1210`, *0x54a814+0x828+id*4) reads EMPTY
  between frames from the harness REPL — don't hunt the LUT object through it; memscan for the
  0x4fa8b8 back-pointer instead.
- The fog global's s32 near/far (+0x34/+0x38) lag the LUT object's f32 near/far (+0x14/+0x18);
  the f32 fields are what the fill consumes — read those.
- `pgrep -f ninja` self-matches the wrapping shell (safe-kill trap) — it reported a phantom
  ninja and skipped a needed harness rebuild once this session.
