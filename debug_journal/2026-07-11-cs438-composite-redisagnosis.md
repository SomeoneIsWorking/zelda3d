# cs438 wordmark composite residual — RE-DIAGNOSED: not alpha/blend/destination, it's letter-stroke coverage (2026-07-11)

Follow-up to `2026-07-11-attr-cs438-composite.md`. That session's fixSpec proposed two
hypotheses for the 0.141 ratio gap (SoH 0.720 vs oracle 0.579): (a) the runtime alpha
reaching the draw differs from the paper value, or (b) the blend destination under the
letters is brighter than expected. **Both are now RULED OUT.** The real residual is on a
completely different axis: **letter-stroke coverage** — SoH renders ~10× fewer high-
saturation letter pixels than the oracle at the same content-matched frame, while the
fire-glow backdrop and per-pixel brightness are at parity.

## Step 1 — runtime alpha trace confirms hypothesis (a) is wrong

Added `ZELDA3D_DBG_WORDMARK_ALPHA` env-gated trace at `title_logo.cpp:534` (the actual
draw-call site, mirroring the existing `ZELDA3D_DBG_SHEEN` pattern). Rebuilt the harness,
ran `title_ab.py ab 700 --soh 1105` (the cs438 pair) with the trace on:

```
[WORDMARK_ALPHA] csFrame=438 phase=1 wordmarkAlpha=162.00 alphaU8=162
```

**Exactly** the paper derivation (wordmarkStart=fadeIn+40=384, elapsed=54, 54×3.0=162,
162/255=0.635). The runtime alpha reaching the draw is correct. **Hypothesis (a) ruled out.**
The trace itself is kept as reusable tooling (zero-cost when the env var is unset, like
the other ZELDA3D_DBG_ traces).

## Step 2 — fully-opaque frame isolates the source, disproves "darker absolute exposure"

Captured a fully-opaque pair (alpha=255, where src-over `out = src` exactly — destination
irrelevant). Calibrated: `title_ab.py calibrate 760` → az=760/soh=1163 (score 0.8378).
Trace confirms csFrame=466, alphaU8=255.

Measured letter brightness (red-hue mask) AND a background-only control (grass strip) at
this same frame:

| pane | letters V (alpha=255) | background V (grass) |
|---|---|---|
| oracle | 0.367 | 0.283 |
| soh | 0.160 | 0.287 |

The **backgrounds match** (0.283 vs 0.287 — within noise). The prior session's explanation
for the dim letters was "SoH's darker absolute exposure" (`wordmark-sheen-mechanism-
ported.md` line 73) — **this is falsified**. The exposure is NOT globally darker; the
dimness is letter-specific.

But the letter V gap (0.367 vs 0.160) is NOT a per-pixel brightness gap — it's a
**coverage artifact** of the measurement (averaging over a fixed hue-masked box where SoH
has fewer letter pixels). See Step 3.

## Step 3 — per-pixel brightness is at PARITY; the gap is coverage

Re-measured using a global red-pixel detector (whole image, not a fixed box) and compared
the **top-25% brightest** red pixels (the actual letter-stroke centers):

| frame | pane | top-25% R | red px count | bbox size |
|---|---|---|---|---|
| cs465 (opaque) | oracle | 0.474 | 5289 | 285×142 |
| cs465 (opaque) | soh | **0.482** | 1134 | 265×148 |
| glow_cal (ass'y done) | oracle | — | 9237 | 268×168 |
| glow_cal (ass'y done) | soh | — | 5236 | 269×174 |

**Per-pixel brightness is at parity** (SoH top-25% R=0.482 vs oracle 0.474 — SoH slightly
BRIGHTER). The **bounding boxes are within 8%** (285×142 vs 265×148). But the **red pixel
count is 3-5× lower** in SoH. The letters are the same SIZE and BRIGHTNESS but cover much
less screen AREA — i.e., the strokes are thinner / more gaps.

## Step 4 — letter vs glow decomposition: it's ALL in the letter strokes

Separated high-saturation letter-stroke red from low-saturation glow-wash red:

| frame | pane | LETTER strokes (strict) | GLOW wash |
|---|---|---|---|
| cs465 (opaque) | oracle | **4930 px** | 1122 px |
| cs465 (opaque) | soh | **508 px** (9.7× gap) | 1769 px (SoH has MORE) |
| glow_cal (ass'y done) | oracle | **5178 px** | 8768 px |
| glow_cal (ass'y done) | soh | **1530 px** (3.4× gap) | 9946 px (SoH has MORE) |
| cs438 (mid-fade) | oracle | **3035 px** | 2266 px |
| cs438 (mid-fade) | soh | **81 px** (37× gap) | 2053 px |

**The fire-glow backdrop is at parity or SoH-brighter.** The entire residual is in the
high-saturation letter strokes themselves. At cs465 (alpha=255, assembly ~68% done), SoH
renders only 508 letter-stroke pixels vs the oracle's 4930 — the letter bodies are ~10×
sparser on screen despite being the same brightness per pixel and same bounding-box size.

## What this rules out (do not re-investigate)

- **NOT the runtime alpha** — 162 at cs438, 255 at cs466, both bit-exact (Step 1).
- **NOT the blend equation/destination** — at alpha=255 (destination irrelevant) the gap
  persists; the blend is textbook src-over and the destination is confirmed on-screen grass
  (background control matches). Hypothesis (b) ruled out.
- **NOT a global exposure difference** — backgrounds match at 0.283/0.287 (Step 2).
- **NOT per-pixel letter brightness** — top-25% brightest R is at parity (0.482 vs 0.474).
- **NOT the wordmark scale/placement** — bbox is within 8% (285×142 vs 265×148).
- **NOT the fire-glow backdrop** — glow wash is at parity or SoH-brighter (Step 4).
- **NOT the sheen term** — at cs466 (t=1) shade=0.757, but this affects both engines
  equally and the per-pixel brightness matches anyway.
- **NOT the CMB combiner** — title_logo_us mat0/1/2 have a trivial 2-stage MODULATE
  (no dual-tex, no const-scale, no brightness-multiplying stage — unlike g_title.cmb's
  fire-glow). The texture decode (RGBA4444, e4 bit-replication) is standard and correct.
  Vertex colors default to white (1,1,1,1) — the color VATR is size=0 (attrHasData=false,
  same class as the g_title.cmb fix, falls back to white correctly).

## What's still open (the real next investigation)

SoH renders the `title_all` RGBA4444 wordmark texture with letter strokes that are
dramatically thinner than the oracle's, despite identical bounding-box size and per-pixel
brightness. The leading candidates, in order:

1. **Texture sampling / filtering of RGBA4444 alpha edges.** `title_all` is RGBA4444 (4
   bits/channel = 16 alpha levels). Letter-stroke edges have semi-transparent texels
   (alpha 1-14 out of 15). If SoH's bilinear filter or alpha-rounding clips these edges
   more aggressively than the PICA200 hardware does, strokes appear thinner. The texCoord0
   scale values for the wordmark meshes are extremely small (~3e-05) — worth verifying the
   UV precision isn't collapsing strokes onto fewer texels.
2. **Assembly-animation timing.** The coverage gap shrinks from 9.7× (cs465, csabFrame=82,
   68% assembled) to 3.4× (glow_cal, csabFrame>120, fully assembled) — part of the early-
   frame gap is the fly-in animation being at different points. But a 3.4× gap persists at
   full assembly, so this is only a partial explanation.
3. **The cs438 ratio measurement itself is confounded by coverage.** The 0.720-vs-0.579
   ratio gap that started this investigation is largely an artifact of averaging letter
   brightness over a fixed box when SoH has 10-37× fewer letter pixels — the box mean is
   dominated by background in SoH but by letters in the oracle. The ratio metric as defined
   cannot distinguish "letters too dim" from "letters too sparse"; a coverage-aware metric
   is needed for future wordmark measurements.

None of these were instrumented this session — the budget went to ruling out the fixSpec's
hypotheses and isolating the coverage axis. The concrete next step is a texture-sampling
investigation: dump the actual texels SoH samples at a known letter-stroke screen pixel
(the harness's `PIXEL` draw-log or a UV-instrumented trace), compare to what the oracle
samples at the same UV, and determine whether the RGBA4444 alpha-edge handling differs.

## Tooling added (reusable)

- `ZELDA3D_DBG_WORDMARK_ALPHA` env-gated trace (`title_logo.cpp:534`) — prints csFrame,
  phase, wordmarkAlpha, alphaU8 at the draw call. Zero-cost when unset. Confirms the
  runtime alpha matches the paper derivation at any frame.
- `scratch/decomp_agent/measure_opaque.py` — letter brightness at alpha=255 (isolates
  source, no blend confound) + background control.
- `scratch/decomp_agent/measure_letters_detailed.py` — global red-pixel detector with
  top-25% brightness percentile comparison.
- `scratch/decomp_agent/measure_wordmark_size.py` — bbox dimension comparison (width,
  height, center) between panes.
- `scratch/decomp_agent/letter_vs_glow.py` — separates strict letter-stroke red (high
  saturation) from glow-wash red (low saturation). THE discriminator for this residual.
- `scratch/decomp_agent/stroke_profile.py` — horizontal R-channel cross-section showing
  stroke positions/thicknesses per pane.

## No code changes to rendering

`title_logo.cpp` gained only the env-gated trace (tooling). No rendering math changed —
the investigation was diagnostic, and the fixSpec's proposed hypotheses were falsified
before any "fix" was attempted (per the no-bandaid directive: the cause was not what the
prior session expected, so no constant-tuning was applied).

## Addendum: deeper root-cause investigation (same session, continued)

Pursued the coverage gap to its mechanism. Key findings:

### The coverage gap is in the DECORATIVE meshes (gold outlines), not the letter fill

The wordmark model (title_logo_us.cmb, model ID 2014) has 22 groups across 12 materials:
- mat0/1/2 (title_all, RGBA4444): the red letter fill — 5 meshes, 12 triangles
- mat3-11 (i_ctex*/zelda_logo_ev*): the gold-outline decorations — 17 meshes

Oracle draw_log (az=760, captured via harness `draw_log`): the oracle draws **676 dual-tex
decoration triangles** + **12 letter-fill triangles**. The decorations vastly outnumber the
letters. SoH's 508 strict-letter pixels vs the oracle's 4930 is because SoH renders the
decorations invisibly (black output) while the oracle renders them as visible gold/red.

### Decorations DO reach the fragment shader — confirmed via magenta debug

Temporarily replaced the dual-tex fragment output with bright magenta: **39158 pixels**
turned magenta. The decoration geometry IS being drawn and passes the alpha test. The issue
is the combiner MATH producing black/invisible RGB, not culling or depth.

### Oracle's live TEV registers vs CMB file — they MATCH for most decorations

Decoded the oracle's live PICA TEV stages from the draw_log for each decoration type:

| material | oracle tev[0] | oracle tev[1] | result | CMB file says | SoH mode |
|---|---|---|---|---|---|
| mat4/5 (i_ctex04a) | MODULATE(PRIM,TEX0)×2 | MODULATE(PREV,TEX1)×2 | 2*PRIM*TEX0*TEX1*2 | same | mode 3 ✓ |
| mat6/9 (i_ctex10a) | ADD(TEX0,TEX1) | MODULATE(PREV,PRIMARY) | (TEX0+TEX1)*PRIM | same | mode 2 ✓ |
| mat10 (zelda_logo_ev01) | MODULATE(PRIM,TEX0) | REPLACE(PREV) | PRIM*TEX0 | same; TEX1 disabled | coordinator-0 sphere map |

**The combiner formulas MATCH for mat4/5/6/9.** SoH implements the same multiply/add as the
oracle. Yet the decorations are invisible. The CMB file's combiner for mat4/5 is correctly
parsed and the dual-tex detection works (verified via `[DBG_DUALTEX]` trace: mode=3, tex1Idx
correct, tex1Bound=1).

The 2026-08-30 exact draw-identity audit retracted the earlier mat10 exception. The old row had
attributed another draw's TEV state to mat10. Exact mat10 draws enable only TEX0 and preserve the
CMB's MODULATE→REPLACE chain; their distinct behavior is coordinator-0 CameraSphereEnvMap, not a
draw-time combiner rewrite or aliased TEX1.

### Dual-tex binding is correct

Traced via `[DBG_DUALTEX]` stderr trace: all 5 dual-tex groups (g0/g2/g13/g14/g15) have
correct dualTexMode, tex1Index, and tex1 is bound (not null). The second texture IS sampled.

### What's still unexplained

For mat4/5/6/9, the combiner formula, texture binding, and depth handling ALL match the
oracle, yet the decorations produce invisible output. The remaining suspects:
1. **vUv1 (second texture UV) mapping** — the shader computes `vUv1 = (aUv - trans) * scale`,
   but the coordinator-1's `sourceCoordinate` byte (byte 0 of the coordinator entry, per
   noclip cmb.ts) is NOT read by the CMB parser. For mat4 it's 0 (use texCoord0), which the
   shader already does. But if other materials use a different source, vUv1 would be wrong.
2. **The texture content at the sampled UVs** — the decorative textures (RGB565, no alpha)
   have real content (verified via dump), but the vUv1 mapping might sample a blank region.
3. **Draw order** — the overlay composites by submission order (no depth test). If the letter
   meshes draw AFTER the decorations and overwrite them (alpha=255 = opaque replace), the
   decorations would be hidden. But the decorations should still show where letters don't
   cover — unless the decoration geometry is very thin and entirely under the letters.

### Tooling added (additional, this session)

- `ZELDA3D_SG_DUMPTEX=all` — dumps ALL models' textures (was single-modelId only). Used to
  find the wordmark's model ID (2014) and verify title_all's decode is byte-exact.
- Oracle draw_log capture (`scratch/draw_log_wordmark.txt`) — 27K lines of per-triangle TEV
  register state at az=760. THE reference for what the oracle actually renders.

### Honest status

The investigation reached the mechanism (invisible decorative meshes) but did NOT fully
root-cause why the multiply combiner produces black output when the oracle's identical
formula produces visible gold. The earlier mat10 override claim was false and is retracted above.
The remaining decoration triangles with genuine dual-texture formulas still needed UV-level
instrumentation (dump the actual vUv1 values and sampled texels at a decoration fragment) to
close out. Not attempted this session — the budget went to the combiner/TEV analysis above.

## Addendum 2: ROOT CAUSE FOUND — coordinator-1 uses CameraSphereEnvMap, not UV

Continued the investigation. The mystery of "matching formulas, correct textures, correct
binding, yet dim/invisible output" was resolved by discovering that the wordmark's decorative
materials use **CameraSphereEnvMap** (noclip `TextureCoordinatorMappingMethod = 3`) for
coordinator-1, NOT `UvCoordinateMap` (1). SoH's shader was computing `vUv1` from `aUv`
(vertex texCoord0), but the oracle generates the second texture's UVs from the **view-space
normal** via sphere mapping (`uv = normal.xy * 0.5 + 0.5`).

For the wordmark's flat normals (mostly (0,0,±1)), sphere mapping samples the texture's
**center** — which is bright (RGB≈247,243,247 for i_ctex04c). The old UV-based sampling hit
random per-vertex UV locations that were dim (mean R≈29/255). The multiply `tex0 * tex1 * 2`
with a bright tex1 (≈0.97) produces `tex0 * 1.9` (visible), vs `tex0 * 0.11 * 2` (invisible)
with the dim UV-based tex1.

### The fix

- **CMB parser** (`cmb.cpp`/`cmb.h`): parse `coord1_mapping` from byte[2] of the coordinator-1
  entry (noclip `TextureCoordinatorMappingMethod`).
- **Group propagation** (`cmb_glgroups.cpp` → `zelda3d_gl.h` → `zelda3d_sdl3gpu.h` →
  `zelda3d_sdl3gpu.cpp`): thread `coord1Mapping` through to the SgGroup and set it on
  `uSheen.w` (previously unused) for dual-tex groups.
- **Vertex shader** (`kVert`): when `uSheen.w > 2.5` (sphere map), compute `vUv1` from the
  view-space normal (`normalize(mat3(uMV) * nM).xy * 0.5 + 0.5`) instead of from `aUv`.

### Verification

- `lus_tests`: 438 passed / 6 skipped (pre-existing), 0 failed.
- Fully-assembled frame (az=900/soh=1303): wm-warm pixels 8242 → 8542 (+4%), strict letters
  1530 → 1546. Small but real improvement — the sphere mapping only affects 5 of 17 decorative
  meshes (the dual-tex groups mat4/5/6/7/9).
- cs438 mid-fade (az=700/soh=1105): wm-warm 2204 → 2615 (+19%).
- The cs438 ratio (SoH 0.720→0.774 vs oracle 0.579) moved AWAY from the oracle — the sphere
  fix brightens cs438 decorations but not cs588 (where alpha=0, no decorations draw). This is
  expected: the fix is a correctness improvement (the UV computation was genuinely wrong), but
  the ratio metric is confounded by the coverage difference and the remaining unfixed decorations.

### Why the fix is partial

The sphere mapping fixes the dual-tex groups (mat4/5/6/7/9, 5 meshes). The remaining
decorations are still broken:
- **mat10/11** (zelda_logo_ev01/ev02): coordinator 0 is sphere-mapped and the authored CMB
  leaves texture units 1/2 disabled. The former claim that the game bound `tex1=tex0` at draw time
  was falsified by exact-cursor draw identities and the decompiled title draw (see Addendum 3).
- **mat3** (i_ctex04a, 3 meshes): simple MODULATE, no dual-tex, but the oracle's live TEV
  may also differ. Not investigated.

## Addendum 3 corrected (2026-08-30): mode 4 was fabricated; coordinator 0 is independent

The original Addendum 3 skipped the required decomp step and misidentified an unrelated
dual-texture draw as mat10/11. Exact cs1093 identities now match the ten host mat10/11 groups to
oracle draws by vertex count (`123,123,201,201,135,141,60,72,126,120`). Every matching oracle draw
reports `texEn=1/0/0`, with the authored two-stage chain
`MODULATE(PRIMARY,TEX0) x1` then `REPLACE(PREVIOUS)`. The binary-authored CMB agrees:
`tex1_idx=-1`, `coord0_mapping=3`.

The decompiled title draw in `oot3d-decomp/docs/title_logo_actor.md` §6 and
`build/decomp/001da4f4.c` writes alpha, private-light state, and transform before generic
submission. It does not rewrite TEV state or bind a second texture. Therefore the former
`kDualTexSelfSphereAdd`, `tex1=tex0`, and `3*PRIMARY*TEX0` path had no ground-truth basis and has
been removed. Commit `400faa57` is historical provenance for that wrong path, not parity evidence.

The faithful transport is narrower: coordinator 0's CameraSphereEnvMap method and transform travel
independently to TEX0, while coordinator 1 remains disabled and the generic evaluator consumes the
authored stages. Cached cs464/cs1093 comparisons improve simultaneously after that correction, but
title parity remains open; the historical “96%” aggregate was measured through the fabricated path
and cannot close the case.
