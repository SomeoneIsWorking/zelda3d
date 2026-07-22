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

## Next step

Identify the offending actor: log each skinned draw's actorId + world pos + the model id it resolved
to, for one frame at the default spawn, and find the one whose draw position does not match its actor
position. `csab not found: dk2_shiji` / `dk2_matsu` also appear in the log and may be related (a
failed CSAB resolve leaving a model bound to a default).
