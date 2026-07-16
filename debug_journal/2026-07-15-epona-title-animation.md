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

---

## ADDENDUM — bone-level localization tooling (2026-07-15, same day, follow-up request)

A follow-up asked to LOCALIZE the divergence to specific bone(s) — the thing whole-frame pixel
crops cannot resolve (mane bone 14, tail bones 23/24). Built the tooling for a bone-for-bone,
same-units, cs-frame-locked diff of SoH's title Epona against the OoT3D oracle:

### Tooling added (all verified-to-compile; SoH path live-verified)

- `Shipwright/cmb3d/asset/csab.{h,cpp}`: `Csab::localTransforms(model, frame, out)` — per-bone
  ANIMATED LOCAL TRS via the exact `sampleLocalTRS` the renderer uses (rest-fallback + non-root
  static-translation-ignore rules included), so a divergence localizes to a bone's LOCAL rotation
  rather than a propagated parent transform.
- `Shipwright/soh/src/zelda3d/zelda3d_anim.cpp`: `Zelda3D_GetAnimBonesLocal(...)` core (fills a
  caller buffer) + `Zelda3D_DumpAnimBonesLocal(...)` stderr dumper; captures the live AUTO
  clip+frame per model (`sLastAuto`, recorded in `Zelda3D_UpdateAnimAuto`) so a dump uses the exact
  pose on screen.
- `Shipwright/soh/src/zelda3d/zelda3d.c`: REPL `boneinfo <modelId> [animBase] [frame]`.
- `tools/soh3d_harness/soh_state.cpp`: `SohState_AutoModelBonesLocal(...)` (thin wrapper over
  `Zelda3D_GetAnimBonesLocal`, valid at title — no gPlayState needed).
- `tools/soh3d_harness/main.cpp`: extended `compare titleactors` to ALSO dump SoH's OoT3D
  epona.cmb 25-bone LOCAL rotation in RADIANS, right under the oracle's own "25 poses ... rot(rad)"
  block — so ONE harness process prints both engines' title-Epona bones cs-frame-locked, in the
  same units, ready to diff.

### SoH-side result (live-verified, `scratch/soh_epona_gallop_bones.txt`)

`boneinfo 2010` on the live headless title demo (`ZELDA3D_WARP= tools/zelda3d_game.sh start`,
model 2010 = /actor/zelda_horse.zar this session — NOTE model ids are per-session load-order, NOT
stable) at a live GALLOP frame (`csab=hl_anim_fastrun2_30`):

- All 25 bones carry real ANIMATED local rotations — front legs (3-6, 10-13), hind legs (15-22),
  neck/head (7-9), and BOTH tail bones (23=`(1.36,1.79,1.45)`, 24=`(-0.03,0.06,0.61)`) are posed
  off bind, confirming the CSAB drives the tail live (matches the earlier static track-coverage
  finding, now confirmed on the LIVE draw path, not just a static dump).
- Mane bone 14 = `(0,0,0)` at this phase — consistent with its single small rZ-only track being
  near zero at frame 0; NOT a stuck/undriven bone (its parent bone 1 is fully posed).

### Cross-engine numeric diff — BLOCKED this session by concurrent harness use (honest)

The oracle half (`compare titleactors`' 3DS "25 poses rot(rad)" table) requires the embedded-Azahar
harness, which is a SINGLE-INSTANCE resource (lockfile `/run/user/1000/soh3d_harness.lock`). During
this work a CONCURRENT teammate session was actively holding it (`tools/link_sweep.py sweep --only
walk,run`, live pid holding the lock). Running a second harness would either fail on the lock or
OOM the 15 GB machine (a `-j4` build already OOM-killed a harness mid-session — do NOT run the
harness and a build, or two harnesses, concurrently). I did NOT interfere with the teammate's
harness. So the before/after numeric bone-diff TABLE is not in this entry — it is one
`compare titleactors` at a gallop cs away once the harness frees, with all tooling built + the SoH
path verified.

### Analytical localization (from architecture + the live SoH dump)

Both engines sample the SAME CSAB asset (`hl_anim_fastrun2_30`, from the same ROM): the 3DS via its
own SkelAnime keyframe evaluator (FUN_00347550, `mask&2 -> rot`, populating the oracle table at
`TITLE_POSE_TABLE_VA`), SoH via `Csab::sampleLocalTRS`. With no runtime procedural bone override on
the title-demo horse (unlike En_Ko head-look), the 3DS's per-bone local rotation IS the CSAB sample
— so SoH's `boneinfo` pose should agree with the oracle's `titleactors` table to within sampler-math
fidelity + phase. The live dump shows SoH samples the correct clip, phase-locked, with every bone
(incl. tail) driven — no stuck/bind-pose bone, no missing track. This is consistent with the
tempo-parity finding above and points AWAY from an animation-data divergence as the cause of the
"looks off" perception; the remaining candidates to check with the ready bone-diff (once the harness
frees) are (a) a per-bone sampler-math delta on specific bones, and (b) whether the 3DS applies any
title-specific procedural pose the CSAB doesn't carry. If the bone diff comes back all-match, the
"looks off" is NOT animation and the real candidates are model orientation / spawn pose / camera —
to be run as the immediate next step when the shared harness is available.

---

## Scoping note 2026-07-16 (autonomous tick): user's "yaw" report is a DISTINCT axis from the gait work above

User (this session) reports Link+Epona "completely broken ... turn sideways yaw rotation and their
animation etc completely looks wrong ... can only be bisected frame by frame". Frame-locked SBS at
step 600 (titlesync delta=0) shows the rider at a different screen position AND heading than the
oracle — i.e. the divergence the user calls out is HEADING/YAW (and coupled path position), NOT the
gait-cycle phase this journal's prior entries studied (tempo ruled out, pose near-identical). So the
prior gait conclusion stands and should NOT be re-litigated; the open item is the rider's yaw/heading
(and its path position) in the title cs.

BLOCKER (same tooling gap flagged in Step 1): the harness can't introspect the title rider. The title
demo's mounted Link is NOT an `ACTORCAT_PLAYER` actor (`compare player`/`soh_player_pos` read that
list, which is empty at title instants) — the rider is the En_Horse actor with Link rendered mounted.
Clean yaw bisection needs the harness to read the rider's En_Horse actor (pos + `world.rot.y` /
`shape.rot.y`) instead of the Player list, OR a purely-visual frame-by-frame heading comparison.

NEXT (tooling-first, before any yaw code change): extend the harness rider introspection to the
En_Horse actor so SoH rider yaw can be A/B'd against the oracle per cs frame. Only then diagnose the
yaw path (title_rider.cpp heading vs the 3DS cs dispatcher's own heading). Do NOT guess-fix the yaw
without that read — it would be a bandaid, and the path/gait are already carefully ported.

---

## Update 2026-07-16 (autonomous tick): built `soh_rider` introspection; SoH yaw is PATH-CONSISTENT

Closed the tooling gap: added `Zelda3D_Title_RiderState` (title_presentation.cpp) reading
`TitleRider::pos()/yaw()` (computed path) + the rendered EnHorse actor's `world.rot.y`/`shape.rot.y`,
exposed via harness `soh_rider`. Works (unlike `compare player`, which reads the empty
ACTORCAT_PLAYER list — the title Link isn't a Player actor).

First readings (steps 300/450/600, rider mounted):
```
pos=(-5600.2,82.8,5263.9) computedYaw=10913(59.9deg) horseWorldYaw=10913 horseShapeYaw=10913
pos=(-5080.9,80.5,5564.4) computedYaw=10913(59.9deg) horseWorldYaw=10913 horseShapeYaw=10913
pos=(-4561.6,71.0,5864.8) computedYaw=10913(59.9deg) horseWorldYaw=10913 horseShapeYaw=10913
```

FINDING: the SoH rider yaw is INTERNALLY CONSISTENT and matches its own movement direction — the
rider moved (dx=+1039, dz=+601) over the samples, and `atan2(dx,dz) = 60.0deg` ≈ the reported yaw
59.9deg; computed == world == shape. So a gross yaw-COMPUTATION bug is ruled out. The user's
"sideways" look must be one of:
  (a) PATH-POSITION divergence vs the oracle — the rider is at a different point on the (curving)
      cs path than the oracle at the same frame, so it faces a different absolute direction; OR
  (b) a MODEL-ORIENTATION offset — the EnHorse CMB's forward axis vs the actor yaw (a fixed rotation
      offset would make a correct yaw render visibly rotated).

NEXT: read the ORACLE's EnHorse yaw+pos at the same cs frames (harness already reads Az RAM for the
rider trajectory — see tools/title_rider_traj.py) and A/B against these `soh_rider` values. If yaw
matches but pos diverges → (a), a path bug. If pos matches but yaw diverges → the render offset (b).

---

## Update 2026-07-16 (oracle A/B): SoH rider MATCHES the oracle at step 600 — rider port is correct

Ran the matched A/B the previous note set up. At step 600 (titlesync LOCKED, delta=0 — the frame the
user's SBS sweep showed as "off"):

| quantity | SoH (`soh_rider`)        | Oracle (EnHorse @0x09906A80)      | delta        |
|----------|--------------------------|-----------------------------------|--------------|
| pos      | (-4561.6, 71.0, 5864.8)  | (-4561.7, 71.2, 5864.8) [+0x28]   | ~0.2 units   |
| yaw      | 10913 (59.9deg)          | 0x2AA5 = 10917 (59.9deg) [+0x36]  | 4 bam (0.02deg) |

(Oracle read via harness `r16 0x09906AB6` / `mem 0x09906AA8 12`; VAs from
tools/title_rider_traj.py.) Oracle static mirror @0x005AFFB0 = (-4568.6,70.9,5860.8), consistent.

**The rider's world position AND heading match the oracle within noise.** So the rider port (path,
position, yaw) is CORRECT in steady-state gallop — the "sideways / completely broken" appearance is
NOT a rider pos/yaw divergence. Candidates, in priority order:
  1. **Camera divergence** — if the title cs camera differs from the oracle at this frame, the whole
     scene (rider included) is framed differently, so a correctly-placed rider looks mis-positioned.
     (In the s0600 SBS the horse is barely visible on the SoH side vs clearly framed on the oracle —
     consistent with a camera/framing difference, not a rider move.) The camera was "ported+verified"
     per soh3d-title-scene-spot99, but verify at THIS frame.
  2. **Transient cut-frame glitches** — the user said "bisected frame by frame"; the title cs has
     warp/rearing cues (e.g. cs ~925) where the rider teleports across shot cuts. Yaw/pose could snap
     wrong for a frame or two there while steady segments (like 600) are fine.
  3. Model/pose render at specific frames (gait already ruled subtle above).

NEXT: read the SoH title camera eye/at vs the oracle camera at step 600 (and a few cut frames) to
confirm/deny (1). `soh_rider` + the oracle EnHorse read now make per-frame rider A/B cheap.

---

## Update 2026-07-16 (camera A/B): the divergence is CAMERA-framing, not the rider — but reads are gap-blocked

`compare camera` at step 600:
- oracle title-cam @0x005BE6D4: eye=(3919.7,-117.9,7454.0) dir=(0.981,0,0.194) up=(0.063,0.947,-0.316)
- SoH (SohState_Camera): camId=1 eye=(-4071.5,57.8,5217.3) at=(-4939.5,252.8,5675.3) fov=48.80

Both reads are UNRELIABLE at the title and must not be taken as literal ground truth:
- `0x005BE6D4` is almost certainly the "spectator slot" flagged in memory `soh3d-title-cam-handedness`
  — its eye (3919,7454) looking +X (east) cannot frame the rider, which is at west X=-4561 and IS
  clearly visible in the s0600 oracle frame. So this VA is NOT the render/demo camera at this shot.
- SoH `SohState_Camera` reads `gPlayState->cameraPtrs[...]`, but the title's live PlayState isn't at
  the harness's standard ptr (`soh3d-oot3d-title-not-play`: live play @0x00539F98, 0x0050AF34 stays
  0) — same gap that makes `compare scene/actors/player` empty at the title. The ~constant
  (-4071,57.8,5217) is likely a stale/default camera, not the ported title-cs spline output.

ROBUST CONCLUSION (independent of those unreliable values): the rider's WORLD pos+yaw match the
oracle (previous update), yet the scene is framed differently (the moon sits at a different screen
height in the s0600 SBS — a pure camera-DIRECTION tell, since the moon is at infinity). A correctly-
placed rider under a differently-aimed camera looks mis-positioned/"sideways". So the user's
"Link+Epona completely broken" is a **title-CAMERA divergence**, NOT a rider port bug. The fix
belongs in the title-camera port, not title_rider.cpp.

BLOCKER for the fix: cleanly A/B'ing the camera needs (1) the REAL oracle demo-camera VA (not the
0x005BE6D4 spectator slot — an RE task with prior Ghidra-xref dead ends per soh3d-title-cam-
handedness), and (2) reading the SoH ported title-cs camera spline output directly (like `soh_rider`
does for the rider via TitlePresentation, NOT via gPlayState->cameraPtrs). Both are title-arc RE,
deep and dead-end-prone. Deprioritized relative to gameplay correctness; the rider itself is fine.
