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
(ROM decode), Azahar oracle (`tools/oracle_boot.sh`), live SoH3D (skill soh3d-game-control).

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
