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

## Actor-local clock caveat

A one-shot `SG_DUMP` audit captured host material 1 with `uvOv=(2.266667,0)` and effective
`uTex1Xf=(0.5,1,4.533333,0)`. The same run's oracle log contained a material-1 matrix translation
of `-0.5666667`, but that is not a valid transform mismatch: the setup script advances SoH with
`soh_step` independently of the oracle's `run` cursor, so the two records are different authored
material frames. The only frame-matched transform check remains the earlier authored-frame-14
capture: CMAB U `0.4666667` produced oracle matrix translation `-0.4666667`, and the host's
pre-scale representation (`0.4666667 / 0.5`) emits the same matrix translation. Do not use an
unsynchronized SG/oracle pair to alter the generic UV conversion.

## Draw-isolation and primary probe audit

The body-group probe was narrowed to the mapped host draw indices from the native group
discriminator. `ZELDA3D_SG_DRAWONLY=37` (material 1) produced no model pixels when the surrounding
depth/compositing draws were removed; this is an invalid material comparison, not evidence that the
group is absent. The same exact-frame probe with `ZELDA3D_SG_DRAWONLY=39` (material 0) produced the
expected body surface, so the isolation control is live but cannot replace the full compositing
context for every group.

The stable oracle selector is `SOH3D_PIXEL_XY=240,195`; `SOH3D_PIXEL_TEX` values from prior runs are
not reusable because the logged texture addresses are process-local. At the synchronized checkpoint,
the oracle material-1 fragment at that coordinate reported `primary=(97,51,12)` and
`combined=(255,86,22)`. The host material-0 primary-only isolation over the same displayed body
region averaged `(94.62,46.57,14.61)`; this is a different material/group and is directional evidence
only. It does not justify a lighting or TEV change. The remaining body residual still requires a
same-material, same-compositing capture before a renderer patch.

## Selected fragment probe boundary (2026-08-29)

The host renderer now accepts `ZELDA3D_SG_FRAGDBG_DRAW=<per-frame draw index>` alongside the existing
`ZELDA3D_SG_FRAGDBG=<mode>`. The selected draw carries a renderer-only `uDebug` gate in the shared
native/unified UBO; all other groups keep their normal shader path and the selected tap runs in the
original draw order. For the diagnostic early return only, the probe writes front depth because
Azahar's `PIXELXY` instrumentation is explicitly pre-depth-test. The shipping path remains unchanged
when the variable is absent.

The companion `ZELDA3D_SG_DRAWSKIP_AFTER=<n>` / `soh_drawskipafter <n>` control suppresses later groups
while preserving earlier scene depth. A live Clang-built harness run mapped draw 39 to BossFd2
model 2018 group 2/material 0 and, with `FRAGDBG=5`, `FRAGDBG_DRAW=39`, and `DRAWSKIP_AFTER=39`,
returned the expected host body mask: crop `(360,180)-(440,460)` had 6,504 nonblack pixels with
mean `(94.62,46.57,14.61)` and display point `(400,234)` was `(105,53,16)`. This validates the
selected tap and the context-preserving cutoff. The corresponding draw-37 material-1 probe was
submitted (`model=2018 group=0 material=1`) but produced zero host pixels even with front depth and
`DRAWSKIP_AFTER=37`; the group has no rasterized coverage at this pose, so that result is not a
material mismatch. The next comparison must use a pose/pixel where host material 1 rasterizes, or
an equivalent oracle/host geometry boundary; no material or lighting change is justified yet.
