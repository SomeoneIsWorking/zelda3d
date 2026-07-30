---
id: C024
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

Forced-CMB slots (sActorForcedAuto) can complete a bbox measurement and draw, for non-skinned entries

## Evidence

Root cause was a keyed measure routed to sAuto[objId] while the slot reads forced->entry, so measuredH was unreachable by construction and tries hit the 8-try give-up. After reserving ZELDA3D_MEASKEY_FORCED_BASE+index: live Hyrule Field, spawn 0x5E params 0x2000 -> forced[1] actor=0x5e |syokudai_ki state=2 scale=0.95016 n64h=58.0 model=2014 tries=1, and the framed torch draws the OoT3D lashed-wood tripod instead of the N64 mesh.

## What would falsify it

A forced non-skinned slot is observed stuck at state=1/3 with tries climbing, or a new forced entry is added without a distinct measure key
