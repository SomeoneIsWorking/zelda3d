# Title-demo rider gets Epona — horse-attribution port (2026-07-10)

Applies `<oot3d-decomp>/docs/title_rider_port_spec.md`'s 4-step plan, closing the gap named
by `debug_journal/2026-07-10-oracle-horse-attribution.md`: the oracle's title-demo rider is
**Link mounted on a real Epona actor** (two independent SkelAnime entities), while SoH3D was
teleporting Link's bare `Player` actor along the cue path with no horse under him at all.

## What changed

All horse-side logic lives in `Zelda3D::TitleRider`
(`Shipwright/soh/src/zelda3d/behaviors/title/title_rider.{h,cpp}`) — no new gates in
`zelda3d.c`; that file's `Zelda3D_ActorPostUpdate` now just forwards both `ACTOR_PLAYER` and
`ACTOR_EN_HORSE` through one bridge (`Zelda3D_Title_RiderApply`) instead of writing
`ACTOR_PLAYER`'s transform directly.

**Step 1 — retarget the cue integrator's target actor** (spec step 1): `TitleRider::step()`
is unchanged (still a pure position/yaw integrator); what changed is *who* the resolved
`mPos`/`mYaw` gets applied to. `TitleRider::applyToActor()` applies it to the spawned
`ACTOR_EN_HORSE` instance's `world.pos`/`shape.rot.y`/`world.rot.y`, not to `ACTOR_PLAYER`.

**Step 2 — spawn + mount** (spec step 2): on the first `ACTOR_PLAYER` post-update call after
title becomes active (`mHorseActor == nullptr`), `applyToActor()` spawns `ACTOR_EN_HORSE` via
`Actor_Spawn(&play->actorCtx, play, ACTOR_EN_HORSE, mPos, ..., params=1)` — `params=1` is the
same "plain ridable Epona" params `z_horse.c`'s own spawn call sites use; `EnHorse_Init`'s
early `Actor_Kill` branches only gate on `SCENE_LON_LON_RANCH`/`SCENE_STABLE`/
`SCENE_GERUDOS_FORTRESS`, none of which is the title's `SCENE_HYRULE_FIELD`, so the spawn
always survives init. Link is mounted via the literal field-set + call z_player.c's own
A-press mount path uses (`~7169-7193`): `rideActor`, `PLAYER_STATE1_ON_HORSE`,
`actor.parent`, `Actor_MountHorse(play, player, horse)` — but WITHOUT the button-press/
put-away/climb-on transient (`Player_ActionHandler_3`'s gate), since the oracle demo begins
already mid-ride. Instead `player->actionFunc` is set straight to the native ongoing-ride
func `Player_Action_8084CC98` with `av2.actionVar2` seeded to `99` (skips the multi-frame
get-on-horse sub-animation and lands directly in that function's "already riding" branch,
`z_player.c` ~13854+).

**Step 3 — drop the direct `ACTOR_PLAYER` world.pos/rot writes** (spec step 3): done — Link's
transform is no longer touched by title code at all. `Player_Action_8084CC98` derives it
every frame from `rideActor->actor.world.pos + rideActor->riderPos` (the same native code any
gameplay Epona ride uses), which is exactly the spec's intent ("Link's transform and pose are
a natural consequence of mounting her").

**Step 4 — force-select the cued gait/CSAB** (spec step 4, extended not hand-rolled): rather
than reaching for anything title-specific, `applyToActor()` re-asserts `EnHorse`'s own native
`action`/`animationIdx`/`speedXZ` fields on the `ACTOR_EN_HORSE` branch every frame (not just
on a cue-action transition) and calls the SAME `EnHorse_MountedGallopReset`/
`EnHorse_MountedTrotReset`/`EnHorse_StartMountedIdleResetAnim` helpers `z_en_horse.c`'s own
gait-switch call sites use when the desired gait changes. This is necessary and NOT optional
per-frame busywork: `EnHorse`'s stock action funcs (`EnHorse_MountedGallop` etc.) read REAL
controller stick input to decide whether to hold/demote the gait — the title attract-demo has
none, so left alone the horse decays to a standstill within a few frames of the ride starting.
Re-forcing after the horse's own `Update()` ran this frame means next frame's
`sActionFuncs[this->action]` dispatch (`EnHorse_Update`) always lands back on the cued gait,
and THAT function's own `SkelAnime_Update` keeps the animation advancing — the existing CSAB
auto-resolver (`zelda3d_animmap.inc`'s `object_horse` table, untouched) then picks the right
clip off the horse's live N64 animation resource with zero title-specific anim code. The cue
action itself is now plumbed through: `Zelda3D_TitleCsRiderCue()` gained an `outAction`
param (`zelda3d_cutscene.h/.cpp`), `TitleRider::step()` captures it (`mCueAction`), and
`applyToActor()` maps `0x40→gallop / 0x41→idle / 0x24→trot` per the spec's tentative table
(0x24's exact meaning is still the one open question the spec itself flags — non-blocking).

One extra hazard found and handled during implementation, not named in the spec:
`EnHorse_MountDismount`'s one-shot mount-edge transition (`z_en_horse.c`) calls
`EnHorse_Freeze()` — which stomps `this->action = ENHORSE_ACT_FROZEN` — the first tick after
`horse->child` becomes non-null (`Actor_MountHorse` sets that). `applyToActor()` pins
`horse->playerControlled = 1` and restores the collider `OC1_ON` flags every frame so that
one-shot transition can't re-fire and so its single-frame side effects self-heal before the
next draw.

**Teardown**: `TitlePresentation::exit()` calls the new `TitleRider::releaseMount(play)` —
un-mounts Link (clears `rideActor`/`PLAYER_STATE1_ON_HORSE`/`actor.parent`, repoints
`actionFunc` to the generic `Player_Action_Idle` so a stray re-entry-without-reload can't
call `Player_Action_8084CC98` with `rideActor == NULL`) and `Actor_Kill`s the spawned horse so
nothing lingers into gameplay/attract paths.

## Verification (TEXPACK=off, embedded harness + real headless boot)

**1/2 — SxS silhouette match** (`tools/title_ab.py ab <az> --soh <soh> --name <name>`, both
builds rebuilt first — `Shipwright/build-cmake` AND `Azahar/build-libretro`):

- az=200/soh=608 → `scratch/title_ab/horse_after_200_sxs.png` (gitignored). Horse+rider now
  visible on the ridge in SoH's pane at the same position as the oracle's silhouette — before
  this port SoH showed empty field there (bare Link teleported with no horse under him, per
  `title_rider_port_spec.md`'s own screenshot evidence).
- az=1000/soh=1408 → `scratch/title_ab/horse_after_1000_sxs.png`. Horse+rider visible
  top-right past the logo overlay, matching the oracle.
- content-match scores 0.93 / 0.78 (lighting/color is a separate, already-documented
  divergence per `title_ab.py`'s own doc comment — this check is for
  position/pose/silhouette, which both frames show correctly attributed to the horse now).

**3 — rider position unregressed** (`scratch/rider_cadence_measure.py`, `compare player` +
direct Az VA read, same matched frame pairs as
`debug_journal/2026-07-10-rider-cadence-fix.md`):

| pair | az csFrame | oracle rider pos | SoH pos (now Link's mounted pos) | XZ offset | pre-horse-port baseline |
|---|---|---|---|---|---|
| az=200/soh=608   | 188 | (-5351.0, 74.2, 5408.0) | (-5327.18, 112.98, 5421.85) | **27.6 u** | 30.2 u |
| az=1000/soh=1408 | 588 | (3352.2, 324.0, 5442.2) | (3339.94, 372.29, 5421.70) | **23.9 u** | 46.1 u |

Both within the task's ≤~46u guideline; both slightly *better* than the pre-port baseline
(SoH's `pos` field now reads Link's mounted position — `rideActor->world.pos +
rideActor->riderPos` — rather than the bare cue integrator's output, and that mount-seat
offset happened to land closer to the oracle's own recorded rider position at both sampled
frames).

**4 — no stray horse outside title** (`ZELDA3D_INSTANCE=9`, real headless boot to entrance
`0x0102`, a normal gameplay entrance — NOT the title path): booted clean, no crash;
`actorscan 0x14` (ACTOR_EN_HORSE) → **0 found**. The scoping is structural, not new logic to
verify separately: the horse spawn is nested inside the pre-existing `Zelda3D_Title_IsActive()`
gate at the `Zelda3D_ActorPostUpdate` call site (unchanged), which itself requires
`TitlePresentation::shouldBeActive()` — no user warp target + `SCENE_HYRULE_FIELD` — so any
real warp/entrance short-circuits before `TitleRider::applyToActor()` ever runs.

**lus_tests**: 438 passed / 6 skipped (ROM-dependent, expected without `.env` sourced for
that binary) / 0 failed — unaffected by this change (libultraship untouched).

## Files

- `Shipwright/soh/src/zelda3d/behaviors/title/title_rider.h` / `.cpp` — owns spawn/mount/
  release + per-frame apply (new `applyToActor`/`releaseMount`, `mHorseActor`/`mPlayerActor`/
  `mCueAction` members).
- `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.h` / `.cpp` —
  `Zelda3D_Title_RiderTransform` replaced by `Zelda3D_Title_RiderApply(play, actor)`;
  `exit()` calls `mRider.releaseMount(play)`; added `mutableRider()` accessor.
- `Shipwright/soh/src/zelda3d/zelda3d.c` (`Zelda3D_ActorPostUpdate` ~line 441) — routes both
  `ACTOR_PLAYER` and `ACTOR_EN_HORSE` through the one bridge call.
- `Shipwright/soh/src/zelda3d/zelda3d_cutscene.h` / `.cpp` — `Zelda3D_TitleCsRiderCue` gained
  `outAction`.
- Reused as-is, unmodified: `Shipwright/soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c/.h`
  (`Actor_MountHorse` call, `EnHorse` struct/gait-reset helpers),
  `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c`
  (`Player_Action_8084CC98`, `Player_Action_Idle`), `zelda3d_animmap.inc`'s `object_horse`
  CSAB table, `zelda3d_object_zars.inc`'s `OBJECT_HORSE` zar mapping.
- Ground truth: `<oot3d-decomp>/docs/title_rider_port_spec.md`,
  `debug_journal/2026-07-10-oracle-horse-attribution.md`,
  `debug_journal/2026-07-10-rider-cadence-fix.md` (rider-position baseline table reused here).

## Conditions

Both builds rebuilt (`Shipwright/build-cmake --target soh -j4`, `Azahar/build-libretro`
`ninja soh3d_harness -j4`) before any verification. One harness run
(az=1000/soh=1408 SxS capture) hit a transient "harness closed stdout unexpectedly" — no
crash signature in dmesg, no leftover process, immediate retry succeeded — consistent with
the known concurrent-agent harness-instability artifact called out in this session's own
task brief, not a regression introduced by this port.
