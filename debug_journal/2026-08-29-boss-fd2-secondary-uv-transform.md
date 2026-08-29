# BossFd2 secondary UV transform

The BossFd2 body mismatch was in the generic coordinator-1 transform handoff. Texture decoding,
texture-pack selection, and the packed TEV stages were not the cause.

Ground truth:

- `valbasiagnd.cmab` has a `Translation` U track on channel 1 for materials 0, 1, and 5,
  duration 120, with values `0 -> 4` (`4/120` per authored frame).
- At the paired oracle checkpoint, the live vertex uniforms were
  `TexMtx1[0] = (0.5, 0, 0, -0.4666667)` for material 1 and
  `TexMtx1[0] = (1, 0, 0, -0.4666667)` for material 2. The shared translation is the negative of
  the sampled CMAB value at authored frame 14; the material-specific scale remains in the matrix.
- The host shader represents baked coordinator translation as
  `scale * (uv - preScaleTranslation)`. Therefore an animated runtime matrix translation `-u`
  must be installed as `preScaleTranslation = u / scale`, replacing the baked translation. Adding
  the sampled value to the baked translation was incorrect and generic TEV did not route the
  override into `uTex1Xf` at all.

The SDL3GPU pass now applies this conversion to material UV overrides in both generic TEV and
dual-texture paths. Materials without an override retain their existing baked transform and
draw-level offset behavior. The conversion is shared and scale-aware; it is not a BossFd2-specific
constant.

Evidence sources: `oot3d-decomp/docs/boss_fd2.md`, the paired oracle `vsuni_log`/`PIXELXY` capture in
`scratch/logs/`, the static `valbasiagnd.cmb`/`.cmab` parse, and the host's nearest-texel probe.

## Live post-port check

The first 2026-08-29 post-port capture was not valid evidence for a colour residual: the oracle and
host clocks were different (`az_daytime=0xf483`, `soh_env daytime=0x2c45`). Its crop means and RMSE
are therefore retired rather than used to drive a renderer change. The live host diagnostic did
confirm `body=loaded ... UV sampled 3/3 materials`, and the paired skip captures remain useful for
the draw-discriminator work.

A synchronized rerun forced `0x6000` on both engines and reported the same camera basis. A bounded
pre-fog TEV probe on the fixed crop `(x=360..440, y=180..460)` measured oracle `(57.7,26.2,5.7)`
and host `(54.8,17.8,4.4)`. This is a diagnostic comparison, not a parity claim: the probe exits
before the final fog path and the crop includes geometry that is not yet proven identical. The
remaining material residual still needs a named renderer or decomp mechanism before any further
change.

## Actor light-bank audit

The synchronized 2026-08-29 checkpoint falsified the apparent native light-bank mismatch. The
oracle's BossFd2 registers are view-space, while the host UBO dump reports world-space directions.
At the matched forced camera, oracle `dir0=(0,0.98995,0.14142)` transforms to world `(0,1,0)` and
oracle `dir1=(0,-0.8891,-0.4577)` transforms to `(-0.3274,-0.9449,0)`, matching the host's
original `uLightDir2` and `uLightDir` values. The temporary negation experiment was therefore
removed. The light-bank binding remains ruled out for this residual; the remaining comparison
must target material/TEV or another renderer mechanism after both clocks are synchronized.
