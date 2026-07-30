---
id: C029
kind: claim
status: holds
created: 2026-07-30
tags: 
reconfirmed: 2026-07-30
---

## Claim

The OoT3D Deku Tree mouth is a MOVING single-mesh actor (closed->open lerp), so its geometry must exist somewhere in the ROM and the fix is NOT draw suppression

## Evidence

z_bg_treemouth.c: BgTreemouth_Draw always emits exactly ONE display list (gDekuTreeMouthDL) with only an env-colour alpha varying, and lines 226-228 drive the actor position as a lerp from closed (4029,136,-1255) to open (3869,-263,-1163). Confirmed the observed actor sits at the open endpoint exactly. So one mesh moves; there is no separate closed-state mesh. ahide showed its complete visual contribution: a dark lip, at the open position only.

## What would falsify it

The mouth mesh is shown NOT to move in OoT3D specifically (Grezzo could have rebuilt the actor), or a closed-state mesh separate from gDekuTreeMouthDL is found

## Re-confirmed 2026-07-30

Resolved the right way round: the mouth mesh is REPLACED with OoT3D's spot04_kuchi_model.cmb (from zelda_spo04_objects.zar -- OoT3D drops the t) rather than suppressed, so it still moves with the closed->open lerp and the closed state keeps its geometry. Verified the CMB draws rather than vanishing: 9337 px vs the actor-hidden frame, with a footprint (9337) matching the N64 mesh's own (9257). Measured scale exactly 1.0 (n64h 415.0 / modelh 415.0).
