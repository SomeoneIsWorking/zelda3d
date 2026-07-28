---
id: 6
title: Actors have no visible shadow — the blob shadow draws at the N64 collision floor, not the OoT3D render ground
status: resolved
symptom: Link (and every actor) casts no shadow; characters read as floating above the ground. Looks like the shadow system is missing entirely.
tags: shadow,actor,terrain-warp,render,206
created: 2026-07-28
updated: 2026-07-28
---

NOT a missing system, and NOT the deleted sun-shadow/SSAO enhancement. Actor_Draw still calls actor->shape.shadowDraw, and ActorShadow_Draw (z_actor.c) is intact.

ROOT CAUSE: ActorShadow_Draw places the shadow with func_80038A28(actor->floorPoly, world.pos.x, actor->floorHeight, world.pos.z, &mtx) — it uses actor->floorHeight, the N64 COLLISION floor, and never reads world.pos.y. The visible ground is the OoT3D terrain at a different height. That gap is exactly what Zelda3D_ActorRenderYOffset compensates for when drawing the MODEL: z_actor.c offsets world.pos.y for the draw and restores it immediately after. The shadow draw is both OUTSIDE that bracket and immune to it (it never reads world.pos.y), so it renders buried in or floating off the OoT3D ground.

FIX DIRECTION: place the shadow on the OoT3D render ground — floorHeight + Zelda3D_ActorRenderYOffset(...), or the OoT3D ground height at that XZ via Zelda3D_RenderYOffsetAtXZ (memory soh3d-terrain-warp). Both height-derived terms in ActorShadow_Draw (the -50..500 visibility gate, and the alpha/scale falloff from world.pos.y - floorHeight) remain correct provided the two heights are shifted consistently. Affects EVERY actor, so verify on an NPC as well as Link.

WHY IT LOOKED LIKE A REGRESSION IN A LIVE SYSTEM: docs/codemap.md carried a stale 'Shadows + AO — done, don't reopen without a specific regression' row describing the Zelda3D dynamic sun-shadow + SSAO enhancements, which were DELETED 2026-07-16 ('OoT3D lighting only', memory soh3d-title-no-loop-and-effects-removed). Two records disagreed; the code was the tiebreak. Row corrected.

### Resolution (2026-07-28)
SUPERSEDED — the first diagnosis (shadow placed at the N64 collision floor instead of the OoT3D render ground) was WRONG, and the fix built on it was a measured no-op: dark-pixel counts under Link were identical before and after (mean 171.2, min 87, both). Two reasons it could not have been the cause: (a) Zelda3D_TerrainWarpEnabled() returns 0 whenever OoT3D collision is active, so Zelda3D_RenderYOffsetAtXZ yields 0 in normal gameplay; (b) when OoT3D collision IS in use the N64 floor and the render ground already coincide. Change reverted rather than left in with a misleading comment. REAL CAUSE, see issue #7.
