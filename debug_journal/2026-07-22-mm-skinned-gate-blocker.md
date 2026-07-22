# 2026-07-22 — MM skinned actors: why the gate must stay (a misplaced actor, with a repro)

`ZELDA3D_MM_SKINNED` has been off by default since the CSAB architecture landed, held back "pending
per-animation playback verification". Now that MM can be driven reliably (warp + stick), I checked it.

## Repro

    ZELDA3D_MM_COLLISION=1 ZELDA3D_MM_SKINNED=1 tools/mm_game.sh start
    # default spawn (South Clock Town), no movement needed
    tools/mm_game.sh shot mm_skinned_ct

A/B against the same frame with `ZELDA3D_MM_SKINNED=0` (`scratch/screenshots/mm_noskin_ct.png`).

## What the A/B shows

GOOD — the port does what it claims: the dog swaps from the N64 model to its 3DS CSAB-animated one
(visibly cleaner/white vs scruffy brown). 7 skinned objects load, 23 models total, and the anim table
does its job — exactly ONE unmapped animation in the whole scene
(`ovl_En_Sth/gEnSthLookUpAnim -> default an_hokiwait`), which is the known overlay-anim gap.

BAD — **a large red/white conical object renders at Link's head height** with skinned ON, and is
absent with skinned OFF. It reads as a Japanese parasol/hat. Whatever it is, it is drawn attached to
or near the player rather than at its own actor position.

## Assessment

This is a placement/attachment defect in the skinned path, not an animation-selection defect: the
anim table is behaving (1/N unmapped), and the model substitution is correct for the dog. The most
likely suspects are the pending-model mechanism binding a skinned model to the wrong actor, or a
scale/offset applied from the wrong source — `Zelda3D_MM_SetPending` / `Zelda3D_MM_OverridePending`
carry worldScale + groundOffset per actor, and a mismatch there would put a model on the player.

**So the gate is JUSTIFIED and stays off.** Un-gating today would ship a visible floating object in
the game's first scene. This is a concrete, one-screenshot repro rather than the vague "needs
playback verification" the gate previously carried — which is progress even though it is a negative
result.

## Investigation round 1 (2026-07-22)

Actor dump at the default spawn shows **#0: id=0x010 obj=0x001 cat=7 at (-278.7, 38.1, -756.7)** —
Link's exact XZ at head height, i.e. precisely where the cone renders. But `obj=0x001` is
gameplay_keep, which is NOT one of the 7 skinned objects loaded (0x107 mm, 0x1CB pst, 0x223 lodmoon,
0x0E2 an1, 0x1B6 sdn, 0x132 dog, 0x00C box). So that actor should never receive a 3DS model.

That suggested a stale-pending leak, and **a real latent bug was found while checking it**:
`Zelda3D_MM_AfterActorDraw()` exists solely to clear the deferred `{actor, modelId, scale,
groundOffset}` slot, and it had **ZERO CALLERS**. `SetPending` fires in `mm3d_draw.c:161`; nothing
ever cleared it, so the slot outlived its owner and the next actor reaching
`Zelda3D_MM_SkelAnimeDrawRaw` could consume a stale entry.

**Fixed** (clear now called in `z_actor.c` right after the actor's draw). **But this did NOT remove
the cone** — it is still present in `scratch/screenshots/mm_skinned_fixed.png`. So the leak was a
genuine bug worth fixing on its own merits, and the cone has a DIFFERENT cause. Do not re-attribute
it to pending state.

## CULPRIT FOUND (round 2) — it is a SCALE bug, not a placement bug

Added `ZELDA3D_MM_DBG_SKIN=1` (one line per skinned emit: model, actorId, actor world pos, scale,
groundOffset, csab). At the default spawn only TWO skinned actors emit:

    [MM3D-SKIN] model=10 actorId=0x1C7 pos=(-280,0,680)   scale=0.010 groundOff=1.7 csab=sdn_lastwait
    [MM3D-SKIN] model=11 actorId=0x0E2 pos=(-239,0,-315)  scale=0.008 groundOff=0.0 csab=dog_run

Link is at (-278, 0, -752) with yaw=0, i.e. facing +Z — so actor 0x1C7 at z=680 is ~1430 units
DIRECTLY AHEAD, which is exactly where the cone renders on screen. **It is not misplaced.** It is
drawn at its correct world position and is simply far too BIG: `scale=0.010` where every other mapped
MM actor in this scene uses `0.1000`. A model that reads as a huge parasol at 1400 units is a
scale-derivation failure, not an attachment failure.

So the earlier "attached to Link" reading was wrong — the object only LOOKED head-height because it
sits on the camera axis at distance. Corrected here rather than left standing.

**ROUND 3 — the STOPGAP for this exact rig is now STALE (my CMB fixes resolved it).**
`Zelda3D_MM_OverridePending` carries a STOPGAP naming `sdn` explicitly: "two MM3D archives (sdn, cs)
yield a minY of ~1e34+ CMB units — the BindPose vert data blends through weird bone matrices during
buildDrawGroups and accumulates junk on those specific rigs."

That 1e34 garbage-vertex signature is the SAME class of defect fixed earlier this session in
`cmb.cpp` (the index-region offset producing ~1e38 positions, plus the version-gated mesh stride).
Measured now with `ZELDA3D_MM_SCALE_LOG=1`:

    [MM3D-SCALE] modelId=10 worldScale=0.01000 groundOff=1.689     <- sdn
    [MM3D-SCALE] modelId=11 worldScale=0.00750 groundOff=-0.001    <- dog

`groundOff=1.689` is a NORMAL value, not 1e34. So the STOPGAP's premise no longer holds for sdn and
the clamp is now dead code guarding a bug that has been fixed. It should be removed once cs is
checked the same way — but only after confirming, not on this one data point.

**And this undercuts my own "scale bug" conclusion from round 2.** 0.010 is not an arbitrary number:
it is the bone-length-sum RATIO the auto-scale is designed to produce, and the dog's 0.0075 (which
renders correctly) is equally far from the 0.1 default. So a scale of 0.010 is not evidence of a
fault by itself. I do not currently know what actor 0x1C7 / object `sdn` IS, and without that I
cannot say the render is wrong — a large festival parasol may be exactly right for that spot.

Honest status: the cone is UNEXPLAINED. Ruled out so far: stale pending state (round 1), misplacement
(round 2 — it is on the camera axis at ~1430 units, drawn at its correct world position), and now
corrupt bind-pose verts (round 3 — groundOff is sane). Next step is identification, not fixing:
determine what actor id 0x1C7 / object 0x1B6 `sdn` is in MM, and compare the 3DS model against its
N64 counterpart at the same spot.

Superseded fix direction: `sdn`'s worldScale comes from the auto-scale path (`Zelda3D_MM_ModelBoneLenSum` /
`Zelda3D_MM_OverridePending`, the HEIGHT-based derivation). Check why it lands on 0.010 for this
model — a bone-length sum near zero, or a rig whose bind pose defeats the height heuristic, would
produce exactly this. `mscale 0x1B6 <s>` can confirm the right value live before changing the
derivation. Note the dog's 0.008 is also off the 0.1 norm yet looks correct, so the derivation is
not uniformly wrong — compare the two.

## Superseded next step (kept: the reasoning was sound, the conclusion was not)

The offending draw evidently does not go through `Actor_Draw`'s pending path at all. Candidates to
check next:
- a model drawn from the PLAYER draw path (mm3d_player.c is a draw-only stub delegating to the
  generic actor path) rather than from an actor's own Draw;
- the two `csab not found: dk2_shiji` / `dk2_matsu` failures in the log — a failed CSAB resolve may
  leave a model bound to a default rig/position;
- instrument `Zelda3D_MM_EmitModelDraw` to log actorId + world pos + modelId for one frame and find
  the draw whose position does not match its actor.
