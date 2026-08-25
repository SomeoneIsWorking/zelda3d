# OoT3D world (scene) lighting & combiner — reverse-engineered port spec

Goal: SoH3D world geometry must render **pixel-identical** to OoT3D (the user's definitive-edition
north star). This documents OoT3D's real per-material PICA200 fragment pipeline so it can be ported
into `soh3d_gl.cpp` (and the Vulkan backend), replacing the current ad-hoc `texture * vColor * uTint`.

Authoritative reference: **noclip.website `src/OcarinaOfTime3D/{cmb,render,zsi}.ts`** (the DMP/PICA200
shader generator). All offsets below are validated live against the 3DS ROM for the Kokiri room CMB
`/scene/spot04_0_info.zsi`. Probe: `scratch/lightport/mat_probe.py`.

## The pipeline OoT3D actually runs (per material)

OoT3D builds a per-material fragment program. For **scene/world geometry** the relevant config is:

1. **Vertex lighting** (`isVertexLightingEnabled`, material +0x01) — Kokiri: **all 21 mats = 1**.
   Fragment lighting (+0x00) = 0 for world geometry. So lighting is computed **per vertex**, fed
   into the combiner as `PRIMARY_COLOR` (= `v_Color`):

   ```
   for i in 0..1:                                  # only 2 lights in the vtx shader
     diffuse_i = sceneLight[i].diffuse * matDiffuse
     ambient_i = sceneLight[i].ambient * matAmbient
     NdotL     = max(0, dot(-sceneLight[i].direction, normal))
     acc      += diffuse_i * NdotL + ambient_i
   v_Color = saturate(acc) * a_Color               # a_Color = baked per-vertex color (VATR)
   ```

   **Kokiri material colors (validated raw bytes):** matAmbient = (255,255,255) WHITE
   (+0xA4), matDiffuse = (0,0,0) BLACK (+0xA8). Because matDiffuse is black, the **directional
   N·L term contributes nothing** — world lit color collapses to:

   ```
   v_Color = saturate( sceneLight0.ambient + sceneLight1.ambient ) * a_Color
   ```
   (sceneLight1.ambient is forced 0 in the ZSI parse, so effectively `saturate(sceneAmbient)*a_Color`).

2. **TEV texture combiner** (per-material, material +0x120 count / +0x124 index table → settings
   table after all materials, stride 0x28). The grass/ground material (mat0) is **one stage**:

   ```
   combineRGB = MODULATE(src0=PRIMARY_COLOR(v_Color), src1=TEXTURE0)   # = v_Color * tex
   scaleRGB   = x2                                                     # <-- KEY brightness factor
   out        = saturate(v_Color * tex) * 2.0
   ```

   `scaleRGB` is **per-material** (mat10/mat12 = x1, mat0 = x2). This is exactly why a single global
   brightness multiply is wrong and the real combiner must be ported. Combiner source/op/combine/scale
   enums: see `cmb.ts` (CombineSourceDMP / CombineOpDMP / CombineResultOpDMP / CombineScaleDMP) and
   `render.ts generateTexCombiner*`.

## What SoH3D does today (the bug)

`soh3d_gl.cpp` frag (uLit==0 world path): `frag = texture * vColor * uTint`, where `vColor` is the
RAW baked `a_Color` and `uTint ≈ 0.95`. It **ignores the TEV combiner entirely** (cmb.cpp parses no
combiner — confirmed), so it misses:
  - the per-material combiner **scale** (×2 for grass) — the prime brightness loss,
  - the **vertex-lighting ambient multiply** `saturate(sceneAmbient)` that OoT3D folds into v_Color,
  - any non-MODULATE combiner / multi-stage materials (e.g. mat10 stage1 MULT_ADD of TEX1).

Result measured (Kokiri noon): SoH3D grass (26,25,13) lum 21 G/R 0.95 vs oracle (75,98,26) lum 66
G/R 1.31 — ~3× dark + hue lost.

## Material byte layout (ver<=6, stride 0x15C; relative to material start)
- +0x00 isFragmentLightingEnabled (u8) ; +0x01 isVertexLightingEnabled (u8)
- +0xA0 emission / +0xA4 ambient / +0xA8 diffuse / +0xAC spec0 / +0xB0 spec1 — all RGBA8 **big-endian**
- +0xB4..+0xC8 constantColors[0..5] (RGBA8 BE) ; +0xCC..+0xD8 combinerBufferColor (4×f32 LE)
- +0xE4 lightingConfig (u32) ; +0x120 textureCombinerTableCount (u32) ; +0x124 index table (u16 each)
- combiner settings table base = matsChunk + 0x0C + count*0x15C ; entry stride 0x28:
  +0x00 combineRGB +0x02 combineAlpha +0x04 scaleRGB +0x06 scaleAlpha +0x08/0x0A bufferInput
  +0x0C/0x0E/0x10 source0/1/2 RGB +0x12/0x14/0x16 op0/1/2 RGB +0x18.. alpha +0x24 constantIndex (u32)
  Note: material data region starts at matsChunk **+0x0C** (not +0x10).

## ZSI environment settings (the scene light source) — scene header `<scene>_info.zsi`, cmd 0x0F
Per-setting stride 0x1C (non-Majora): +0x0A ambient RGB, +0x0D light0 dir (s8/0x7F), +0x10 light0 col,
+0x13 light1 dir, +0x16 light1 col, +0x19 fog col. Kokiri has **12 settings** (time-of-day variants).
light1.ambient forced 0. light0.col const (0,128,59) (irrelevant — matDiffuse black). The active
setting/blend is time-driven (OoT3D z_kankyo analogue).

## STATUS — increment 1 DONE (per-material combiner scale), live-verified on Vulkan

Ported the parse + plumbing + the per-material **combiner RGB scale** (the x2 grass factor):
- `cmb.cpp` now parses `vertex_lighting`/`fragment_lighting`, `mat_ambient`/`mat_diffuse`, and the
  stage-0 combiner (op, scaleRGB, sources) onto `CmbMaterial`.
- Threaded through `SoH3DGlGroup` → `GlGroup`/`VkGroup` (`makeCgroup`, both uploads).
- **Vulkan (the live backend)**: scene draws now do `saturate(tex*vColor*shade) * combScale`,
  scoped to non-lit scene geometry, gated by REPL `worldlit 0|1`. Measured Kokiri noon, frozen cam:
  grass lum 31→52 (oracle 66), G/R 1.12→1.19 (oracle 1.31); walls ~doubled; characters unchanged.
  Real move toward parity, visually correct (not blown out).
- **OpenGL**: has the FULLER reference impl (the real vertex-lighting equation with matAmbient/
  matDiffuse + uAmbient, then MODULATE*scale). NOTE: GL is currently a regressed/secondary path —
  it does not even draw the OoT3D room replacement (N64 shows through), so it can't be verified
  live; treat the GL shader as the reference for increment 2, not as a working renderer.

REMAINING GAP (still ~1.3x dark + slightly under-green): the lighting input. VK increment 1 reuses
the existing scene shade (N64 envCtx `uTintSkin`, ~0.31 gray ambient) instead of OoT3D's real ZSI
env ambient (Kokiri daytime ambient is brighter + tinted). Increment 2 = feed the real
`saturate(sceneAmbient*matAmbient + sceneDiffuse*matDiffuse*NdotL)` as the vertex-lit colour (the GL
shader already does this) and decide the scene-light SOURCE (N64 envCtx vs OoT3D ZSI env cmd 0x0F by
time-of-day). That unifies GL and VK on the same model.

## OPEN / next (do live, not offline)
Offline numeric reconstruction does NOT cleanly match the oracle yet — too many offline unknowns
(runtime ETC1 texture decode vs python, which of the 12 env settings is active + time blend, exact
per-vertex a_Color, gamma). Per project rule "verify the FULL path live vs oracle": implement the
port in-engine, gate A/B, and measure live grass/wall G-R + luminance vs the Azahar oracle. Do NOT
tune offline constants to match.

### Port plan
1. **cmb.cpp**: parse the TEV combiner (≥stage0: combineRGB, src0/1/2, op, scaleRGB, constantIndex)
   + isVertexLighting/isFragmentLighting + matAmbient/matDiffuse per material. Store on CmbMaterial.
2. **soh3d_gl.cpp world path**: replace `texture*vColor*uTint` with the real combiner eval. Minimum
   viable for scenes: `out = saturate(tex * v_Color) * scaleRGB`, with
   `v_Color = saturate(sceneAmbient*matAmbient + sceneDiffuse*matDiffuse*NdotL) * a_Color`.
   Feed sceneAmbient/diffuse/dir from the scene env (see "source of truth" below). Gate behind an
   env var + REPL toggle for A/B against the current path.
3. Generalize to multi-stage / non-MODULATE combiners (MULT_ADD, ADD, constants, buffer color) so
   layered materials (e.g. mat10) match too.
4. **Source of truth for scene lights**: decide N64 envCtx vs OoT3D ZSI env settings. For pixel parity
   prefer OoT3D's own ZSI env (cmd 0x0F) selected/blended by time-of-day; confirm which index the live
   game uses by reading the oracle.
5. Mirror in the Vulkan backend (single source of truth).
6. Verify live across scenes (Kokiri, Market, Kakariko, a dungeon; day+night) vs oracle. Pixel-diff.

Tools: `scratch/lightport/mat_probe.py` (material+combiner dump), `tools/cmb.py`/`zsi.py`/`ctr_romfs.py`
(ROM decode), the embedded-Azahar harness (`tools/harness_cli.py`), live SoH3D (skill soh3d-game-control).

---

## Session 2026-06-24 — parity re-measure + PLAN CORRECTION (read before doing "Increment 2")

Measured live SoH3D-VK vs Azahar oracle, **both at the Kokiri Deku-ledge spawn, ~midday**, sampling
green-grass pixels (constant-region, percentile stats). Findings, all data-backed:

- **Real position-matched gap = ~1.5x brightness, hue MATCHED.** Oracle grass median lum 54 / p90 87,
  G/R 1.26; SoH3D 36 / 57, G/R 1.21. (An earlier "2.4x" was a Saria sunlit-closeup framing artifact.)
  The ratio is ~constant across p25/p50/p90 ⇒ **linear** scale, **not** gamma.
- **Live VK world frag confirmed** (`soh3d_vk.cpp`): `rgb = tex * a_Color * uTint(~0.95) * combScale(2)`.
  SoH3D grass 36 == this exactly ⇒ SoH3D nets **~0.65× texture**.
- **The floor texture `s04_yuka_01r` decodes to lum 50** (same logic as runtime). The oracle renders
  the floor at **54 ≈ 1.08× the texture** — i.e. OoT3D shows the texture **~as-decoded; net world
  multiplier ≈ 1.0**. So the gap is the **world lighting multiplier: oracle ~1.0 vs SoH3D ~0.65.**
- **a_Color is genuinely dark & real** (not a decode bug): per-vertex u8 ×1/255; ground sepds mean
  ~0.34 (sepd0) … 0.56 (sepd13); sepd9/17 are CONSTANT-mode 0.4/0.5 gray.
- **Combiner scale is LITERAL (×1/×2), not the PICA 0/1/2→×1/×2/×4 enum.** Across all settings the
  raw scaleRGB values are only {1,2} and **scaleAlpha is always 1** — under the enum that would mean
  ×2 alpha on every material (absurd). So the live ×2 for grass is correct; **the missing 1.5× is NOT
  a ×4 scale.** (Also: `mat_probe.py` previously MISPRINTED amb/dif via an endian bug — read u32 LE
  then shifted BE; fixed to `>I`. The numbers in this doc's body — matAmbient=WHITE, matDiffuse=BLACK,
  grass scale ×2 — are the CORRECT raw bytes and match the live C++ `rgb_be` parse.)

### ⚠️ Consequence: the "Increment 2 = `saturate(sceneAmbient)·a_Color`" step CANNOT close the gap
Kokiri **matAmbient = WHITE** and `saturate(sceneAmbient) ≤ 1`, so that term is **at most `a_Color`** —
identical to (or darker than) SoH3D's current `a_Color·uTint·×2` path. It is a **noon no-op** (only
changes dawn/dusk/night). OoT3D renders the world **~1.5× brighter than its own documented
vertex-light × combiner model** (`a_Color·tex·scale = 0.68·tex`) predicts — the model is incomplete.

### Next experiments (do NOT add a global brightness constant — banned)
1. **Read OoT3D's LIVE env ambient from Azahar RAM** at noon, confirm it saturates ~1. (Warp on this
   Azahar build: write `play+0x5c2d = 0x14` (TRANS_TRIGGER_START) and `play+0x5c32 = 0xEE`
   (nextEntrance, Kokiri); PlayState was @ 0x0871e840 this session. Need the envCtx offset in OoT3D
   PlayState.)
2. **Live A/B in SoH3D-VK:** drop the spurious N64-envCtx `uTint` from the world path (OoT3D never
   applies the N64 tint) and read the actual FLOOR sepd's per-vertex `a_Color` in-engine; check if the
   net → ~1.0 and grass → ~54. Suspects, in order: (a) uTint double-dim (~1.05× alone), (b) floor
   a_Color ~0.5 with SoH3D's extra uTint pulling it to 0.65, (c) texture-decode/gamma residual.

### FIX LANDED (2026-06-24): exclude OoT3D world geometry from SSAO
**Root cause of the ~1.5x:** SoH3D's screen-space AO was darkening the OoT3D world on top of its
ALREADY-baked per-vertex AO (the dark a_Color *is* the baked occlusion) — a ~1.3x double-darkening.
Live A/B (Deku ledge): grass median lum AO+shadow=36, AO-off=47; shadow alone has ZERO effect on
open grass. So AO was the whole gap.

**Fix:** both AO depth-prepass loops in `soh3d_gl.cpp` (the VK dispatch ~L1560 and the GL `aoPass`
~L1378) now `if (!it.lit) continue;` — `DrawItem.lit==0` is world/scene geometry (baked AO), so it's
excluded from SSAO; `lit==1` dynamic actors/props (≈white vColor, no baked AO) still get it. Shadows
untouched (separate pass; they only darken genuinely shadowed pixels). Verified live on Vulkan with
AO ON: Kokiri Deku grass median 36→46, p90 57→73, G/R 1.22 (oracle 54/87/1.26) — hue matched,
~1.17x residual remaining (the spurious N64 `uTint`≈0.95 + minor a_Color/texture; a smaller
follow-up). Do NOT add a global brightness constant for that residual.

## FOG — FALSIFIED prior assumption + real F3DEX port (2026-06-24, #102, commit 1784454)

**Falsified:** the prior session asserted "OoT3D renders a DENSER atmosphere than the raw N64 F3DEX
fog reproduces — the 3DS forest haze fills the mid-field" and hand-tuned a world-distance ramp
(`fogNear=zFar*0.045`, `fogFar=zFar*0.31`). The oracle REFUTES this: at the Kokiri Deku ledge the
OoT3D background is **clear dark forest, almost no fog**. The ramp washed SoH3D's whole mid/far field
to a bright yellow-grey haze (the opposite of the oracle). The "verified at Deku ledge" claim behind
the ramp did not hold up to a location-matched comparison.

**Real model:** N64/OoT3D fog is the F3DEX screen-Z curve, NOT linear world distance. `z_play.c:235`
issues `gSPFogPosition(play->lightCtx.fogNear, 1000)`; the RSP (Fast3D `interpreter.cpp:1850`)
computes `fog_z = (clipZ/w)*fogMul + fogOffset`, clamped `[0,255]`, as the blend factor toward the
fog colour. `fogMul/fogOffset` come from the gbi.h macro: `fogMul = 128000/(max-min)`,
`fogOffset = (500-min)*256/(max-min)`, truncated to s16. Kokiri `fogNear=994` ⇒ mul=21333,
offset=-21077 ⇒ fog only over NDC z `[0.988, 1.0]` (near fog-free until the far clip). Hyrule
`fogNear=996, zFar=12800` ⇒ mul=32000, offset=-31744 ⇒ faint bluish horizon haze only.

**Port (live = Vulkan):** `soh3d.c SoH3D_FogSetPosition()` derives mul/offset from the live per-scene
`envCtx.lightSettings.fogNear`; the VK world vert passes the GL-NDC z (`clipZ/w`, captured *before*
the Vulkan z remap) and the frag applies the exact RSP formula. Globals renamed
`gSoH3dFogNear/Far → gSoH3dFogMul/Offset`. REPL: `fog pos <near> [max]`, `fog info` (shows mul/offset).
Per-scene faithful — no global constant. Verified: Kokiri Deku-ledge background haze band lum 167→85
(meanRGB 172,173,126 ≈ fog colour → 88,90,54 = real geometry).

**GL backend still has no fog consumer** (it doesn't render the OoT3D world; secondary path) — wiring
GL frag fog with the same mul/offset is an open follow-up, unverifiable until GL renders the world.

## NIGHT/DUSK HUE — new parity gap (2026-06-24, issue #110, data-backed)

Noon is now near-parity (~1.2-1.3x dark, hue ~ok). The bigger remaining gap is **time-of-day HUE**:
at NIGHT the OoT3D world goes cool moonlit-blue but SoH3D stays warm yellow-green.

**Measured (Kokiri, clean near-grass at Link's feet, fog≈0, frozen framing, SoH3D-VK vs oracle):**
| | R | G | B | B/R |
|---|---|---|---|---|
| oracle grass NOON  | 49.4 | 61.2 | **22.9** | 0.46 |
| oracle grass NIGHT | 16.7 | 33.5 | **22.9** | 1.37 |
| SoH3D grass NOON   | 47.9 | 54.6 | 6.6 | 0.14 |
| SoH3D grass NIGHT  | 27.7 | 39.4 | 6.6 | 0.24 |

**Signature → root cause:** the oracle's grass BLUE is *identical* noon vs night (22.9) while R/G
collapse. That is an **additive blue ambient/atmosphere FLOOR**, NOT a multiply (would scale blue
down) and NOT depth-fog (`render.ts:438` fog = depth `smoothstep mix` → ≈0 on near grass; fog colour
also varies with time, this floor doesn't). SoH3D's grass blue is likewise time-invariant but ~3.5×
lower (6.6) → **SoH3D lacks the blue ambient floor OoT3D applies.** The current SoH3D world frag is
purely multiplicative (`tex·a_Color·uTint·combScale`); a multiplicative blue tint can't introduce
blue onto a green-dominant texture (night `shade` B/R≈1.8 × grass tex R/B≈6 ⇒ predicted B/R≈0.3,
oracle is 1.37). The faithful OoT3D vertex model (`saturate(sceneAmbient·a_Color)·tex·scale`) is ALSO
multiplicative and can't reach B=22.9 on grass either ⇒ the 22.9 enters via an additive term TBD.

**The N64 envCtx DOES carry the blue night data** (SoH3D REPL `lightparams` @ time 0x0000:
ambient=(0.235,0.314,**0.431**), light1col dim-blue; @ 0x8000 ambient gray (0.314)³). So the source
colour exists; it's just annihilated by the multiplicative model. NOTE env lerps over several frames
after a `time` change — settle ≥1s before reading `lightparams` (an immediate read shows the prior
time's value).

**Fix plan (implement in-engine + verify LIVE — offline won't converge, per the doc rule above):**
1. Feed real OoT3D/env sceneAmbient into the world v_Color path (port the vertex-lighting eqn:
   `v_Color = saturate(sceneAmbient·matAmbient + ...)·a_Color`), replacing the flat N64 uTint
   multiply. A/B in VK (REPL toggle), measure grass B at noon AND night vs oracle.
2. If blue still short of 22.9 (likely — multiplicative can't reach it), add the **additive** ambient/
   atmosphere floor OoT3D applies, sourced from the env ambient colour (NOT a magic constant — derive
   the coefficient; the floor is ~the env ambient added post-combiner, clamp to [0,1]).
3. Verify ≥1 more scene + dusk (0xC000) and dawn. Repro: warp oracle to Kokiri via
   `play+0x5c32=0xEE, play+0x5c2d=0x14` (PlayState @ gPlayState=0x0050AF34); pin oracle dayTime by
   spam-writing u16 @ gSaveContext(0x00587958)+0x0C; SoH3D `time <u16>` + `camfreeze 1`.

### IMPLEMENTED (2026-06-24, VK world path) — additive scene-ambient blue floor
Added an **additive** env-ambient floor to the VK world frag (`soh3d_vk.cpp`: `uAmbient` UBO slot;
`rgb += uAmbient.xyz * uAmbient.w` for vertex-lit scene geom, scoped exactly like the combiner
scale). Globals `gSoH3dWorldAmbColor`/`gSoH3dWorldAmb`/`gSoH3dWorldAmbOverride` (`soh3d_gl.cpp`),
fed/overridable from `soh3d.c`; REPL `worldamb <coef> [r g b]`.

**Derivation (live vs oracle, Kokiri grass box (420,640,560,800)):** the env time-blended ambient
is the WRONG source — it's GRAY at noon (0.314,0.314,0.314), so scaling it overshoots R/G (at
coef 0.20 noon R 51.7→67.7 vs oracle 49). The oracle floor is a CONSTANT blue ≈22.9 at ALL times
(noon=night=dusk=dawn) with R/G≈0 — i.e. OoT3D's per-scene **constant** `u_SceneAmbient`
(render.ts:355 `t_FragPriColor += u_SceneAmbient`), not the per-light ambient. So the floor is
pinned to pure blue `(0,0,1)` × coef `0.06` (= +15.3/255): SoH3D grass blue 7.6 + 15.3 = **22.9**,
matching the oracle at every time of day while leaving noon R/G untouched (noon stays near-parity).

**Measured after (default-on):** grass blue → 22.9 (noon), 22.9 (night), 21.0 (dusk), 22.0 (dawn);
B/R 0.15→0.44 noon, 0.25→0.77 night. Verified no breakage on an indoor scene (chicken house wood
stays warm) and Kakariko night (grass cools, dark rock unaffected).

**REVISED 2026-06-24 — coef 0.06 → 0.02 (ground wash-out).** At coef 0.06 the dirt PATH washed out
to flat grey: a fresh consistent-framing oracle capture shows grass B/R 0.32 but path B/R 0.23 (path
is warmer, R>G, less blue), while a uniform flat floor pushed BOTH to ~0.50 — the path went bluest
because it's darker (flat add raises a darker surface's B/R more). The grass and path are
INDISTINGUISHABLE by material (both dif=0, amb=white, comb=2 — dumped via SOH3D_DBG_MAT); the
grass-vs-path blue difference lives in the baked vertex colour/texture, so a post-combiner additive
floor cannot separate them. Reproducing it exactly needs the full vertex-lighting port (#111). As a
non-breaking compromise the coef is lowered to 0.02 (a subtle cool ambient: grass B/R 0.15→0.26,
path 0.15→0.27, both near the oracle) and modulated per-material by matAmbient (render.ts:651).

**RESIDUAL (separate card, NOT this fix):** night R/G are still too BRIGHT — oracle night grass R is
34% of its noon R, SoH3D's is 58%, so B/R reaches 0.77 not the oracle's 1.37. Cause: the flat tint
`SoH3D_SceneTint = (ambient + 0.5·(light1+light2))·1.0` under-darkens at night (light2col stays
bright). That is the MULTIPLICATIVE night-darkening gap and needs the real OoT3D vertex-lighting
equation (`saturate(diffuse·NdotL + ambient·matAmbient)`), the bigger lighting port — out of scope
for the #110 additive-floor fix. TODO: also source `u_SceneAmbient` PER-SCENE (the 0.06 blue is
Kokiri-derived; other scenes may differ).

## #111 VERTEX-LIGHTING PORT — corrected ZSI layout, slot/schedule mapping, model (2026-06-24n→o)

This is the source-of-truth section for the #111 port. **A prior session used the WRONG ZSI env
layout** (noclip's `zsi.ts` offsets — ambient at +0x0A). That layout is for *N64 ZSI embedded inside
the 3DS build*, NOT native 3DS romfs ZSI. The CORRECT native-3DS env-setting layout (28-byte stride,
from `oot3d-decomp/docs/scene_lighting.md`, re-validated here against `/scene/spot04_info.zsi`):

```
+0x00 u8[3]  ambientColor       +0x03 s8[3] light0Dir   +0x06 u8[3] light0Color
+0x09 s8[3]  light1Dir          +0x0c u8[3] light1Color  +0x0f u8 pad
+0x10 f32 fogEnd (=12000 Kokiri) +0x14 f32 drawDist (=2400) +0x18 u16 blendFog
```
Validated: with this layout `fogEnd=12000.0`, `drawDist=2400.0` come out as clean round numbers and
`light0Color` is the constant per-scene sun colour — the +0x0A layout produced garbage. **Entry 0 is a
metadata blob (different layout) — skip it; entries 1..N-1 are the per-time-of-day settings.** The
parsed Kokiri (`spot04`) palette matches `oot3d-decomp/data/scene_lighting.json["spot04"]` exactly.
Probe: `scratch/lightport/env_probe.py` (uses the CORRECT layout).

### Slot↔slot + time schedule are SOLVED (no more guessing which setting is active)
- **OoT3D scene slot `i` ↔ N64 scene slot `i`** (slot-by-slot; per `oot3d-decomp/tools/lighting_parity.py`).
- **The N64 z_kankyo time schedule (`D_8011FB48` row 0) picks the slot by time** and SoH3D already
  runs it: noon `0x8000` → slot **1**, deep night `0x0000` → slot **3** (verified LIVE: SoH3D N64
  `lightparams` reads ambient `(80,80,80)`=N64 spot04 slot1 at noon, `(60,80,110)`=N64 slot3 at night,
  matching `D_8011FB48[0]` = `{day=1, night=3}`). So the active OoT3D slot per time is KNOWN for free
  by reusing the N64 schedule — **do NOT reverse-engineer OoT3D's own kankyo**.
- OoT3D `spot04` ambient palette (corrected): slot1(noon)=(61,72,72), slot3(night)=(40,72,72),
  slot2/4=(160,72,72) bright. Note **G,B are constant 72** across the day slots (1-8).

### The model is NOT pure `saturate(2·ambient)` — there IS a directional term (offline can't close it)
Derived the oracle's effective WORLD SHADE from rendered grass (pixel/texture), per channel:
| | shade_R | shade_G |
|---|---|---|
| noon  | ~0.87 | ~0.70 |
| night | ~0.29 | ~0.39 |
| night/noon | **0.34** | **0.55** |

noclip's room path (`render.ts:1518`, *explicitly labelled "Temporary hack until I get kankyo
implemented"*) forces both world lights' diffuse=BLACK and `light[1].ambient=light[0].ambient`, giving
world shade = `saturate(2·ambient·matAmb)`. That predicts night/noon R=0.66, **G=1.0** (G ambient is
constant 72 → no G darkening) — but the oracle G DOES drop to 0.55, and noon shade_R 0.87 ≫ 2·ambient
(0.478). The noon "extra" over 2·ambient is R≈0.389 ≈ **light0Color_R (99/255=0.388)** → OoT3D world
geometry DOES receive a directional/diffuse contribution from light0/light1 (brighter sun at noon than
moon at night). So the faithful model is `saturate(2·ambient·matAmb + Σ lightCol·something·NdotL)`,
NdotL depending on per-vertex normals — **only resolvable LIVE** (matches the doc's standing rule).

### Port plan (build + verify LIVE, A/B; do NOT tune offline)
1. **Data**: `tools/gen_oot3d_scene_lighting.py` → generated `.inc` table keyed by OoT3D scene name,
   one row per slot: {ambient[3], l0dir[3], l0col[3], l1dir[3], l1col[3]} (corrected 28-byte layout,
   skip entry 0). Runtime maps SoH sceneNum→OoT3D name via existing `SoH3D_SceneName`/`kSoH3dSceneNames`.
2. **Blend**: reuse the N64 z_kankyo schedule — the active slot index(es) + weight are already computed
   for the N64 palette; compute a PARALLEL OoT3D-palette blend with the same indices/weights into
   globals `gSoH3dWorld{Ambient,Light0Col,Light0Dir,Light1Col,Light1Dir}[3]` (hook next to the
   `envCtx->lightSettings.ambientColor` write in `z_kankyo.c`, outdoor + indoor paths).
3. **Shader (world path, `soh3d_vk.cpp`)**: replace the flat-tint `shade` for scene geom (uParams.y<0.5)
   with the OoT3D vertex-light eqn using per-vertex view-normal: start
   `shade = saturate(2·ambient·matAmb)`, then ADD the directional term and tune which light/coeff LIVE
   against the oracle until noon stays at parity AND night R/G hit 0.34/0.55. Keep the #110 additive
   blue floor. Gate behind REPL `worldlit`/env var for A/B.
4. Verify Kokiri noon+night+dusk vs oracle, then ≥1 more scene (Market/Kakariko + a dungeon).

### RESOLVED via the live oracle (2026-06-24o) — ground truth from Azahar RAM
A RAM investigation of the live OoT3D oracle (now in oot3d-decomp/docs/ram_map.md "ENVIRONMENT
LIGHTING") resolved the two offline-unresolvable unknowns:
1. **CORRECTED romfs ZSI light-color offsets.** Validated against the runtime EnvLightSettings:
   `+0x00 ambient · +0x04 light0Color · +0x07 light0Dir(s8) · +0x0a light1Color · +0x0d light1Dir(s8)`.
   The earlier doc/generator had light0Color @ +0x06 — WRONG (a near-constant field). Confirmed for
   spot04 noon/night/dusk: l0col = (255,255,219)/(63,63,99)/(239,140,61). gen_oot3d_scene_lighting.py
   fixed + table regenerated.
2. **Slot alignment = +1.** The runtime DROPS ZSI metadata entry 0, so runtime slot i = ZSI entry
   (i+1). N64 z_kankyo schedule: noon→slot1, night→slot3 → OoT3D entry2/entry4. `gSoH3dWorldShadeSlotBias
   = 1`. Verified live: SoH3D's `SoH3D_WorldShadeBlend` now reports the IDENTICAL blended env values as
   the oracle (noon amb=(160,72,72) l0col=(255,255,219); night amb=(160,72,72) l0col=(63,63,99)).
3. **The day/night darkening is in light0Color (the sun), NOT ambient** (ambient ~constant 160 noon &
   night). So the world shade tracks light0Color. light1Color is the cool moonlight FILL
   (night (99,170,219)) that keeps night G/B up.

### Model shipped (opt-in, REPL `worldshade`): saturate(ka·ambient + kd·light0Color + ke·light1Color)
Per-channel, computed on CPU as the room's single shade tint (matches the existing one-tint room
architecture). Defaults ka=0.16, kd=0.77, ke=0.12 (REPL-tunable, no rebuild). LIVE A/B (Kokiri,
consistent framing within a run, grass night/noon ratio vs oracle):
| ratio | flat tint (shipped) | worldshade model | oracle |
|---|---|---|---|
| night/noon R | 0.58 | **0.36** | 0.34 |
| night/noon G | 0.73 | 0.32 (ke=0) → ~0.45 (ke=0.12) | 0.55 |

Night R is fixed (the primary #111 complaint) and noon moved closer to oracle.

### CORRECTION (2026-06-24p): ka=0 — ambient out of the multiplicative term (measured regression fix)
A pinned-frame A/B (Kokiri grass, fixed cam `cam -66 5 1075 -66 -55 945`, Link tp'd out of frame,
`measure_grass.py` box) found the shipped **ka=0.16 DE-GREENS noon**: the Kokiri ambient is
red-dominant `(160,72,72)` (G,B pinned ~72), so ka folds that red into a multiplicative tint:

| model (noon, pinned frame) | R | G | B | G/R |
|---|---|---|---|---|
| worldshade OFF (flat tint) | 63.4 | 75.7 | 19.5 | 1.19 |
| shipped ka.16/kd.77/ke.12  | 61.3 | 68.5 | 15.9 | **1.12** (de-greened + dimmed) |
| **ka0/kd.9/ke.12 (new default)** | 63.1 | 74.7 | 16.8 | **1.18** (matches OFF) |
| oracle target | — | — | — | ~1.26 |

Night/noon ratios with the new default: **R 0.28** (oracle 0.34 — the #111 darkening, retained),
G 0.32, B 0.69. So `ka=0` is a strict improvement: removes the noon de-green/dim regression while
keeping the night fix. The reddish ambient belongs in the ADDITIVE #110 floor, not the multiply.
**New defaults: ka=0.0, kd=0.9, ke=0.12.**

RESIDUALS (NOT to be closed by more coefficient grinding — [[soh3d-stop-microtuning-lighting]]):
(a) night **G** ratio 0.32 vs oracle 0.55 and **B** short — both come from the #110 ADDITIVE floor
being too small (coef 0.02), a #110 follow-up, NOT this multiplicative model; (b) a single global
per-light coef cannot fit R AND G simultaneously — this is IRREDUCIBLE for a single-tint model.
KEY: the oracle's world shade matched as a SINGLE per-room tint (no per-vertex normal dependence;
Kokiri matDiffuse=BLACK → no real NdotL), so the per-vertex NdotL port would NOT help here and is
not the answer — the lights act as global colour terms, not directional diffuse. (c) Verifying ≥1
more scene + the default-ON decision are user-gated (any default visual change needs user approval).
Tooling: scratch/lightport/{env_probe,measure_grass}.py.
