---
id: C028
kind: claim
status: falsified
created: 2026-07-30
tags: 
falsified_on: 2026-07-30
---

## Claim

OoT3D bakes the Deku Tree mouth into the spot04 room mesh; the N64 Bg_Treemouth actor draws only a redundant threshold lip

## Evidence

No /actor/zelda_spot04_objects.zar exists (enumerated all 461 ROM .zar files) and /scene/spot04.zar contains no CMB (13 entries: one cmab, one qdb, eleven ctxb). actorsnear reports id=0x3E as --N64--. With ahide 1 the tunnel, trunk, bark and ground all remain complete and only a flat dark lip in front of the mouth disappears (9257 px vs a 574 px noise floor). Reached via a clean boot + warp 0x209 as the first action.

## What would falsify it

The mouth's CLOSED state turns out to be drawn by the actor, which would mean the room mesh does not contain the whole mouth and suppressing the draw leaves the entrance permanently open

## FALSIFIED 2026-07-30

The inference was wrong, though the observation was right. BgTreemouth's position is a LERP (z_bg_treemouth.c:226-228): closed (4029,136,-1255) -> open (3869,-263,-1163). The open endpoint is EXACTLY the position I observed, so the ahide capture was taken with the mouth fully open, where the room mesh happens to cover the area. A mesh that MOVES between two positions cannot be baked into a static room mesh in both states, so 'baked and redundant' cannot be right -- suppressing the draw would make the CLOSED mouth invisible and the entrance look open before Link is allowed in. Corrected by C029.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
