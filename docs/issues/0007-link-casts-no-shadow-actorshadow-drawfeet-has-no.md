---
id: 7
title: Link casts no shadow — ActorShadow_DrawFeet has no feetPos because the replaced CMB draw skips the N64 limb walk
status: resolved
symptom: Link has no shadow at all and reads as floating. Other actors using the plain circle shadow are unaffected; it is specifically the FEET shadow.
tags: shadow,player,limb-walk,skelanime,206,zelda3d
created: 2026-07-28
updated: 2026-07-28
---

Player uses ActorShadow_DrawFeet (z_player.c:11183 ActorShape_Init(..., ActorShadow_DrawFeet, ...)), not the circle shadow. DrawFeet places one shadow per foot from actor->shape.feetPos.

feetPos is written ONLY by Actor_SetFeetPos, called from Link's post-limb-draw callback (z_player_lib.c:2001, Player_PostLimbDrawGameplay) — which runs during the N64 SKELETON LIMB WALK. Zelda3D replaces Link's draw with the OoT3D CMB model, so that walk never happens and feetPos is never written; DrawFeet then has no foot positions to place shadows at.

THIS IS A KNOWN PATTERN IN THIS CODEBASE, not a new class of bug: memory soh3d-skinned-actor-collision records the same failure for COLLIDERS ('replaced draw skipped limb walk -> colliders at origin'), fixed by re-running the limb walk for its side effects and then rewinding the display list so nothing actually draws. The same remedy should apply here — feetPos is another side effect of that walk.

RULED OUT (do not re-chase): shadow placement / terrain-warp Y offset. Measured no-op, and Zelda3D_TerrainWarpEnabled() is 0 whenever OoT3D collision is active anyway. See issue #6.

VERIFY WITH: a ground strip BELOW the boots (e.g. px box 320,395-450,435) — a box that includes Link's body is dominated by his dark boots and cannot see the shadow. Compare mean/min against a known-good frame.

## RESOLVED 2026-07-28

Fixed in z_player.c Player_Draw: when Zelda3D_TryDrawPlayer replaces the body, re-run Player_DrawImpl
(with Player_PostLimbDrawGameplay) under gZelda3dColliderPass = 1 purely for the side effect, then
rewind play->state.gfxCtx->polyOpa.p / polyXlu.p so none of the N64 geometry renders. This is exactly
Zelda3D_UpdateSkelColliders' remedy (z_skelanime.c, #107/#108) applied to the player's dedicated hook;
gZelda3dColliderPass now has one declaration, in zelda3d/zelda3d.h, instead of a local extern.

Ordering is safe: Actor_Draw calls actor->shape.shadowDraw immediately after actor->draw
(z_actor.c:2833), so feetPos written during the re-run walk is fresh for the same frame's shadow.

EVIDENCE (like-for-like A/B, two builds of the same tree differing only in this hunk; identical Link
pos (-68,-79,941) and identical `acam 120 z` eye, settled frame in both):
  ground patch below the boots, px 370,288-440,302
    before  mean RGB = (85.0, 102.9, 29.9)
    after   mean RGB = (74.2,  87.5, 24.9)     green -15%
  and the contact shadow is plainly visible in scratch/screenshots/shadow206_after_zoom.png where
  shadow206_before_zoom.png has bare grass.
No double-draw / N64 leak: the frame shows a single Link (the rewind discards the walk's geometry).

