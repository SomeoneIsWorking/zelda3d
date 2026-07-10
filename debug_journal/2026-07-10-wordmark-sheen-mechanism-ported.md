# Wordmark sheen — real mechanism RE'd and ported (2026-07-10)

Closes the `2026-07-10-fade438-sheen-diffuse-analysis.md` arc: the oracle's letters brighten
×1.40 across the sheen ramp (cs470→cs588, alpha constant) while SoH was bit-flat ×1.000, and
the documented light env (ambient {1,1,1,1}, diffuse {0.1834}) could not produce more than
×1.18. Root cause: the documented slot-color ROLES were swapped, the diffuse constant was a
misread, and SoH's port additionally dotted the normal with +L where PICA uses −L.

## RE result (full derivation in `<oot3d-decomp>/docs/title_logo_actor.md` §6.6)

Three independent legs, all byte-/register-exact:

1. **Pool bytes** (`code.bin`, ptr `0x001da8d0` → `0x004d9924`): the light-env colors are
   `{1,1,1,1} {0.18,0.18,0.18,1} {1,1,1,1} {0,0,0,1}` — the grey is **0.18 exactly**
   (`0x3E3851EC`), not 0.1834.
2. **Shader** (`/CmbVShader.shbin`, full disasm): vertex-lit path is
   `o1 = Σ_i matAmb·lightAmb_i + max(0, dp3(−LightDir_i, N))·matDif·lightDif_i`
   (×vColor only when HasColor — FALSE for the letters). No specular term exists in this
   path; no clamp before output (PICA clamps at the TEV input). Note the **negated** dir.
3. **Runtime uniform read-back** (NEW tooling — Azahar Patch 5 + harness `vsuni_log`,
   `tools/soh3d_harness/AZAHAR_PATCH.md`): at the wordmark draw (az=1000):
   `hasCol=0 vLit=1 fLit=0 matDif=(1,1,1,1) matAmb=(1,1,1,0)
   dir0=(0.57735,−0.57735,−0.57735,1) dif0=(1,1,1,1) amb0=(0.18,0.18,0.18,1)`,
   lights 1/2 disabled. So the slot's FIRST color (white) is the **diffuse** and the
   SECOND (0.18) is the **ambient** — reverse of the old §6.3 labels. The guest-RAM light
   slot (found via memscan at ctx `0x0820ca9c`) holds dir `(−0.64838, 0.64838, −0.399)` at
   az=764 and `(0.57735,−0.57735,−0.57735)` at az=1000 — bit-exact vs
   `normalize(2t−1, 1−2t, −0.5−0.5t)`, no basis transform.

**Ground-truth letter shading** (letters are perfectly flat — all `title_logo_us.cmb`
mats-0-2 normals are exactly (0,0,1)):

```
shade(t) = clamp(0.18 + max(0, dot(N, −L(t))), 0, 1)
pixel    = texel.rgb · shade(t)          (alpha untouched by the sheen)
```

Predicted cs470→cs588 factor = 0.757/0.579 = **×1.308** vs oracle-measured ×1.40
(baseline mask) / ×1.38 (strict letter-core) — within the ±0.1 acceptance bar of the
strict measure; the small residual is fire-glow bleed into the letter mask (the glow ramps
over the same cs466–525 window), not letter shading.

## Why SoH was flat ×1.000 (two independent bugs in the old port)

- `zelda3d_sdl3gpu.cpp` dotted `normalize(vNrmView)` with **+L**; PICA dp3 uses **−L**
  (`dp3 r3.x, -c80, r14`). With the letters' +Z normals the +L dot is negative →
  `max(0,·)` = 0 → the term never fired at all.
- Even had it fired, the shape `shade *= 1 + 0.1834·ndotl` (ambient-1 + small diffuse
  boost) caps at ×1.18 — the real term is `0.18 + ndotl` (dominant DIRECTIONAL term).

## The port

- `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp`: wordmark branch now
  `shade *= clamp(uSheen.x + max(0, dot(N, −L)), 0, 1)` with `uSheen.x = 0.18`
  (`kWordmarkLightAmbient`, byte-verified). Per-draw scoped exactly as before (only the
  wordmark's own draws set the light-dir override). Basis safety: N and L both go through
  `mat3(uMV)`, so the dot is invariant under the overlay's RotateX(180°)+reversed-ortho.
- `zelda3d_sg_ubo.h` + `title_logo.cpp`: comments corrected (falsified constants marked).
- lus_tests: 438 passed, 6 env-gated skips, 0 failed.

## Verification (TEXPACK=off, method identical to the prior journal entries)

Discriminator (az=764/soh=1172 vs az=1000/soh=1408, red-letter mean V over the wordmark
crop, 1-iter erosion; strict = hue≤15, sat>0.5, erosion×2):

| pane | cs470 mean V | cs588 mean V | factor 588/470 |
|---|---|---|---|
| oracle (Az), val>0.12 (journal's original gate) | 0.431 (n=3854) | 0.602 (n=3755) | **×1.396** |
| SoH, val>0.12 | 0.270 (n=1037) | 0.208 (n=2654) | (0.770 — MASK ARTIFACT, see below) |
| oracle (Az), val>0.06 (mask-converged) | 0.414 (n=4229) | 0.577 (n=4181) | **×1.393** |
| SoH, val>0.06 (mask-converged) | 0.167 (n=2657) | 0.208 (n=2654) | **×1.248** |

The original val>0.12 gate is unusable on the POST-fix SoH pane at cs470: the now-correctly-dimmed
letters (shade≈0.59 on SoH's darker absolute exposure) straddle the gate, so 61% of the letter
pixels drop out and the survivor mean is the bright tail (0.270) — a selection artifact, not a
brightness. Lowering the gate to 0.06 converges the mask (n stable at ~2657 for BOTH frames, and
gate 0.04 gives identical numbers) and leaves the oracle factor unchanged (1.393 vs 1.396), so it
measures the same thing. Result:

- **SoH: ×1.000 (bit-flat) → ×1.248** — past the ≥1.18 bar; and 1.248 is within 0.03 of the
  ported model's own prediction for these exact frames (shade(t=17/255)=0.591 → shade(1)=0.757 =
  ×1.28). The port does exactly what the derived mechanism says.
- Oracle ×1.39 vs its own model prediction ×1.31 — the same ~0.08-0.10 excess attributed to
  fire-glow bleed (present in the oracle's brighter pane; negligible in SoH's darker one).
- Content-match scores also improved at both anchors: az=764 pair 0.4650 → **0.7302**,
  az=1000 pair → 0.8367.

cs438 ratio (az=700/soh=1108 vs az=1000/soh=1408, method of
`2026-07-10-moon-epona-fade-attribution.md` §3):

| pane | cs438 mean V | cs588 mean V | ratio 438/588 |
|---|---|---|---|
| oracle (Az), val>0.12 (original method) | 0.305 (n=4294) | 0.580 (n=2014) | **0.526** (bit-identical to the pre-fix measurement) |
| SoH, val>0.12 (original method) | 0.224 (n=1089) | 0.291 (n=1496) | **0.769** (was 0.814) |
| oracle (Az), val>0.06 | 0.305 | 0.527 | 0.579 |
| SoH, val>0.06 | 0.198 | 0.290 | 0.681 |

SoH's cs438 ratio moved 0.814 → 0.769 (original method), toward the oracle's 0.526; on the
mask-converged gate the gap is 0.681 vs 0.579. Direction and magnitude are what the mechanism
predicts: the sheen term explains the cs588-side brightening; the REMAINING cs438 gap is the
mid-fade alpha-composite-over-background difference (wordmark alpha ≈162/255 at cs438; the panes
composite over visibly different backgrounds/exposures), which is a separate axis from this fix
and was already flagged in the fade-attribution journal.

## Tooling added (reusable)

- **Azahar Patch 5** (`tools/soh3d_harness/AZAHAR_PATCH.md`): per-draw VS-uniform log at
  `trigger_draw` (c8/c9, c80–c88, b5/b9/b10) — reads back EXACTLY what the game wrote into
  the CmbVShader lighting uniforms for any draw. Harness REPL: `vsuni_log <path>` / `off`.
  This is the generic answer to "what does the 3DS lighting actually receive" — it also
  directly showed the terrain draws receiving the SAME scene ambient in slots 0 AND 1
  (the ×2 terrain-ambient mechanism of `title_env_lighting.md` §10, now runtime-confirmed).
- `scratch/decomp_agent/{vsuni_capture2,slot_dump2,predict_factor,measure_letters,measure_cs438}.py`.

## Dead ends recorded

- Static hunt for the CPU fill of c80–c88 (the function reading the light-env slot and
  writing the vertex uniforms): the uniform-name table (`0x54c8d0`, 34 entries) and the
  resolve-by-name fn (`FUN_003fc218` → `FUN_00310844`, a DMPGL glGetUniformLocation-alike)
  were found, but the VALUE writer is vtable-dispatched and was not located statically —
  the runtime uniform log answered the question faster and byte-exactly. Don't re-chase
  the static path unless the writer itself is needed.
- `FUN_003fa5d0` (title_env_lighting.md §11) is the HARDWARE fragment-lighting fill, not
  the vertex-uniform fill — but its material-color pairing (+0x88·matDiffuse,
  +0x98·matAmbient) independently confirms the slot-color roles.
