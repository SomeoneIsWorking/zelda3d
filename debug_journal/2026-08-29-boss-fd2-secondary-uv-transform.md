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

On 2026-08-29, the deterministic BossFd2 setup was rerun with the software oracle path after
rebuilding the shipping renderer. The host reported `body=loaded ... UV sampled 3/3 materials` for
the live body controller. The paired output is `scratch/screenshots/fd2_oracle_base_fixed2.{az,soh}.ppm`
with per-group suppression captures `fd2_oracle_skip29_fixed2` through `skip38_fixed2`.

The fixed actor crop `(x=360..440, y=180..460)` contains the same centered body in both engines.
Its non-black means are oracle `(142.3,65.8,14.4)` and host `(110.5,39.4,3.5)`; crop RMSE is
`38.646744` over 22,400 pixels. This is evidence that the new UV override is live and that the
remaining body darkness is not closed by it. It is not a parity claim: the next BossFd2 material
change still needs a named renderer or decomp mechanism and a new matched capture.
