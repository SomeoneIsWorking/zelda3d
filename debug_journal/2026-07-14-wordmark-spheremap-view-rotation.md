# Wordmark decoration sphere-map: live cs-camera view rotation ported (2026-07-14)

Closes the root cause diagnosed earlier today
(`2026-07-14-title-cs464-wordmark-and-composition-and-fireglow.md` Divergence 1): the
wordmark's gold-outline decorations (mats 4-11, CameraSphereEnvMap coordinators, ported
2026-07-11 in `efa336cd`/`400faa57`) computed their sphere-map UV from `mat3(uMV)*n` where
uMV is the title 2D ortho overlay's FIXED placement matrix (no camera), so the sampled UV
was camera-independent — systematically near the gradient textures' center, producing the
measured brightness/hue overshoot (meanR 0.686 vs oracle 0.420 at the cs466 isolation
frame) and severely under-covered strict-red letter pixels at every cs frame.

## Ground truth

- On the 3DS these decorations are a normal scene draw: placement basis has IDENTITY
  rotation (`title_logo_actor.md` §6.1), composited through the LIVE cs-camera's view
  matrix, so the sphere-map unit's view-space normal = R_view * n_model, camera-varying.
- View basis convention: the decompiled 3DS LookAt `FUN_002d9e68`
  (`oot3d-decomp/docs/title_view_matrix_lh.md`): fwd = normalize(eye-at),
  right = normalize(up x fwd), up' = fwd x right; rows = (right, up', fwd). (Algebraically
  identical to the standard RH gluLookAt rotation rows — verified symbolically, so there is
  no separate handedness knob to tune.)
- Sphere-map formula: noclip render.ts (decompiled from this binary) CalcTextureCoordRaw
  mode 3: `uv = viewNormal.xy*0.5+0.5`, then the coordinator's OWN texture matrix, then the
  same `t = 1-t` texture-space y-flip every other coordinator path gets. SoH's previous
  sphere branch had neither the y-flip nor the coordinator transform (identity for all
  wordmark decorations except mat4, coord1 scaleT=2 — dumped from the ROM CMB this session).

## The fix (per-model override, mirroring the existing lightDirOv pattern)

- `zelda3d_sg_ubo.h`: +uSphRot0/1/2 (rows of the view rotation; uSphRot0.w = gate) before
  uBones; offsets updated in `tests/zelda3d_render_tests.cpp`; size-parity mirrors added to
  `unified_ubo.h`/`unified_shader.cpp` (uBones 400→448).
- `zelda3d_sdl3gpu.cpp` kVert: sphere branch uses uSphRot rows when gated (else the old
  mat3(uMV) path — every non-overlay draw unchanged), then applies the coordinator
  scale/trans and the texture-space y-flip identically to the UV-coordinate branch.
- `zelda3d_gl.{h,cpp}`: `Zelda3D_GL_SetSphereMapViewRot(modelId, m9)` (+Clear), direct-read
  contract like SetLightDirOverride; threaded through DrawItem/Submit →
  `Zelda3D_Sg_DrawModel(..., sphRotOv)`.
- `title_logo.cpp`: computes the FUN_002d9e68 basis from this frame's ported OP97 spline
  camera (play->view.eye/lookAt/up, already verified 0.00 vs Az) and sets it for the
  wordmark model each draw.

## Verification (harness, exact-frame-locked captures via title_sbs_verify.py)

Box-scoped (logo box x110-300 y40-190) strict-red letter mask (r>90, r>1.6g, r>1.6b),
oracle-normalized:

| frame | metric | oracle | SoH BEFORE | SoH AFTER |
|---|---|---|---|---|
| cs464 | red px coverage | 1.0 | **0.176** | **1.040** |
| cs464 | red R mean | 99.7 | 171.6 | 134.0 |
| cs464 | whole-frame content score | — | 0.6564 | **0.8306** |
| cs464 | best-shift SSD | — | 1054 | 313 |
| cs1093 | red px coverage | 1.0 | **0.173** | **0.900** |
| cs1093 | red saturation mean | 0.811 | 0.789 | **0.811** (exact) |
| cs1093 | red R mean | 158.9 | 208.1 | 196.2 |

Pure-glow regions at cs1093 (no letters/text in crop) also moved toward oracle:
below-shield warm px 246→398 (oracle 518), left-halo warmth 20.1→32.5 (oracle 45.7).

Intermediate result kept honest: the first cut (rotation only, no y-flip) already brought
cs464 coverage to 1.04 but pushed cs1093 whole-frame red to 128% of oracle; adding the
y-flip + coordinator transform (both derived from the noclip/UV-path convention, not
fitted) improved both frames simultaneously (cs464 score 0.81→0.83, cs1093 red 7200→6919
whole-frame). Two different camera angles improving together is the anti-overfit check.

No regressions: lus_tests 438 passed / 6 skipped (pre-existing) / 0 failed; the
upside-down-flip class of bug (zelda3d_overlay2d.cpp history) is placement-side and
untouched — both captures show the wordmark upright at two different camera angles.

## Remaining residual (open, smaller)

At cs1093 SoH's letter interiors carry more green than the oracle's and isolated pixels clip to
white where the oracle shows gold. The 2026-08-30 exact-identity audit later falsified the mode-4
explanation recorded here: mat10/11 have only TEX0 enabled and use their authored
MODULATE→REPLACE chain. Their coordinator-0 sphere map remains real, but it must not imply a second
sampler or `3*TEX0` combine. Cached evidence with independent coordinator-0 transport improves both
cs464 and cs1093 while leaving an open residual. Also the glow outer halo at cs1093 remains smaller
than oracle (below-shield 398 vs 518) — partially D3 territory, remeasured there.
