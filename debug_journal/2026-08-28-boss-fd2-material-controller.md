# BossFd2 material controllers are actor-local, not scene-frame modulo

## Cause

The hole-form port sampled `valbasiagnd.cmab`, `valbasia_firehair.cmab`, and
`valbasiagnd2.cmab` from `play->state.frames % duration`. OoT3D owns separate
material controllers constructed at actor/model-instance initialization:

- body and all fire-hair instances start at frame zero, loop, and receive step
  `2.0` on each 30 Hz draw;
- the exposed-face pulse starts at zero, resets on the typed
  `BossFd::faceExposed` rising event, advances by `1.0` per 30 Hz draw only while
  that state is active, and freezes when cleared;
- controller tick applies the increment before sampling and wraps on
  `frame >= duration`.

Addresses and derived C are in `oot3d-decomp/docs/boss_fd2.md`. The port now
keeps these clocks in a focused actor-local controller and advances them through
the exact 30:20 scheduler instead of inheriting scene age.

The shared `valbasia_firehair.cmb` material is authored with
`vertex_lighting=0`. Both flying and hole-form mane submissions now use an
explicit force-unlit draw option. The interpreter also makes bit 29 authoritative
over bit 31, matching the existing handle contract.

## Checks and falsifiers

- `boss_fd2_material_controller_test` demonstrates both 3:2 outcomes, hidden
  draw freezing, pulse event reset, active-only pulse advance, and clear freeze.
- The Clang SoH core build compiles the extracted material owner and real draw
  call sites.
- Paired capture `scratch/screenshots/fd2_ground_paired_20260828_z.{az,soh}.png`
  exercises the shipping route after the change. Relative to the prior `_y`
  host frame, the output changed (different SHA-256; cropped head luma changed
  from 41.00 to 29.24 and body from 8.94 to 9.53), proving the actor-local
  clocks reach the renderer. The matching oracle capture is byte-identical
  between `_y` and `_z`, so this is not oracle launch noise.

This does **not** close the remaining brightness residual. At this sampled
phase the oracle crop remains brighter (head 71.69, body 23.23). Fire-hair phase
can only explain time-varying mane tint, and its generic TEV RGB chain does not
consume PRIMARY; it cannot explain the stable full-body deficit.

An attempted synchronization of the SoH environment slot to the oracle's slot
zero produced a byte-identical host capture and was removed. That hypothesis is
falsified; do not reintroduce a palette-slot force or a fitted brightness gain.
