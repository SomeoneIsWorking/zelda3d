# 2026-07-07 — title rider ported from cs op-0x0a actor cues

## Decode

The cs op-0x0a records are the classic N64 CsCmdActorAction 48-byte shape:
`{u16 action, u16 start, u16 end, u16 rot[3], s32 p0[3], s32 p1[3], f32 x3}`.
op 0x0a = the PLAYER/rider cue track (N64 cmd-10 analogue; interpreter
latches the frame-matching record at csCtx+0x40). The waypoint pointers
pinned in docs/title_writer_chains.md (0x0877df60/0x0877df90/0x0877e1b0)
are exactly these records inside the live spot99 ZSI — the old 3-entry
kZelda3dTitleRiderPath was a partial dynamic capture of this table.

Full itinerary decoded (15 cues, f0..3036, actions 0x40/0x41/0x24 =
gallop/idle/trot variants). Semantics VERIFIED vs Az (scratch/
verify_rider_cues.py): NOT a lerp — on a shot cut (fresh p0) the rider
teleports to p0, then INTEGRATES toward p1 with the already-RE'd
PathFollow dynamics (speed 8.0, yaw step 267) and terrain-following Y.

## Port

- zelda3d_cutscene.{h,cpp}: parse op-0x0a cues; Zelda3D_TitleCsRiderCue().
- zelda3d.c Zelda3D_RiderStepCue(): frame-indexed by the SAME cs cursor as
  the camera — this KILLS the /9 tick-rate STOPGAP and the static
  settled-pos hardcode. Teleport on discontinuity (>100u), PathFollow
  integrate, floor-raycast Y (BgCheck_AnyRaycastFloor1).
- Transform applied in Zelda3D_ActorPostUpdate (after Player's own update)
  so SoH-native cs/physics can't fight the ported trajectory.

## Verified (scratch/ab_rider3.py, lockstep by cs frame)

- Past the f=925 shot cut (both engines naturally aligned): steady
  |dXZ| = 14.2u (~2 frames of phase at 7.1u/frame), Y within ~5u.
  Pre-port residual was 6529u; segment avg now 266 (phase artifacts +
  one cue-boundary divergence at f~1118 to chase).
- XZ velocity matches Az exactly (~7.85u/frame, same heading) — the
  dynamics port is right; residuals are phase/boundary alignment.

## Open

1. ~2-frame phase offset between engines' cs cursors (harness sampling
   or cs-start anchor); chase with a boundary-anchored probe.
2. RESOLVED 2026-07-14 (commit 0a711e4c): f~1118 divergence was SoH's
   >100u any-cue-change teleport heuristic; the decompiled 3DS dispatcher
   (FUN_0026a30c = EnHorse_CutsceneUpdate) teleports ONLY on warp actions
   0x40/0x41, latch is start<f<=end last-match-wins. See
   2026-07-14-title-rider-cs-dispatch-port.md.
3. Rider ANIMATION per cue action id — PARTIAL 2026-07-14 (0a711e4c):
   0x24 corrected to GALLOP per CsMoveInit (FUN_0016ca48); 0x41 rearing
   animation still an idle approximation (journaled follow-up).
4. Env cue (op 0x0a is rider; env palette driver still unlocated —
   check op 0x03/0x3e consumers next).
