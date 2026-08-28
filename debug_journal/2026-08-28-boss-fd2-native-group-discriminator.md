# BossFd2 native material-group discriminator

Registered instrument: `I042`.

## Diagnostic root cause

The existing host `sgdrawonly` control and oracle `drawskip` control did not make the same
experiment. The host-only frame removed every surrounding draw, while the oracle skip frame retained
the complete compositing context. A brightness difference between those captures could therefore be
caused by overdraw/blending rather than the selected BossFd2 material group.

The renderer now has a generic `sgdrawskip` control that suppresses one per-frame Zelda3D group and
composes with the existing stable model filter. `sgdrawlist` also reports the CMB model-local group
and material indices, so the seven `valbasiagnd.cmb` groups can be classified without texture-pointer
inference. The embedded harness exposes both operations as `soh_drawlist` and `soh_drawskip`; the
ordinary SoH REPL exposes `sgdrawskip`. This is instrumentation at the native draw-submission owner,
not a BossFd2 shader/material exception.

## Controlled capture

For each group, restore the same paired checkpoint and camera, arm the oracle `drawskip` index and
the independently listed host `soh_drawskip` index, render the same controlled frame, and capture the
paired PPMs. Capture an unmodified base and a second unmodified repeat from that same checkpoint
first. The two base images for each engine must be byte-identical; otherwise animation, camera, or
effect motion exceeds the instrument and no group conclusion is valid.

Fresh host processes randomize the hole actor's world position. The harness control accepts
`force bossfd2_mane_sync <x> <y> <z>` for a caller-selected finite position; omitting the coordinates
preserves the oracle-selected behavior. Every restored group capture must use the same explicit
position before its host base hashes can be compared.

For each engine and group, the changed-pixel mask is the exact nonzero RGB difference between its
base and skipped frame. Report mask population, base-frame mean luminance on that mask, and mean
signed/absolute base-minus-skip luminance. Compare the host/oracle results by the logged CMB material
identity, not by their unrelated global draw indices. Materials 0, 1, and both material-5 groups are
the authored UV1-dependent class; materials 2, 3, and 4 are the negative-control class.

## Exact falsifiers

- The claim that the remaining darkness is confined to the UV1-dependent mechanism is falsified if
  any material-2/3/4 group has a nonempty changed-pixel mask and its host/oracle luminance residual
  exceeds the byte-identical repeat control. It is supported only if all three negative-control
  groups stay within repeat-control noise while at least one material-0/1/5 group does not.
- A body-wide native lighting/input explanation is falsified by the inverse result: every
  material-2/3/4 group stays within repeat-control noise while the residual occurs only in the four
  material-0/1/5 groups.
- An empty group mask, a non-identical repeated base, or a listed group/material mapping that changes
  across checkpoint restores invalidates the capture instead of counting as parity.

## Runtime result

The fixed-position protocol was executed headlessly on 2026-08-28. Two fresh restores produced
byte-identical bases for each engine:

```text
oracle 92b19296713b05b4701c8b37d5ea31c31bc140787ec390fe17dec8da6b04d33b
host   133a70872b81a3bd3ef79dfe83108be3956625f2b8f3e6507f974dc496528f4f
```

The native body list was stable across those restores and mapped to the oracle's PICA log by
expanded vertex counts:

```text
host draw 37 group 0 material 1 -> oracle draws 29,30 (537 + 165 = 702 vertices)
host draw 38 group 1 material 2 -> oracle draw 31 (198 vertices)
host draw 39 group 2 material 0 -> oracle draws 32,33,34 (417 + 417 + 240 = 1074 vertices)
host draw 40 group 3 material 3 -> oracle draw 35 (120 vertices)
host draw 41 group 4 material 4 -> oracle draw 36 (498 vertices)
host draw 42 group 5 material 5 -> oracle draw 37 (258 vertices)
host draw 43 group 6 material 5 -> oracle draw 38 (102 vertices)
```

The oracle skip masks were nonempty for draws 29, 30, 31, 32, 33, 35, 36, and 37; draws 34 and 38
were fully occluded in the presented frame. The corresponding host skips 37 through 42 were
nonempty; host draw 43 was fully occluded. This proves the mapping and that the controls reach the
rendered body, but it does not yet prove a material cause: all visible groups participate in the
composite, and the two engines use different global screen placement and texture-pack state. The
next graphics step is a matched per-material output comparison at the corrected camera, not a
group-specific shader tweak.

## Integration validation

The combined Clang build of `lus_tests` and `soh_core` passes, the focused renderer gate passes 9/9,
and the embedded harness rebuilds. A live forced-ground frame produced one exact seven-group body
candidate, model 2018, with the following draw/group/material mapping:

```text
37/0/1  38/1/2  39/2/0  40/3/3  41/4/4  42/5/5  43/6/5
```

That is the asset's complete material signature and therefore avoids a stale hard-coded model ID.
Skipping draw 37 through `soh_drawskip` changed 29,009 RGB channels in the following host capture,
so the control has demonstrated the other answer and is not inert. Those two frames were sequential,
not restored from one checkpoint, so their pixel delta validates control delivery only; it is not a
material-parity measurement. The oracle was not advanced or skipped between those snapshots and its
two PPMs were byte-identical (0 changed channels), proving the paired capture itself was stable while
leaving host repeat stability unproven. The matched repeated-base capture protocol above remains the
required gate before drawing a UV1-versus-lighting conclusion.

## Generic TEV source-layout correction

The oracle's `vsuni_log` and Azahar's `TevStageConfig` then exposed an independent shared-renderer
defect. PICA packs the three RGB source selectors into bits 0/4/8 and the three alpha selectors into
bits 16/20/24; the four-bit gap is present only in the source word. The host `PackTevStage` and
`tevRun` had incorrectly used 12/16/20, which is the alpha-modifier layout, not the alpha-source
layout. For `valbasiagnd` material 1 the corrected host words are byte-compatible with the oracle:

```text
host after fix: 0e300430, 0e1f0e43, 0e1f0edf, 0e1f0eef
oracle records: e300430,  e1f0e43,  e1f0edf,  e1f0eef
```

The RGB fields were already correct; therefore this fixes generic TEV alpha behavior without being
claimed as the explanation for the remaining opaque BossFd2 body brightness. A CMB close-test now
asserts all four material-1 source words, and a shader-source test asserts the 16/20/24 decode. The
native harness also exposes `soh_sgdump <modelId>` so this boundary can be inspected on the same
fixed checkpoint. The body residual remains open for a separate material/texture comparison.

## Fragment probe extension (2026-08-28)

The existing oracle `PIXEL` probe was extended in the local Azahar harness patch to include the
texture-unit-1 sample (`tex1col`) alongside `texcol`, `primary`, and `combined`. At the fixed
material-1 address `tex0=0x180bde00`, 1,524 samples measured linear 8-bit means:

```text
tex0     (93.260, 29.911, 11.373, 255)
tex1     (131.835, 85.491, 66.180, 255)
primary  (107.285, 53.755, 16.251, 255)
combined (138.288, 28.182,  4.222, 255)
```

The host received a generic `ZELDA3D_SG_FRAGDBG=14` tap for `texture(uTex1,vUv1)` so the same
sample boundary can be measured through SDL3GPU. The first host paired runs exposed a separate
tooling failure: after `force bossfd2_mane_sync 0 -850 0`, later fresh processes sometimes reported
the oracle rendered anchor at `z=240` while the host anchor stayed at `z=0`. Those camera-mismatched
frames are rejected as parity evidence. No material, lighting, or gain change is justified until
the checkpoint restores the same rendered anchor on both sides.

The host probe was then changed to the generic `force camera` command, which writes the same explicit
eye/at/FOV into both engines and removes anchor-derived camera placement from the measurement. With
that control, host `soh_drawskip 37` changed the model-2018 material-1 surface and overlapped 1,588
pixels of the oracle material-1 union mask (1,656 pixels), so the draw boundary is real. A nearest-
texel host tap (`ZELDA3D_SG_FRAGDBG=15`) was added beside the filtered TEX1 tap (`=14`) as a sampler
discriminator; it is diagnostic only and does not change the shipping sampler.

One subsequent A/B was rejected: the oracle checkpoint had been saved with the texture pack enabled
while the host side was run with it disabled. The host-off run also hit a 240-unit posed-root
divergence on the next controlled mane step. Future material numbers must use a newly generated
checkpoint and the same `ZELDA3D_HARNESS_TEXPACK` mode on both sides, with the root-control result
matching before the framebuffer is interpreted.
