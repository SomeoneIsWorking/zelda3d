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

---

## Update 2026-07-16 (root cause found): camera-spline GAP at cs 300 → SoH falls back to a static default

Built `soh_camera` (Zelda3D_Title_CameraState → the ported spline `Zelda3D_TitleCsCamera` at the
current cs frame; reliable at the title, bypasses gPlayState). At step 600 (cs frame 300):

- `soh_camera` → **live=0, eye=(0,0,0)** — i.e. NO camera segment covers cs 300 (the impl matches
  `s.start < frame < s.end`; 8 segments loaded, end_frame=2400).
- When the spline returns 0, title_presentation.cpp's camera block holds via `f-1` (also in the gap
  → 0) and then falls back to the STATIC default `kZelda3dTitleEye=(-4071.5,57.8,5217.3)` /
  `kZelda3dTitleAt=(-4939.5,252.8,5675.3)` (zelda3d.c) — exactly the (-4071,5217) SohState_Camera
  reported. That camera looks WEST (toward the rider) with ~+11deg pitch.
- Oracle `0x005BE6D4` = (3919,7454) looking +X (east, AWAY from the rider) — confirmed the SPECTATOR
  slot (soh3d-title-cam-handedness), not the render camera; ignore it.

VISUAL CONFIRMATION (s0600 SBS, `scratch/harness/s0600_azsoh.png`): same scene, but the MOON sits
far lower in the SoH frame than the oracle — a pure camera-PITCH divergence (moon is at infinity).
Consistent with SoH using the fixed static default (too much up-pitch) during the cs-300 gap while
the oracle's camera is elsewhere in pitch.

ROOT CAUSE (SoH side, concrete): the ported title camera has 8 segments with GAPS between them (and/
or before the first). During a gap, SoH freezes at the fixed `kZelda3dTitleEye` static default
instead of tracking whatever the oracle does there → the framing/moon-height divergence the user
sees as "everything looks wrong". The rider is correctly placed underneath (previous updates);
it's the CAMERA that's wrong during gaps.

NEXT (the fix — needs care, not a bandaid): determine the correct gap behavior. Two candidates:
  (a) the segment PARSER is dropping coverage that should exist (cs 300 should be inside a segment) —
      check the 8 segments' [start,end] ranges vs the cs's authored camera commands; OR
  (b) gaps are real and the camera should HOLD the nearest PRECEDING segment's end value (not a
      fixed global default, and not `f-1` which fails for multi-frame gaps).
Confirm which against the oracle's actual camera at cs 300 before changing title_presentation's gap
fallback. `soh_camera` now makes per-frame SoH camera A/B cheap; the oracle side still needs its real
render-camera VA (not 0x005BE6D4).

---

## Update 2026-07-16 (fix attempted + REVERTED — the spline isn't the render camera): where it really stands

Dumped the 8 camera-segment ranges (added per-seg logging to zelda3d_cutscene.cpp):
`(0,299)(300,929)(930,1379)(1380,1619)(1620,1656)(1657,1776)(1777,2031)(2032,2455)`. The lookup
`s.start < frame < s.end` (strict both ends) drops BOTH seam frames (299 AND 300, 929 AND 930, ...)
to the caller's static default — a real per-seam glitch.

Tried the naive fix `<=` both ends and VERIFIED it — it REGRESSED. At cs300 (`<=`) the lookup lands
in seg1, whose opening pose renders LOOKING DOWN AT GRASS (soh_camera eye=(3921,-118,7460) matching
0x005BE6D4 exactly), whereas the oracle at cs300 renders the rider+moon (a level shot). So:
  - the OP97 spline `Zelda3D_TitleCsCamera` reads is NOT the oracle's render camera at seam-opening
    frames — it matches 0x005BE6D4, i.e. the SPECTATOR/basis slot (soh3d-title-cam-handedness was
    right). The static default (rider+moon, moon a bit low) was actually CLOSER to the oracle than
    seg1 (grass).
  - so `<=` shipped a verified-worse frame. REVERTED to strict `<` (documented in-code). No net
    code-behavior change this tick; the tools + diagnosis are the deliverable.

REAL STATE of the title-camera issue (honest): the rider is correct (matches oracle). The camera is
wrong, but it's NOT a one-line seam fix — the ported spline's seam-opening poses don't match the
oracle's render camera (a wrong-camera-data or seam-frame-alignment problem, e.g. the oracle's shot
cut is a few frames off from SoH's seg starts). Fixing it needs the oracle's REAL render-camera VA
(0x005BE6D4 is the spectator slot; the render camera VA is still unknown — a title-cam RE task with
prior Ghidra-xref dead ends). Deprioritized vs gameplay. Tools now in place: `soh_camera` (SoH
spline), per-seg range logging, `soh_rider`, and the oracle EnHorse read.

---

## Update 2026-07-16 (LOCATED): SoH title camera eye+up are CORRECT, the AT/direction is WRONG

"Nothing is blocked." Fixed the title-introspection gap at its source: CurrentPlayState() read
gPlayState @0x0050AF34 (stays 0 at the title); added the fallback to the live title play ptr
@0x00539F98 ([it] = 0x0871e854). Now oracle scene/actors/camera resolve at the title. Also repointed
`az_camera` at the RE'd 3DS title-camera basis @0x005BE6D4 (eye + forward + up, LIVE/moving; NOT
play->view.eye+0x1B8 which is the N64 offset — that read garbage on the 3DS PlayState).

Clean A/B at cs~320 (step 640, seg1 interior), `az_camera` vs `soh_camera`:
```
             eye                       up                      direction
oracle   (3882.4,-107.3,7336.2)  (0.076,0.946,-0.314)  fwd=(0.972, 0.000, 0.235)  -> +X, level (at rider)
SoH      (3884.4,-108.0,7342.1)  (0.075,0.947,-0.314)  dir=(0.221,-0.323,-0.920)  -> -Z, DOWN (at grass)
rider pos=(4227.5,-147.9,7364.2)  (east of the camera)
```

EYE matches within ~6 units. UP matches. **DIRECTION is wrong** — the oracle looks EAST at the rider;
SoH looks NORTH-and-DOWN at the ground. So the ported title camera computes the right eye + up but
the wrong LOOK-AT — that IS the "Link/Epona/everything looks broken, frame by frame" report (it's the
camera aim, and it's wrong across the whole segment, not just seams — reverting the seam experiment
was right).

ROOT CAUSE now precise: `Zelda3D_TitleCsCamera`'s at-point (seg->atDef + the type-2 "at" track), or
the eye->at handedness, is wrong while the eye track (type-1) is right. Both are parsed the same way,
so the divergence is specific to the at channel. NEXT: diff the at-track parse/eval vs the eye track
(and vs the oracle forward @0x005BE6D4+12) — the oracle gives forward directly, so `at = eye +
forward*dist` is the target; find why the ported at-track produces (0.221,-0.323,-0.920) instead.
Tools ready: az_camera (oracle basis), soh_camera (SoH spline), CurrentPlayState title fallback.

---

## Update 2026-07-16 (raw values): the ported "at" -> forward mapping is wrong; eye is right

Instrumented Zelda3D_TitleCsCamera (ZELDA3D_DBG_TITLECAM=1). seg1 at cs320:
```
eyeDef=(3921.0,-118.2,7460.0)  atDef=(3940.5,-152.2,7362.3)  tracks=[1(eye) 2(at)]
eyeEval=(3884.4,-108.0,7342.1) atEval=(3907.7,-142.0,7245.0)
```
- eyeEval (3884,-108,7342) MATCHES the oracle 0x005BE6D4 eye (3882,-107,7336). So the eye track is
  correct and 0x005BE6D4 IS this camera (not a spectator slot).
- forward the ported code derives = normalize(atEval - eyeEval) = (0.221,-0.323,-0.920) — down/back.
- oracle actual view dir @0x005BE6D4+12 = (0.972,0.000,0.235) — +X, toward the rider (X=4227).
  RENDER agrees: oracle frames the rider, SoH looks at grass.

So `at` as a naive look-at point (`forward = normalize(at - eye)`) is WRONG. The at-eye offset from
the cs data is (~+19,-34,-98) (down/back) regardless of track animation, but the real view is +X.
This is not a simple axis swap (eye is un-swapped and correct), nor the seam issue (whole-segment).
The OoT3D title "at" field must map to the view direction some other way — likely the decompiled cs
camera interpreter FUN_002c5ba0 case 0x97 (oot3d-decomp; the "segment select by frame range" note in
zelda3d_cutscene.cpp cites it) defines `at`'s semantics (a rotated/relative target, or eye+at are a
different basis). That RE is the definitive next step — do NOT patch the forward blindly (the seam
`<=` patch already regressed once).

Tools in place for the fix loop: ZELDA3D_DBG_TITLECAM (raw defs/eval/tracks), az_camera (oracle
basis @0x005BE6D4), soh_camera (SoH spline), CurrentPlayState title fallback (oracle title
introspection), soh_rider. Nothing here is blocked — it's a bounded decomp-read of the at->forward
map, then a verified port.

---

## Update 2026-07-16 (RESOLVED): title-cam matches the oracle; the bug was the SEAM fallback + a mislabeled vector

The whole "camera divergence" was a MEASUREMENT ERROR. oot3d-decomp
`title_basis_writer_jit_solved.md` gives the corrected 0x005BE6D4 layout:
`+0x00 eye, +0x0C RIGHT, +0x18 up, +0x24 at-eye (the real view dir)`. Earlier passes (and my
az_camera) read +0x0C (the RIGHT vector) as "forward" — so comparing SoH's real forward to the
oracle's RIGHT of course diverged 90deg. Fixed az_camera to read +0x24.

With the correct vector, the SoH title camera MATCHES the oracle:
```
             eye                     forward                   up
cs320  SoH (3884,-108,7342)    (0.221,-0.323,-0.920)   (0.075,0.947,-0.314)
       AZ  (3882,-107,7336)    (0.223,-0.324,-0.920)   (0.076,0.946,-0.314)   MATCH
cs300  SoH (3921,-118,7460)    (0.182,-0.323,-0.929)   (0.062,0.946,-0.317)
       AZ  (3920,-118,7454)    (0.184,-0.322,-0.929)   (0.063,0.947,-0.316)   MATCH
```

So the ONLY real defect was the SEAM handling: strict `s.start<frame<s.end` dropped both boundary
frames of every seam (299/300, 929/930, ...) to the fixed static-default camera — 7 seams x 2 frames
of a jarring camera jump = the user's "everything looks broken, bisectable frame by frame". The
oracle at cs300 is already at seg1's opening (verified above), so the `<=`-inclusive lookup is
correct and makes SoH match the oracle at the seams too. (My first `<=` attempt was reverted because
I judged it against the RIGHT vector + eyeballed a 1-frame-desynced render — a false negative.)

FIX (shipped this session): inclusive segment bounds in Zelda3D_TitleCsCamera. Verified: SoH camera
eye/forward/up match the oracle within noise at both interior (cs320) and seam (cs300) frames. Rider
was already correct. The title-demo camera now tracks the oracle across the whole cs.

Tools left in place: az_camera (correct 0x005BE6D4 layout), soh_camera, ZELDA3D_DBG_TITLECAM,
CurrentPlayState title fallback, soh_rider.

---

## Update 2026-07-16 (rider frame-by-frame): LINK's mounted POSE leans too far forward

Per user request, built a frame-by-frame rider A/B where the rider is clearly visible: seg0 following
shot, cs~240-276, 10 frames every 4 (scratch/harness/rf_00..09, rider_film2.png — LEFT oracle, RIGHT
SoH, horse-centered crops). soh_rider confirmed pos moving straight, yaw constant 59.9deg (matches
movement dir) — so this window is a clean straight gallop, no warp/yaw issue.

FINDING (consistent across ALL 10 frames): the HORSE gait is close, but **Link's mounted posture is
wrong** — in the oracle Link sits roughly UPRIGHT (slight forward lean); in SoH Link is HUNCHED far
forward, torso nearly horizontal over Epona's neck. This is Link's rider pose, not the horse, and not
yaw (yaw is correct here). This is almost certainly the "animation ... completely looks wrong" part
of the user report; the "sideways yaw" is a separate thing at the warp/cut frames.

LIKELY CAUSE: mounted Link is posed by the Player's native N64 mounted-ride action func (title_rider.
cpp reuses z_player.c ~13825) driving the N64 horseback animation retargeted onto the OoT3D rig
(soh3d-n64anim-retarget). The N64 gallop-ride pose has a strong forward lean; OoT3D's title Link
pose is gentler/upright. So the port is showing the N64 lean, not the 3DS one — OR a speed-based
upper-body lean (Player_UpdateUpperBody, proportional to horse speedXZ=8.0) is over-applied on top.

NEXT: identify which animation/lean drives it — dump Link's SkelAnime joint table (torso/root
rotations) vs the oracle's mounted Link pose (compare skeleton / the title pose table), and check
whether the mounted anim should be the OoT3D CSAB (hl_...) instead of the N64 uma anim. Fix = use the
3DS mounted pose, or cap the speed-lean to the 3DS value. (Rider position + yaw + horse gait are
already correct; this is specifically Link's torso lean.)

---

## Update 2026-07-16 (FIXED + VERIFIED): mounted-Link pose — dual-dispatcher fight resolved

Full Ghidra-backed chain (title-screen port arc):
- The 3DS riding CSABs live in **`zelda_link_opening.zar`** (21 CSABs, `uma_*` family mirroring
  N64's `gPlayerAnim_link_uma_*`; gallop-ride = `uma_anim_fastrun`, 24f both sides). The animID-8
  mapping already existed in zelda3d_player_animmap.inc — selection, not mapping, was broken.
- `log link` trace: mounted Link played `uma_anim_stand` with `horseAnimIdx=3 (REARING)` during the
  gallop — Player's D_80854944 selector (z_player.c ~14048) keyed off a wrong horse index.
- `log rider` trace at the cue transition: `cutsceneAction` flip-flopped 5->1->5 and `animationIdx`
  3->6->3 every frame — TWO dispatchers fighting: the ported 3DS title dispatcher (title_rider.cpp)
  AND the vendored N64 `EnHorse_CutsceneUpdate`, which dispatches from `play->csCtx.linkAction` —
  NON-NULL at the title (the N64-authored cs still ticks underneath; the old "always NULL at title"
  assumption in title_rider's comment is FALSIFIED). Each re-inited against the other; Link's
  selector sampled whichever index was live at its update — the racy stand/hunched pose.

FIX: suppress `EnHorse_CutsceneUpdate`'s csCtx dispatch while `Zelda3D_Title_IsActive()` — the same
suppression family as EnMag_Update and Cutscene_Command_Terminator; the ported 3DS dispatcher
(decomp FUN_0026a30c) is the sole title-horse driver, as on the 3DS.

VERIFIED: (1) trace — clean REARING(cs0-15) -> one MOVE transition -> steady idx=6, Link plays
`uma_anim_fastrun` with av2=6; (2) visual — rider filmstrip cs240-276 re-captured
(scratch/harness/rider_film_fixed.png): Link sits upright matching the oracle in all 10 frames.
