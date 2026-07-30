---
id: C029
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

The OoT3D Deku Tree mouth is a MOVING single-mesh actor (closed->open lerp), so its geometry must exist somewhere in the ROM and the fix is NOT draw suppression

## Evidence

z_bg_treemouth.c: BgTreemouth_Draw always emits exactly ONE display list (gDekuTreeMouthDL) with only an env-colour alpha varying, and lines 226-228 drive the actor position as a lerp from closed (4029,136,-1255) to open (3869,-263,-1163). Confirmed the observed actor sits at the open endpoint exactly. So one mesh moves; there is no separate closed-state mesh. ahide showed its complete visual contribution: a dark lip, at the open position only.

## What would falsify it

The mouth mesh is shown NOT to move in OoT3D specifically (Grezzo could have rebuilt the actor), or a closed-state mesh separate from gDekuTreeMouthDL is found
