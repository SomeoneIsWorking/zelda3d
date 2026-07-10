# cs-438 fade "under-attenuation" — coordinator analysis (2026-07-10)

Follow-up to `2026-07-10-moon-epona-fade-attribution.md` §3 (oracle letter ratio 0.526 vs SoH
0.814 at cs438:cs588). Investigated directly (not delegated). Status: mechanism identified as
the leading hypothesis with supporting arithmetic; ONE discriminating measurement pending.

## Facts established this session

1. **The 3DS wordmark const5 RGB is genuinely white.** Decompiled draw fn
   (`<oot3d-decomp>/build/decomp/001da4f4.c` lines ~158-163) writes const5 =
   {pool RGB, +0x1D4·(1/255)}; dereferencing the literal pool in code.bin
   (ptr @0x001da8cc → 0x004d9914) gives RGB = (1.0, 1.0, 1.0). So the fade is alpha-only in
   the const register — no color ramp. (Falsifies the α² const-color theory.)
2. **The wordmark letter materials are vertex-lit with NON-black diffuse.** Combiner dump of
   `title_logo_us.cmb` materials 0-2 (letters, renderLayer=1):
   `isVertexLighting=1`, material `diffuse=(255,255,255,255)`, stage0 =
   MODULATE(PRIMARY_COLOR, TEXTURE0), stage1 RGB REPLACE / alpha MODULATE(PREV, const5.a),
   blend standard srcAlpha/1-srcAlpha. PRIMARY_COLOR comes from CmbVShader's vertex lighting —
   and unlike terrain (matDiffuse black), the letters' white diffuse makes the **animated sheen
   light direction (+0x1DC) contribute a real per-vertex diffuse term** on the letter geometry.
3. **Timeline**: at cs438 sheen t=0 (ramp starts cf466); at the cs588 reference t=1
   (saturated). So on the 3DS, full-display letters get a diffuse boost that mid-fade letters
   lack → the oracle's mid:full ratio is α × (1/(1+boost)) — with boost ≈ 0.1834·N·L on the
   letters' beveled normals this lands near the measured 0.526 for α=0.62 (pure α-blend over
   the measured backgrounds predicts ≥0.75, which the oracle is clearly below).
4. **SoH's measured 0.814 is exactly pure α-blend with NO sheen difference between the two
   frames** — i.e. SoH's `uSheen` term (zelda3d_sdl3gpu.cpp ~line 271, additive
   `shade *= 1+0.1834·max(0,N·L)`) is currently not differentiating cs438 from cs588 on the
   letter pixels (either ndotl≈0 for the letter normals under the overlay's flipped basis, or
   the boost applies equally/never).

## The discriminating measurement (next step, scoped)

Alpha is constant 255 from cs466 onward; the sheen ramps cs466→525. Therefore measure the
ORACLE's letter brightness at cs≈470 (sheen≈0) vs cs≈588 (sheen=1), same method as the 0.526
measurement:
- If the oracle's letters brighten measurably from cs470→cs588 (expected ~×1.1-1.18 if the
  sheen-diffuse theory is right) while alpha is provably constant → confirmed; the SoH fix is
  making the sheen term actually produce that same delta (check the light-dir transform under
  the overlay's RotateX(180°)+flipped-ortho basis — the same handedness trap that bit depth).
- If the oracle's letters do NOT brighten across the sheen ramp → theory falsified; the
  darkener at cs438 is something else time-varying (enumerate: csab assembly mid-flight
  letter orientation at cs438 — letters still animating until cf465+40? — measure orientation-
  independent pixels).

## Do NOT

- Do not add any compensating brightness/alpha constant to SoH's fade.
- Do not touch the ramp constants (byte-confirmed) or const5 plumbing (verified correct).

## Measurement result (agent)

Executed the discriminating measurement (2026-07-10, measurement-only agent). Captures:
`tools/title_ab.py ab 764 --soh 1172 --name sheen_disc_470` and
`ab 1000 --soh 1408 --name sheen_disc_588` (content mapping az = 2·cs − 176, soh = az + 408 per
the RE'd rate law in `tools/title_ab.py`; artifacts in `scratch/title_ab/sheen_disc_{470,588}*`).

Frame-identity guards: (a) the az→cs mapping is the byte-exact affine law (`az_cs = 88 + 0.5·az`,
verified previously by camera-eye inversion against the OP97 spline), so az=764→cs470,
az=1000→cs588 deterministically from `title_settled.state`; (b) visual timeline markers confirm
both instants — the cs470 frame shows the wordmark fully drawn with NO fire glow (sheen t≈0.07,
ramp starts cf466) and the cs588 frame shows the saturated glow plus the "© 1998-2011" copyright
fade-in, exactly the recheck_1000 framing. (A live `soh_titlecs` cursor read was skipped: the
single-instance harness lock was contended by a concurrent fix agent; the deterministic mapping +
timeline markers stand in.)

Method: same red-letter HSV-value approach as `2026-07-10-moon-epona-fade-attribution.md` §3
(hue ∈ [340,360]∪[0,25], sat>0.35, val>0.12 over a wordmark crop x[100:320] y[85:200] of each
400×240 pane), PLUS a 1-iteration binary erosion of the mask to drop antialiased letter borders.
Glow exclusion: the fire glow is orange/yellow (hue ≳25) so the red hue-gate excludes its body,
and the erosion removes letter-edge pixels where glow bleed could contaminate; a stricter
robustness variant (hue ≤ 15, sat > 0.5, erosion ×2 — letter cores only) was run alongside and
agrees, confirming no glow contamination in the baseline numbers.

| pane | cs470 mean V | cs588 mean V | factor 588/470 |
|---|---|---|---|
| oracle (Az) | 0.431 | 0.602 | **×1.40** |
| SoH | 0.273 | 0.274 | **×1.01** |

(Strict-mask variant: oracle 0.440 → 0.605 = ×1.38; SoH 0.269 → 0.269 = ×1.000 — SoH's letter
cores are bit-flat across the ramp.)

**Verdict: sheen-diffuse CONFIRMED as the visible modulator.** With alpha provably constant at
255 across cs470→cs588 (wordmark ramp ends cf465), the oracle's letters still brighten by ×1.40 —
even more than the ~×1.1–1.18 first-order prediction (the analysis' 0.1834·N·L estimate was a
lower bound using the raw sheen scale; the measured excess says the letters' beveled normals catch
the animated light harder than the flat-facing assumption, and/or the vertex-diffuse term is not
the only sheen-coupled contribution — worth reading the shader term exactly during the fix). SoH
over the same pair is ×1.01 (×1.000 on letter cores) — zero sheen modulation, exactly the
predicted gap. The fix target stands as analyzed: make SoH's `uSheen` diffuse term actually vary
on the wordmark letters across the ramp (check the light-dir transform under the overlay's
RotateX(180°)+flipped-ortho basis).

Caveats: (i) the SoH pane rendered with the 4K logo texture pack active (TEXPACK=off was not in
the capture env) — harmless here because the within-engine 588/470 ratio cancels any static
texture/color bias, but absolute SoH V values (0.27 vs oracle 0.43) are not comparable across
engines for that reason (plus the known scene-exposure mismatch); (ii) the oracle factor
1.40 > the predicted band, so treat 1.18 as a floor, not the target, when verifying the fix.
