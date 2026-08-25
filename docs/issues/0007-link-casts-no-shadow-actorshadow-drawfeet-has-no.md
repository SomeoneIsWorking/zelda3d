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
Zelda3D_UpdateSkelColliders' remedy (z_skelanime.c, #107/#108) applied to the player's dedicated hook.
`gZelda3dColliderPass` is owned by `zelda3d/anim/skeleton_draw_bridge.{h,c}` (declaration in the
header, definition in the C owner), rather than the compatibility umbrella `zelda3d.h`.

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

## Follow-up: the mechanism is back, the STRENGTH is not (new frontier row `player.shadow-strength`)

Compared against the OoT3D oracle (embedded Azahar, entrance 0xEE Kokiri Forest, daytime 0x6000;
`tools/oracle_shot.py --settle 400`). Shadow contrast, measured as green(under-boots) / green(clean
grass in the same frame):

| frame                       | ratio | reading            |
|-----------------------------|-------|--------------------|
| Zelda3D BEFORE the #206 fix | 1.01  | no shadow at all   |
| Zelda3D AFTER the #206 fix  | 0.91  | shadow present, 9% |
| OoT3D oracle                | 0.65  | 35% darkening      |

So #206 is genuinely fixed — the ratio moves off 1.0 — but OoT3D's shadow is roughly four times the
contrast and visibly larger and softer. That is a SEPARATE, un-RE'd gap, tracked as frontier row
`player.shadow-strength`; do not close it by tuning shadowAlpha/shadowScale.

Caveat on the numbers: the oracle and Zelda3D cameras/poses are not identical, so the two sample
boxes do not cover identical geometry. The before/after/oracle ordering is solid; the exact ratio is
not. Also note the oracle at daytime 0x4000 shows NO visible shadow under Link either
(`scratch/screenshots/oracle_t0x4000_feet.png`), so the shadow is light-dependent on the 3DS as well.

## The follow-up gap was a MEASUREMENT ARTIFACT — closed

The "OoT3D's shadow is 4x the contrast" table above is WRONG, and the error was the comparison, not
the renderer: the two frames were not lighting-matched. Shadow contrast, green(under-boots) /
green(clean grass) measured inside the SAME frame:

| frame                        | ratio |
|------------------------------|-------|
| ours, daytime 0x4000         | 0.862 |
| ours, daytime 0x6000         | 0.868 |
| ours, daytime 0xB000 (low sun)| 0.624 |
| OoT3D oracle (`oracle_kday`) | 0.654 |

The oracle frame matches our LOW-SUN frame, not our 0x6000 frame — so `oracle_shot.py --daytime
0x6000` did not put the oracle at the same sun position our game was at. With the lighting matched,
the two agree to within the placement error of the sample boxes.

Visually the same: at 0xB000 our shadow is large, dark and elongated exactly like the oracle's
(`scratch/screenshots/sh_0xB000.png`); at 0x4000 it is nearly absent (`sh_0x4000.png`). That is the
shared `4.5f - light->l.dir[1] * 0.035f` stretch and the shared `min(1, w*5e-5) * shadowAlpha`
darkening doing exactly what they should.

Frontier row `player.shadow-strength` is closed as skip-by-design. **Lesson:** an oracle-vs-ours
pixel comparison is meaningless unless time-of-day is verified equal on BOTH sides; passing
`--daytime` to the oracle is not proof that it landed there.
