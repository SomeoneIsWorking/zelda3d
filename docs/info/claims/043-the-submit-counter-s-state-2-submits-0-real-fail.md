---
id: C043
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

The submit counter's state=2/submits=0 'real failure' signature is only valid for UNSKINNED slots

## Evidence

Ice Cavern ice_wall_modelT reads state=2 submits=0 yet is benign: Zelda3D_TryAuto sends any skinned CMB straight to state 2 with no measurement and defers to the SkelAnime hook, which never fires on a static Bg_ actor, so the N64 draw stays in control. autostate/submitted now print skin= to separate the two. Verified live in Ice Cavern: skin=1 for that slot, skin=0 for the five that draw.

## What would falsify it

a slot reading state=2 skin=1 submits=0 that is nonetheless visibly missing its N64 draw, or the auto path gaining bind-pose drawing for skeleton-less actors
