---
id: 7
title: Link casts no shadow — ActorShadow_DrawFeet has no feetPos because the replaced CMB draw skips the N64 limb walk
status: open
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
