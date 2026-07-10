# Dawn HUE axis root-caused: PICA distance fog toward the blended palette fogColor (2026-07-10)

Target (task brief): at az=1000/soh=1408 and az=1522/soh=1930 the oracle's sky is warm
purple-pink and its distant rock warm reddish; SoH's are cold blue/green. Known-good going in:
dayTime phase-exact (`b4d55be2`), dome schedule ported (`efb70276`), light palette blend
verified, terrain formula formula-exact at az=500.

**Verdict: the diverging input is the PICA distance-fog stage — SoH computes the correct
blended fogColor every frame and then never applies fog at all.** Full derivation + ground
truth in `oot3d-decomp/docs/title_env_lighting.md` §12. This journal holds the SoH-side
numbers and the port surface. No fix was shipped this session (the exact fog CURVE still
needs the 3DS LUT-fill decompile — a fitted curve would violate the no-magic-constants rule);
this is the data-backed partial the task brief's stop condition allows.

## What was measured EQUAL (both target frames, live A/B via soh3d_harness)

| input | oracle | SoH | method |
|---|---|---|---|
| dayTime | 0x3197 / 0x37b5 | same, 0-delta | `az_daytime` vs `soh_env` |
| effective terrain ambient | (60,74,100), (79,87,83) — duplicated into amb0+amb1 | (61,75,100), (79,87,84) | `vsuni_log` c82/c85 vs `soh_env` |
| dome idx/blend | (3,0) w=0.324 / 0.611 | (3,0) blend 83/156 | dome curTime global 0x588f00 + play+0x3370 vs `soh_env` |
| rock draw combiner | 2·texel·primary exact, same texels | same formula | `SOH3D_PIXEL_TEX=0x180bf800` per-pixel dump |
| fogColor DATA | fog_color reg = (56,42,40) @0x3197 | blended fogCol (57,42,40) | per-draw `vsuni_log` fog fields vs `soh_env` |

## The diverging mechanism (all numbers live-measured)

- spot99 room 0: 28/29 CMB materials `isFog=1`; oracle draws them with `fog_mode=5` and the
  palette-blended fog color.
- Fog factor = 128-entry LUT over framebuffer depth; scene depths are compressed into
  0.96..1.0, so ALL fog action sits in LUT entries 126-127 (the `difference` field — a
  value-only dump reads as "no fog"; that false negative cost this session an hour).
  Max fog at far plane: 79.3% (dt 0x2bbb) / 75.2% (0x3197) / 71.3% (0x37b5).
- Exact closure: rock pixels (depth 0.9971-0.9976) → 48-53% mix toward (56,42,40); horizon
  fill (depth ≈1.0) → 75% mix. Applying these to SoH's unfogged renders reproduces the
  oracle's region means (tables below).
- Palette fogNear (u16@+0x08 & 0x3ff, per slot): night 40, sunrise 200, day 800, sunset 200.
  Blended 92 / 138 at the target frames. SoH's live `lightSettings.fogNear/fogFar` = stale
  996/12800 (N64 path; `applyLightOverride` never writes them).

## Region means at the target pairs (TEXPACK=off, fresh captures, scratch/dawn_hue/)

az=1000/soh=1408 (dayTime 0x3197):

| region | oracle | SoH (unfogged) | SoH + measured oracle fog mix (predicted) |
|---|---|---|---|
| sky behind mountain | (62.7, 51.1, 54.5) | (57, 67, 88) | (59, 48, 52) via 0.75 mix on the horizon fill |
| rock (Death Mtn) | (50.3, 47.9, 40.7) | (57.2, 68.6, 33.8) | (52, 55, 29) via 0.5 mix (+ haze layers closes G) |
| upper-mid sky (dome) | (58.2, 65.4, 90.6) | (59.6, 68.6, 92.9) | n/a — dome is NOT fogged on the 3DS |
| grass (near, unfogged) | matches | matches | n/a |

az=1522/soh=1930 (0x37b5): same structure; blended fogColor (107,79,49), max fog 71.3%.
(Whole-frame mean|d| at these pairs is dominated by the known camera-framing residual and
the az1522 moon composite — not re-attributed here.)

## Port surface (for the follow-up session, after the LUT-fill decomp)

1. `Zelda3D_TitleCsBlendedLight`/`applyLightOverride`: also blend + write
   `lightSettings.fogNear` (40→200 window at dawn) and a real far value (palette
   fogEnd f32 = 32000, drawDist 40000-56000) — today the title leaves stale spot00 values.
2. `gZelda3dFogEnable` defaults 0 (REPL-only) — contradicts the no-opt-out-gates doctrine;
   fog should be always-on once the curve is right.
3. The existing shader ramp `f=clamp(vFogDist·fogMul+fogOffset)/255` with
   `Zelda3D_FogSetPosition(996,12800)` evaluates to **zero at the far plane** — the current
   fog path is a no-op even when enabled. The replacement must reproduce the 3DS LUT curve
   as a function of view distance; the LUT-fill CPU function (writer of GPUREG_FOG_LUT_DATA
   0x0e8) is the decomp target that names that curve. Do NOT fit the three dumped LUTs with
   ad-hoc constants (tried candidate closed forms — N64 F3DEX on 2d−1, linear-in-world,
   fogEnd/drawDist-scaled fogcoord — none match within LUT LSB; the depth→world conversion is
   per-draw state that also needs pinning).
4. Sky handling: keep the dome UN-fogged (shader sky gate is already correct); the fogged
   layer behind mountains on the 3DS is a separate untextured horizon FILL draw (vertex
   colors ~(75,80,118) at 0x3197) that SoH may not draw as a distinct fogged surface —
   check what SoH renders in the dome-to-horizon gap when porting.

Secondary dawn layers observed on the oracle and absent in SoH (smaller than fog at the
measured pixels, unattributed to assets yet): additive horizon glow (untextured,
(182,34,0) α≈0.35, blend srcAlpha/ONE, mostly occluded), mauve haze band
(tex 0x1834c100, α≈0.08-0.17), warm alpha layer (tex 0x20ace580 VRAM, α≤0.38).

## Tooling added (committed; Azahar-side blocks recorded in tools/soh3d_harness/AZAHAR_PATCH.md)

- harness `az_fog`: live PICA fog mode/flip/color + full 128-entry LUT **with difference
  fields** + viewport depth regs.
- `vsuni_log` extended: per-draw fog mode/color/LUT samples + uProjection rows c0..c3.
- `SOH3D_PIXEL_UNTEX=1`: per-pixel dump of untextured draws (skips black fragments).
- `SOH3D_PIXEL_XY=<x>,<y>`: full compositing stack (every draw's fragment + cbuf + blend +
  depth) at one framebuffer pixel — the tool that cracked this; reusable for any
  "which layer paints this region" question.

## Dead ends this session (do not re-walk)

- PICA hardware fog "disabled" (mode=0) at end-of-frame: per-draw state; the UI pass resets
  it. Always sample fog regs per-draw (`vsuni_log`), not via an end-of-frame dump.
- Fog LUT "all 1.0": the value field alone is meaningless at entry 127 — the difference
  field carries a −0.7 drop. Dump both.
- The warm tint is NOT: the palette entries, the schedule, the dome vertex colors/blend, a
  CMAB-tinted rock material, a rock texture difference, or the texture pack (off throughout).
- Screen-coordinate mapping for pixel probes: 3D FB is 480x400, image (400x240) maps
  image_x = fb_row, image_y ≈ 240 − fb_col/2; the display pass at (x,y) samples the 3D FB at
  (2x, y) — do not equate the two coordinate spaces (cost two wasted probe rounds).
