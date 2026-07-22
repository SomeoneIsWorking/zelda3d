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

## Next step (cone still open)

The offending draw evidently does not go through `Actor_Draw`'s pending path at all. Candidates to
check next:
- a model drawn from the PLAYER draw path (mm3d_player.c is a draw-only stub delegating to the
  generic actor path) rather than from an actor's own Draw;
- the two `csab not found: dk2_shiji` / `dk2_matsu` failures in the log — a failed CSAB resolve may
  leave a model bound to a default rig/position;
- instrument `Zelda3D_MM_EmitModelDraw` to log actorId + world pos + modelId for one frame and find
  the draw whose position does not match its actor.
