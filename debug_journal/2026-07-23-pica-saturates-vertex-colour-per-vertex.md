# 2026-07-23 — Kokiri near-terrain +18% was the CLAMP STAGE, not the vertex-colour decode

`render.kokiri-near-terrain-overbright`. Outcome in one line: **PICA saturates the vertex-shader
colour output PER VERTEX, before interpolation. We were evaluating `clamp(lightSum * vColor)` per
FRAGMENT on interpolated inputs. Moving the whole PRIMARY_COLOR term into the vertex shader takes
d8 from 1.183–1.193 to 0.996–1.018 and pulls five other Kokiri draws toward 1.0.**

Nothing committed; changes are in the working tree (listed at the bottom).

## The decode was never wrong — ruled out first, with data

The named suspect was the CMB vertex-colour attribute format/scale. It is correct:

* `spot04_0_info.zsi`, sepd 3 (= our group g3, `first=2661 count=2727`, the oracle's d8):
  colour attribute is `dataType=0x1401` (UNSIGNED_BYTE), `scale=1/255`, `mode=0` (ARRAY),
  4 components — identical for **every** array-mode sepd in the file. That is exactly what
  `cmb.cpp` decodes, and it matches the oracle's own reported `vtxScl0=(1, 0.007874, 0.0039216, 0)`
  (the 1/255 slot).
* The per-sepd `attr.start` values are self-consistent across attributes (sepd 3: colour
  `5576/4 = 1394`, position `16728/12 = 1394`), so the base-offset indexing is right too.
* The material colours are not a hue source either: **all 21** `spot04_0` materials carry
  `matAmbient = (255,255,255)` and `matDiffuse = 0`, i.e. the light term reduces to the scene
  ambient exactly as measured. No per-material tint to explain a hue shift.

So the hue-shaped ratio (R/G ~1.24, B ~1.08) had to come from somewhere else.

## Root cause — ground truth from Azahar

`Azahar/src/video_core/pica/output_vertex.cpp`, `OutputVertex::OutputVertex`:

```cpp
// The hardware takes the absolute and saturates vertex colors, *before* doing interpolation
for (u32 i = 0; i < 4; ++i) {
    const f32 c = std::fabs(color[i].ToFloat32());
    color[i] = f24::FromFloat32(c < 1.0f ? c : 1.0f);
}
```

The 3DS evaluates CmbVShader's `o1 = clamp(Σ lights, …) · a_Color` at each **vertex**, saturates the
**result**, and the rasterizer interpolates the already-clamped value. We had the formula and the
clamp ORDER right (both re-confirmed yesterday from the oracle's registers) but the clamp STAGE
wrong. `min(·,1)` is concave, so

```
lerp(min(a,1), min(b,1))  <=  min(lerp(a,b), 1)
```

— the per-fragment form is systematically **brighter** across every triangle edge where one end
saturates, and never darker. That predicts the exact three things measured:

1. an over-bright, not under-bright, terrain;
2. confined to the NEAR terrain (large near triangles straddle saturation across many pixels;
   distant ones cover few);
3. **hue-shaped**: at Kokiri noon the light sum is `2 × (0.7098, 0.7098, 0.62745)`, so red/green
   saturate for any baked vertex colour above `1/1.4196 = 0.704` while blue only above
   `1/1.2547 = 0.797`. R/G lose more to per-vertex clamping than B — the measured 1.24/1.23/1.08.

## Fix

`Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp`: the vertex-lit PRIMARY_COLOR term moved
from `kFrag` to `kVert`, emitted as a new varying `vPrim` (location 7) with PICA's own
`min(abs(x), 1)` saturation; the fragment shader's `vtxLit` branch is now just `prim = vPrim`.
This also fixes the normal: PICA evaluates `dot(N, -L)` per vertex, we were re-normalising an
interpolated normal per fragment.

## Measurements — Kokiri, vanilla BOTH sides, exclusive pixels, matched camera

Entrance `0xEE` (238), tod `0x6000`, `cam -153.2 -22.0 1043.7 -90.2 -38.2 967.7`, re-issued
immediately before every `shot`; oracle masks `scratch/drawiso/kokiri` (texpack off, provenance
recorded); our run launched with `ZELDA3D_TEXPACK=off`, `tev_mask_ratio.py` confirmed
`oracle=off ours=off` on every capture. A/B is a real two-build A/B: the per-fragment form was
rebuilt and re-measured on the same session/camera, not quoted from yesterday.

| draw | BEFORE (3 settled samples) | AFTER (5 settled samples) |
|---|---|---|
| **d8 near terrain (126682 px)** | **1.183 / 1.193 / 1.183** | **1.002 / 1.018 / 1.018 / 1.003 / 0.996** |
| d17 | 1.033 / 1.002 / 1.043 | 1.003 / 1.002 / 1.005 / 1.002 / 1.015 |
| d12 | 1.015 / 1.019 / 1.014 | 1.007 / 1.005 / 1.005 / 1.005 / 1.006 |
| d10 | 1.028 / 1.017 / 1.017 | 1.507* / 1.032 / 1.016 / 1.016 / 1.016 |
| d5  | 1.005 / 1.016 / 1.003 | 1.003 / 1.001 / 1.086* / 1.001 / 1.001 |
| d9  | 1.024 / 1.017 / 1.025 | 1.056* / 1.004 / 1.006 / 1.037 / 1.013 |
| d15 | 0.998 / 0.992 / 1.029 | 0.976 / 1.014 / 1.007 / 1.003 / 1.000 |
| d11 | 1.034 / 1.044 / 1.033 | 1.074 / 1.066 / 1.179* / 1.136* / 1.004 |
| d7  | 0.936 / 0.912 / 0.959 | 0.923 / 0.908 / 0.941 / 0.910 / 0.931 |
| d59 | 1.800 / 1.800 / 1.800 | 1.800 (identical every sample) |

`*` = the small masks (≤14k px) swing frame to frame in BOTH builds — waving foliage and Link
drift through them. Judge those on their spread, not a single sample. d7 is unchanged within its
own spread (0.912–0.959 → 0.905–0.941) and d59's 1.800 is bit-identical before and after, so
neither is caused by this change; both are pre-existing residuals for a later step.

Every regression gate held or improved. The predicted signature — "a correct decode/stage fix
should move MANY draws toward 1.0 at once" — is present: d8, d17, d12, d5, d10 and d9 all tightened.

Artifacts: `scratch/screenshots/kok_old{1,2,3}.png` (per-fragment build),
`scratch/screenshots/kok_{pvfix,pv1,pv2,pv3,n1..n5}.png` (per-vertex build).

## Zora regression gate — unchanged

Entrance `0x109` (265), tod `0x6000`, `cam -1286.4 285.1 -159.0 -1089.4 250.3 -160.2`, masks
`scratch/drawiso/zora_masks`, vanilla both sides. Opaque surfaces over exclusive pixels:
`d11 0.993, d3 0.995, d12 0.997, d10 1.000, d52 0.969, d43 1.027` — the recorded 0.99–1.00 band.
The translucent layers are also unmoved (`d9 0.762` vs the two prior sessions' 0.757/0.772), so
this change neither helps nor hurts `render.zora-translucent-layers`.

## Step 2 (`render.zora-translucent-layers`) — one real finding, then stopped

**The affected draws are ADDITIVE, not alpha-blended.** From the CMB itself
(`spot07_0_info.zsi` mats 1 / 13 / 14): `blendEnable=1, srcRGB=GL_SRC_ALPHA (0x0302),
dstRGB=GL_ONE (0x0001), eq=FUNC_ADD, depthWrite=0`. Our `glBlendFactor` mapping handles
`0x0001 -> SDL_GPU_BLENDFACTOR_ONE` correctly, so the state itself is ported; but it means the
residual must be read as an additive **contribution over the background**, not as a ratio of the
composited pixel:

| | oracle | ours |
|---|---|---|
| composited d9 | (100,184,231) | (95,136,145) |
| background under it (d3) | (59,74,84) | (52,66,74) |
| **implied additive contribution** | **(41,110,147)** | **(43,70,71)** |

Red is at parity (1.05); green 0.64, blue 0.48. So it is not a gain or an alpha error on the layer
(those would be flat across channels) — the layer's own emitted colour is short in G/B. Next
observation to take: the oracle's per-fragment TEV tap (`SOH3D_HARNESS_SW=1` + `SOH3D_PIXEL_TEX`,
the probe built yesterday) on d9's pixels, to split that between texcol and PRIMARY_COLOR the same
way d8 was split. Also note d9/d49/d15/d54 are single-stage `MODULATE(PRIMARY, TEX0) x2` with
`texEn=1/0/0` — identical combiner configuration to d11, which measures 0.982 — so the combiner
math is not the difference; the inputs are.

Also measured in passing: `fog3d 0` at Zora collapses nearly every draw (d3 0.889→0.546,
d48 0.880→0.422, d9 0.762→0.679), i.e. the fog stage is doing large, real work here — it is not an
inert path, and any Zora hypothesis has to survive with fog on.

## Working-tree changes (nothing committed)

* `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp` — the fix above: new `vPrim` varying,
  vertex-shader PRIMARY_COLOR with PICA's `min(abs(x),1)` per-vertex saturation, fragment
  `vtxLit` branch reduced to `prim = vPrim`. Two comment blocks record the Azahar ground truth
  and why the fragment-stage form is not equivalent. The yesterday's-session changes
  (`SOH3D_HARNESS_SW`, the sw_rasterizer PIXEL dump cap, FRAGDBG modes 5/6/7 + hard-warn,
  REPL `fog3d`) are still in the tree from before and are untouched.
