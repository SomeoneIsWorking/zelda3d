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
