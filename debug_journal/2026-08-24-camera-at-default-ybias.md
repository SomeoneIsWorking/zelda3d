# Camera at-default upward-rise Y bias

## Symptom

OoT3D contains a camera-at smoothing term for fast upward movement that the SoH runtime did not
produce or consume. This is a structural behavior gap, not a pixel-tuning issue. It is normally
inert and only activates for a qualifying free walk/run rise.

## Reproducing tooling

The implementation still needs a headless live discriminator: put Link in the free walk/run action
on static collision, produce a rise of at least 9 world units in one observed update, and trace the
Player accumulator plus camera `at.y` while it decays. Pair the same state against the embedded
OoT3D oracle before calling the port verified.

## Root cause

`oot3d-decomp/docs/camera_calc_at_default.md` resolves the two missing halves:

- `FUN_00250AD0` compares `Actor.world.pos.y - Actor.prevPos.y`, requires
  `Player_Action_80842180`, rejects dynamic floors through
  `DynaPoly_GetActor(&play->colCtx, floorBgId)`, and normally keeps floor types 4, 7, and 12 on the
  separate stock slope branch. `Player_Action_8084E6D4` (get-item) is the exact exception: on one
  of those floor types it still selects the extra-Y reset/decay branch.
- A qualifying rise sets a private active latch and adds `rise * 100` to `Player::unk_6C4`; active
  state decays by 400 per authored 30 Hz update and clears at zero.
- `FUN_00338AC8` adds `unk_6C4 * -0.01` to the camera at target while the latch is active. The
  product makes the initial lag exactly the qualifying rise.

The earlier frontier note was wrong about `prevPos` being a spawn-only snapshot and the collision
lookup being an unidentified camera table. Typed decomp mapping resolves both.

## Dead ends

- A fixed vertical offset is not equivalent; the recovered value is rise-dependent and decays.
- Treating the raw 3DS offsets as SoH struct offsets is invalid; the port uses typed fields.
- Applying `-400` once per 20 Hz host update would slow the authored decay by one third.

## Fix / status

`behaviors/camera/at_default.{h,cpp}` now owns the producer/consumer behavior and per-player latch.
`behaviors/camera/at_default_policy.{h,cpp}` owns the pure branch predicate and is regression-tested
for all three slope floor types under ordinary and get-item actions. The oversized Player and camera
files only host narrow typed seams. An integer 30:20 accumulator preserves the authored decay cadence;
stock behavior remains in control for ordinary slope actions and when Zelda3D is disabled.

Status: **IN PROGRESS**. The SoH-owned Clang/GTest target `soh_behavior_tests` passes all three
branch cases (ordinary slope, get-item slope exception, and non-slope). Static formatting and diff
checks pass, but the shipping build and live oracle discriminator have not run. The host observes
motion at 20 Hz, so the exact threshold timing is still an explicit verification gap even though the
authored decay is rate-correct.
