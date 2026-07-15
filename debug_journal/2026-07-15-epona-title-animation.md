# Title-cs Epona gait "looks off" — RE'd, gameplay-approximation removed, tempo hypothesis ruled out

User report: after the model-render fix (commit `db0696d8`), the title-cs Epona's ANIMATION/gait
"looks off". No prior kanban card for this specific report; a prior mane/tail claim
(`2026-07-15-epona-mane-tail-already-csab-driven.md`) had already been retracted as a
misattribution, so the animation mechanism was treated as unverified going in.

## Step 1 — reproduce & quantify

`tools/title_rider_crop.py` (rider-centered zoomed SxS vs the embedded Az/3DS oracle, camera-
projected via each engine's own live camera) across the gallop window (cs 1400-1620,
`scratch/title_ab/gallop_sweep_*`). Camera-projection of SoH's own rider (`soh_px`) came back
`None` at every sampled frame — the embedded harness's `compare player`/`soh_player_pos` reads
`gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER]`, which returned empty at these instants in the
harness's own SoH linkage (a harness-side gap, not evidence of a missing Player actor in the real
game — not investigated further this session, flagged as an open tooling gap below). Fell back to
full-frame crops, still legible since the rider is only ~40px in the 400x240 capture.

Full-frame + hand-cropped grid comparison (`scratch/title_ab/rider_zoom_1560_1600.png`) at
cs1560 and cs1600: pose is visually near-identical to the oracle at cs1560; at cs1600 SoH's front
leg reads slightly more raised than the oracle's. No sawtooth/freeze/stuck-bind-pose signature —
the divergence, if real, is subtle (a fraction of a gait-cycle phase), not a gross gait error.

## Step 2 — RE ground truth

Rebuilt the oot3d-decomp Ghidra project (gitignored `build/ghidra/`, `build/code.bin` — regenerated
via `tools/extract_code.py` + `analyzeHeadless ... -import build/code.bin -processor ARM:LE:32:Cortex
-loader BinaryLoader -loader-baseAddr 0x100000`, since this machine's clone had neither). Read the
literal-pool constants in the title cs's own per-frame Epona animation dispatch
(`FUN_0016ca48`/`FUN_003cf3c4`, already-decompiled from the 2026-07-14 dispatcher session) via
`ReadWord.py`:

- Title cs's own gallop-CSAB rate multiplier: **0.45** (`speedXZ * 0.45`, both init and per-frame).
- N64/gameplay `EnHorse_MountedGallop`/`EnHorse_CsMoveInit`/`EnHorse_CsMoveToPoint` (vendored
  `z_en_horse.c`, currently driving SoH3D's port): **0.3** (`speedXZ * 0.3f`).

Computed actual cycle rate using each engine's OWN anim length (N64 `gEponaGallopingAnim` = 24
frames, live-confirmed via `animdbg`; 3DS `hl_anim_fastrun2_30` = 36 frames, `csab_catalog.md`):
`(8.0*0.3)/24 = 0.1 cycles/tick` vs `(8.0*0.45)/36 = 0.1 cycles/tick` — **identical**. Grezzo's CSAB
is 1.5x the N64 anim's frame count and the 0.45 multiplier is exactly the compensating 1.5x scale.
SoH3D's phase-lock CSAB driver (`Zelda3D_UpdateAnimAuto`) samples `csab_frame =
(n64CurFrame/n64AnimLength) * csab_duration` — a fraction-of-cycle mapping, multiplier-agnostic by
construction — so the port's resulting on-screen tempo already matches the 3DS native tempo exactly
even while borrowing the N64-side 0.3 constant. **Tempo/rate mismatch, the leading hypothesis
going in, is ruled out by direct computation from both binaries — not assumed away.**

Full derivation: `oot3d-decomp/docs/en_horse_title_gallop_rate.md`.

## Step 3 — what WAS fixed

Independent of tempo, the port's cue-0x24/0x40 (Move/WarpMove) branch drove Epona's SkelAnime
through the **gameplay** `EnHorse_MountedGallop` action func instead of the native cs dispatcher's
own animation code (`EnHorse_CsMoveToPoint`/`EnHorse_CsWarpMoveToPoint`/`EnHorse_CsMoveInit` —
vendored in `z_en_horse.c`, previously unused, structurally 1:1 with the decompiled 3DS
`FUN_003cf3c4`/`FUN_00230d84`/`FUN_0016ca48` per `title_rider_cs_dispatch.md`). The gameplay func
additionally reads live stick input every frame (`EnHorse_UpdateSpeed`/`EnHorse_StickDirection`,
gated on `EnHorse_PlayerCanMove`) — machinery the real title cs's own code path never exercises
(a scripted move has no stick coupling). With zero stick input, `EnHorse_UpdateSpeed` decremented
`speedXZ` by 0.06/frame before the post-update hook reset it to 8.0, so `playSpeed` was computed
from a slightly stale, non-constant speed nearly every frame (~0.75% deficit — not the dominant
symptom, but a real unfaithful-approximation gap this project's ground-truth rule exists to close).

Fix, following the SAME pattern already used for the rearing cue (idx3/5, 2026-07-14 session):

- `Shipwright/soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c`: added
  `EnHorse_CsMoveAnimOnly`/`EnHorse_CsMoveInitAnimOnly` — the animation-only tail/init of
  `EnHorse_CsMoveToPoint`/`EnHorse_CsWarpMoveToPoint`/`EnHorse_CsMoveInit`, with the position math
  deliberately excluded (SoH3D's title rider owns position via its own oracle-verified integrator,
  `title_rider_cs_dispatch.md`'s cross-check; calling the native functions' position halves verbatim
  here would double-integrate the same cue endpoint every frame).
- `Shipwright/soh/src/zelda3d/behaviors/title/title_rider.cpp`: `applyToActor`'s cue-0x24/0x40
  branch now calls these instead of the old "force gait every frame via EnHorse_MountedGallopReset
  + EnHorse_MountedGallop" approximation, parks on `ENHORSE_ACT_CS_UPDATE` (same single-dispatcher
  structure as rearing, so the gameplay func never runs at all this frame), and feeds the actor's
  `speedXZ` from `TitleRider::mSpeed` (the already-integrated, cue-accurate value: 8.0 while moving,
  0.0 on the close-snap) instead of a hardcoded `8.0f`. Added a small idle fallback for the
  funcIdx==0 (no cue latched yet) case, replacing the old default-to-gallop fallback.

## Step 4 — verify

Rebuilt game (`cmake --build Shipwright/build-cmake --target soh -j4`) and harness
(`cmake --build Azahar/build-libretro --target soh3d_harness -j4`), serially, one at a time.
Re-ran `tools/title_rider_crop.py` at the same cs1560/1600 instants
(`scratch/title_ab/gallop_after_*`, grid `scratch/title_ab/rider_zoom_after_1560_1600.png`):
visually unchanged from the before grid, as predicted by the Step 2 math — since the tempo was
already correct, removing the gameplay-function coupling doesn't change the rendered pose at these
sample points, it only removes the (previously benign but unfaithful) stick/PlayerCanMove
dependency. No regression observed.

**Honest judgement:** the fix lands a real, decomp-grounded fidelity improvement (matches the
rearing cue's existing pattern; eliminates dead-weight gameplay coupling from a scripted cs), but
it likely does NOT explain the full "looks off" report — the leading hypothesis for a VISIBLE
gait divergence (tempo) was tested with real numbers and ruled out. The small pose lag visible at
cs1600 in both before/after crops is within the already-documented ~1-2 cs-frame sync envelope
(`title_rider_cs_dispatch.md`), not a new finding.

## Remaining work (not done this session, named concretely — not a vague TODO)

1. **Bone-level A/B tooling gap.** The embedded harness already exposes ground-truth Epona limb
   rotations live (`titleactors a` REPL command, `TITLE_POSE_TABLE_VA=0x005642D0`, 25 limbs,
   `tools/soh3d_harness/main.cpp` `HandleTitleActors`). SoH3D has no REPL-reachable equivalent —
   `Zelda3D_DumpModelBones` (`zelda3d.c`) exists but is only called internally from inside the
   AUTO draw path, gated, not exposed as a standalone command. Adding a `boneinfo <modelId>` REPL
   command and diffing it bone-for-bone against `titleactors a` at a matched cs frame would be a
   materially more decisive verification than pixel-space projection (which has its own error
   sources: camera calibration, rider-in-frame localization). This is the concrete next step if
   the user still reports the gait as off after this fix.
2. **Harness `soh_player_pos` gap at title.** `compare player`'s `soh_world=` read came back empty
   at every sampled title-cs instant this session (`ACTORCAT_PLAYER` list empty in the harness's
   embedded SoH linkage at these frames) — not investigated (out of scope for this task), but it
   silently degrades `title_rider_crop.py` to full-frame crops instead of rider-centered zoom.
   Worth a follow-up session using the SAME "why is gPlayState/actor-list empty at these instants"
   question the harness's own `TitleActive()` machinery already answers for the Az side.

## Files changed

- `Shipwright/soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c` — new
  `EnHorse_CsMoveAnimOnly`/`EnHorse_CsMoveInitAnimOnly`.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_rider.cpp` — `applyToActor`'s Move/WarpMove
  branch rewritten to call them; idle fallback added for funcIdx==0.
- `oot3d-decomp/docs/en_horse_title_gallop_rate.md` — new: the RE'd constants, the tempo-parity
  computation, and the honest "what this doesn't explain" note.
- `oot3d-decomp/build/` (gitignored) — regenerated `code.bin` + Ghidra project (not committed).
