---
id: C090
kind: claim
status: holds
created: 2026-08-30
tags: mm3d,player,graphics
depends: 2ship/2s2h/zelda3d/mm3d_player_left_hand.cpp#StateForPlayer, 2ship/src/overlays/actors/ovl_En_Boom/z_en_boom.c#EnBoom_Destroy
---

## Claim

MM3D En_Boom is one exact producer of Player+0x129bc bit 16, and its open-hand lifetime is represented in 2S2H by zoraBoomerangActor pointing to ACTOR_EN_BOOM

## Evidence

MM3D En_Boom update 0x003cc27c and flight action 0x0057c7f4 set 0x10000; destructor 0x00353e8c clears it, maintains the sibling, clears PLAYER_STATE1_ZORA_BOOMERANG_THROWN on the last actor, and sets stateFlags3 0x00800000 exactly like 2S2H EnBoom_Destroy. A native Great Bay Coast B-charge/release held leftHandType at 1 while zoraBoomerangActorId transitioned -1 -> 32 -> -1 and the shipping left-hand mask transitioned closed 0x4 -> open 0x2 -> closed 0x4. Player helper 0x002250f0 is a separate mount-transition producer and is not covered by this claim.

## What would falsify it

The En_Boom functions are shown not to write bit 16, their destructor transitions do not match 2S2H EnBoom_Destroy, or the typed actor pointer does not cover the En_Boom lifetime.
