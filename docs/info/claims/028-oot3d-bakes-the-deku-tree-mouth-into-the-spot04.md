---
id: C028
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

OoT3D bakes the Deku Tree mouth into the spot04 room mesh; the N64 Bg_Treemouth actor draws only a redundant threshold lip

## Evidence

No /actor/zelda_spot04_objects.zar exists (enumerated all 461 ROM .zar files) and /scene/spot04.zar contains no CMB (13 entries: one cmab, one qdb, eleven ctxb). actorsnear reports id=0x3E as --N64--. With ahide 1 the tunnel, trunk, bark and ground all remain complete and only a flat dark lip in front of the mouth disappears (9257 px vs a 574 px noise floor). Reached via a clean boot + warp 0x209 as the first action.

## What would falsify it

The mouth's CLOSED state turns out to be drawn by the actor, which would mean the room mesh does not contain the whole mouth and suppressing the draw leaves the entrance permanently open
