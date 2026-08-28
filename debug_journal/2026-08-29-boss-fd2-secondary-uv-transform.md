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
